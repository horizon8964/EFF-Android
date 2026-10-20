#include "runtime_loader.h"
#include <dobby.h>
#include <android/log.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <dlfcn.h>

namespace {
std::string diagnostics, modRoot;
std::mutex logMutex;
std::atomic<bool> active{false};
std::atomic<unsigned> substitutions{0};
std::atomic<bool> setupFinished{false};
struct Route { std::string logical, chunk, file; uint64_t offset, inputBytes, outputBytes; };
std::map<std::pair<std::string,uint64_t>, Route> routes;
thread_local bool replacing = false;
struct Api {
    const void* (*assemblyOpen)(void*,const char*);
    const void* (*image)(const void*);
    void* (*klass)(const void*,const char*,const char*);
    const void* (*methods)(void*,void**);
    const char* (*methodName)(const void*);
    uint32_t (*paramCount)(const void*);
    const void* (*param)(const void*,uint32_t);
    const void* (*returns)(const void*);
    uint32_t (*flags)(const void*,uint32_t*);
    char* (*typeName)(const void*);
    void (*freeName)(void*);
    const uint16_t* (*chars)(void*);
    int32_t (*length)(void*);
    void* (*newString)(const char*);
    void* (*invoke)(const void*,void*,void**,void**);
    void* (*unbox)(void*);
    const void* (*classType)(void*);
    void* (*typeObject)(const void*);
    uint32_t (*root)(void*,bool);
    void* (*field)(void*,const char*);
    void (*staticField)(void*,void*);
} api{};
using FileFn = void* (*)(void*,uint32_t,uint64_t,const void*);
using HelperFn = void* (*)(void*,void*,uint64_t,uint8_t,uint32_t,const void*);
using OtherFn = void* (*)(void*,void*,uint64_t,uint32_t,const void*);
FileFn originalFile=nullptr, originalAsync=nullptr, originalInternal=nullptr, originalInternalAsync=nullptr;
HelperFn originalHelper=nullptr, originalHelperAsync=nullptr;
OtherFn originalStream=nullptr, originalStreamAsync=nullptr, originalPersist=nullptr, originalPersistAsync=nullptr;
const void *fileInfo=nullptr, *asyncInfo=nullptr;
const void *pathImplicit=nullptr,*loadResource=nullptr,*resourceDone=nullptr,*resourceError=nullptr,*resourceGet=nullptr;
const void *resourcesInitialized=nullptr,*resourcesIniting=nullptr;
void* objectType=nullptr;
uint64_t rootCategory=0,resourcePriority=0;
struct WarmHandle{void* boxed;std::string path;uint32_t root;};
std::vector<WarmHandle> warmHandles;
std::vector<std::string> warmPaths;
bool warmQueued=false,warmComplete=false;
using SelfFn=int (*)(int);
SelfFn selfOriginal=nullptr;
__attribute__((noinline)) int SelfSource(int value){return value*7+3;}
__attribute__((noinline)) int SelfDetour(int value){return selfOriginal(value)+1000;}

bool HookSelfTest() {
    SelfFn volatile call=&SelfSource;
    if(call(5)!=38)return false;
    if(DobbyHook(reinterpret_cast<void*>(&SelfSource),reinterpret_cast<void*>(&SelfDetour),reinterpret_cast<void**>(&selfOriginal))!=0)return false;
    const bool applied=call(5)==1038;
    const bool restored=DobbyDestroy(reinterpret_cast<void*>(&SelfSource))==0;
    return applied&&restored&&call(5)==38;
}

void Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::ofstream file(diagnostics + "/loader.log", std::ios::app);
    file << message << '\n';
    __android_log_print(ANDROID_LOG_INFO, "EFFLoader", "%s", message.c_str());
}
void Status(const char* stage, unsigned hooks) {
    std::lock_guard<std::mutex> lock(logMutex);
    const auto path=diagnostics+"/loader-status.json";
    std::ofstream file(path+".tmp");
    file << "{\"build\":\"" << EFF_ANDROID_VERSION << "\",\"stage\":\"" << stage << "\",\"routes\":" << routes.size()
         << ",\"hooks\":" << hooks << ",\"substitutions\":" << substitutions.load() << ",\"active\":" << (active.load()?"true":"false") << "}\n";
    file.close(); std::rename((path+".tmp").c_str(),path.c_str());
}
std::string String(void* text) {
    if(!text) return {};
    const auto n=api.length(text);
    if(n<0||n>16384) return {};
    const auto chars=api.chars(text);
    std::string out; out.reserve(static_cast<size_t>(n));
    for(int32_t i=0;i<n;++i) {
        const auto c=chars[i];
        // All compiled VFS keys and device paths are ASCII. Unrelated Unicode paths pass through.
        if(c>127) return {};
        out+=c=='\\'?'/':static_cast<char>(c);
    }
    return out;
}
std::string Base(const std::string& value) {
    auto at=value.find_last_of('/');
    std::string result=at==std::string::npos?value:value.substr(at+1);
    for(char& c:result) if(c>='a'&&c<='z') c-=32;
    return result;
}
const Route* Find(void* first,void* second,uint64_t offset) {
    if(!active.load(std::memory_order_acquire)||replacing) return nullptr;
    auto a=String(first), b=String(second);
    for(const auto* text:{&a,&b}) {
        auto found=routes.find({Base(*text),offset});
        if(found!=routes.end()) return &found->second;
    }
    return nullptr;
}
std::atomic<unsigned> installedHooks{0};
void* Substitute(const Route& route,bool async) {
    struct Guard { Guard(){replacing=true;} ~Guard(){replacing=false;} } guard;
    void* path=api.newString(route.file.c_str());
    if(!path) throw std::runtime_error("managed replacement path allocation failed");
    void* result=async?originalAsync(path,0,0,asyncInfo):originalFile(path,0,0,fileInfo);
    ++substitutions;
    Log(std::string(result?"SUBSTITUTED ":"LOAD-FAILED ")+route.logical+" bytes="+std::to_string(route.outputBytes)+(async?" async":" sync"));
    Status("running",installedHooks.load());
    return result;
}
#define FILE_DETOUR(name,original,isAsync) \
void* name(void* path,uint32_t crc,uint64_t offset,const void* info) { \
    if(const Route* route=Find(path,nullptr,offset)) { try { return Substitute(*route,isAsync); } catch(const std::exception& e){Log(e.what());} } \
    return original(path,crc,offset,info); }
FILE_DETOUR(FileHook,originalFile,false)
FILE_DETOUR(AsyncHook,originalAsync,true)
FILE_DETOUR(InternalHook,originalInternal,false)
FILE_DETOUR(InternalAsyncHook,originalInternalAsync,true)
#define HELPER_DETOUR(name,original,isAsync) \
void* name(void* dir,void* file,uint64_t offset,uint8_t pos,uint32_t crc,const void* info) { \
    if(const Route* route=Find(dir,file,offset)) { try { return Substitute(*route,isAsync); } catch(const std::exception& e){Log(e.what());} } \
    return original(dir,file,offset,pos,crc,info); }
HELPER_DETOUR(HelperHook,originalHelper,false)
HELPER_DETOUR(HelperAsyncHook,originalHelperAsync,true)
#define OTHER_DETOUR(name,original,isAsync) \
void* name(void* dir,void* file,uint64_t offset,uint32_t crc,const void* info) { \
    if(const Route* route=Find(dir,file,offset)) { try { return Substitute(*route,isAsync); } catch(const std::exception& e){Log(e.what());} } \
    return original(dir,file,offset,crc,info); }
OTHER_DETOUR(StreamHook,originalStream,false)
OTHER_DETOUR(StreamAsyncHook,originalStreamAsync,true)
OTHER_DETOUR(PersistHook,originalPersist,false)
OTHER_DETOUR(PersistAsyncHook,originalPersistAsync,true)

std::string Type(const void* type) { char* name=api.typeName(type); std::string value=name?name:""; if(name)api.freeName(name); return value; }
const void* Method(void* domain,const char* assembly,const char* space,const char* klass,const char* name,const char* returns,std::initializer_list<const char*> parameters,bool isStatic=true) {
    const auto loaded=api.assemblyOpen(domain,assembly);
    if(!loaded) return nullptr;
    const auto cls=api.klass(api.image(loaded),space,klass);
    if(!cls) return nullptr;
    const void* found=nullptr;void* iterator=nullptr;
    while(const void* m=api.methods(cls,&iterator)) {
        if(std::strcmp(api.methodName(m),name)!=0||api.paramCount(m)!=parameters.size()||Type(api.returns(m))!=returns)continue;
        uint32_t impl=0;if(bool(api.flags(m,&impl)&0x10)!=isStatic)continue;
        size_t i=0;bool match=true;for(const char* p:parameters)if(Type(api.param(m,static_cast<uint32_t>(i++)))!=p)match=false;
        if(match){if(found)throw std::runtime_error("ambiguous loader method");found=m;}
    }
    return found;
}
bool Executable(void* address) {
    std::ifstream maps("/proc/self/maps");std::string line;const auto p=reinterpret_cast<uintptr_t>(address);
    while(std::getline(maps,line)){unsigned long low=0,high=0;char permissions[5]{};
        if(std::sscanf(line.c_str(),"%lx-%lx %4s",&low,&high,permissions)==3&&p>=low&&p<high)return permissions[0]=='r'&&permissions[2]=='x';}
    return false;
}
std::vector<std::string> Split(const std::string& text) {
    std::istringstream stream(text);std::string item;std::vector<std::string> values;
    while(std::getline(stream,item,'\t'))values.push_back(item);return values;
}
} // namespace

void StartEffLoader(void* library,void* domain,const std::string& root) {
    diagnostics=root;
    const char* configured=std::getenv("EFF_ANDROID_MOD_ROOT");
    if(!configured||!*configured){Status("no_verified_mod",0);setupFinished.store(true);return;}
    modRoot=configured;
    std::vector<void*> installed;
    try {
#define RESOLVE(field,name) api.field=reinterpret_cast<decltype(api.field)>(dlsym(library,name)); if(!api.field)throw std::runtime_error("missing " name)
        RESOLVE(assemblyOpen,"il2cpp_domain_assembly_open"); RESOLVE(image,"il2cpp_assembly_get_image");
        RESOLVE(klass,"il2cpp_class_from_name"); RESOLVE(methods,"il2cpp_class_get_methods");
        RESOLVE(methodName,"il2cpp_method_get_name"); RESOLVE(paramCount,"il2cpp_method_get_param_count");
        RESOLVE(param,"il2cpp_method_get_param"); RESOLVE(returns,"il2cpp_method_get_return_type");
        RESOLVE(flags,"il2cpp_method_get_flags"); RESOLVE(typeName,"il2cpp_type_get_name"); RESOLVE(freeName,"il2cpp_free");
        RESOLVE(chars,"il2cpp_string_chars"); RESOLVE(length,"il2cpp_string_length"); RESOLVE(newString,"il2cpp_string_new");
        RESOLVE(invoke,"il2cpp_runtime_invoke");RESOLVE(unbox,"il2cpp_object_unbox");
        RESOLVE(classType,"il2cpp_class_get_type");RESOLVE(typeObject,"il2cpp_type_get_object");
        RESOLVE(root,"il2cpp_gchandle_new");RESOLVE(field,"il2cpp_class_get_field_from_name");RESOLVE(staticField,"il2cpp_field_static_get_value");
#undef RESOLVE
        std::ifstream table(modRoot+"/routes.tsv");std::string line;
        if(!std::getline(table,line))throw std::runtime_error("missing route header");
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line!="EFF_ANDROID_ROUTES\t1")throw std::runtime_error("invalid route header");
        while(std::getline(table,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            auto c=Split(line);if(c.size()!=8||c[5].find('/')!=std::string::npos||c[5].find("..")!=std::string::npos)throw std::runtime_error("invalid route row");
            Route route{c[0],Base(c[1]),modRoot+"/bundles/"+c[5],std::stoull(c[2]),std::stoull(c[3]),std::stoull(c[6])};
            if(!routes.emplace(std::make_pair(route.chunk,route.offset),std::move(route)).second)throw std::runtime_error("duplicate source range");
        }
        if(routes.empty())throw std::runtime_error("empty routes");
        auto cls=[&](const char* assembly,const char* space,const char* name){const auto a=api.assemblyOpen(domain,assembly);return a?api.klass(api.image(a),space,name):nullptr;};
        auto objectClass=cls("UnityEngine.CoreModule.dll","UnityEngine","Object");
        if(!objectClass)throw std::runtime_error("UnityEngine.Object type unavailable");
        objectType=api.typeObject(api.classType(objectClass));api.root(objectType,false);
        pathImplicit=Method(domain,"Common.Beyond.dll","Beyond.Resource","StringPathHash","op_Implicit","Beyond.Resource.StringPathHash",{"System.String"});
        loadResource=Method(domain,"Common.Beyond.dll","Beyond.Resource","ResourceManager","LoadAsync","Beyond.Resource.FAssetProxyHandle",{"Beyond.Resource.StringPathHash","System.Type","Beyond.Resource.RootCategory","Beyond.Resource.EResourceRequestPriority"});
        resourceDone=Method(domain,"Common.Beyond.dll","Beyond.Resource","FAssetProxyHandle","get_isDone","System.Boolean",{},false);
        resourceError=Method(domain,"Common.Beyond.dll","Beyond.Resource","FAssetProxyHandle","get_hasError","System.Boolean",{},false);
        resourceGet=Method(domain,"Common.Beyond.dll","Beyond.Resource","FAssetProxyHandle","Get","UnityEngine.Object",{},false);
        resourcesInitialized=Method(domain,"Common.Beyond.dll","Beyond.Resource","ResourceManager","IsInitialized","System.Boolean",{});
        resourcesIniting=Method(domain,"Common.Beyond.dll","Beyond.Resource","ResourceManager","IsAsyncIniting","System.Boolean",{});
        if(!resourcesInitialized||!resourcesIniting)throw std::runtime_error("resource readiness contract absent");
        if(!pathImplicit||!loadResource||!resourceDone||!resourceError||!resourceGet)throw std::runtime_error("native ResourceManager lease contract absent");
        auto mainClass=cls("Common.Beyond.dll","Beyond.Resource","RootCategory");
        auto priorityClass=cls("Common.Beyond.dll","Beyond.Resource","EResourceRequestPriority");
        auto mainField=mainClass?api.field(mainClass,"Main"):nullptr;
        auto priorityField=priorityClass?api.field(priorityClass,"Default"):nullptr;
        if(!mainField||!priorityField)throw std::runtime_error("resource category/priority constants absent");
        api.staticField(mainField,&rootCategory);api.staticField(priorityField,&resourcePriority);
        std::ifstream prewarm(modRoot+"/prewarm.txt");
        while(std::getline(prewarm,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty())warmPaths.push_back(line);}
        if(warmPaths.empty())throw std::runtime_error("no source resource leases declared");
        if(!HookSelfTest())throw std::runtime_error("ARM64 hook/restore self-test failed");
        Log("SELFTEST ARM64 trampoline and restoration passed");
        auto file=[&](const char* name,const char* returns){return Method(domain,"UnityEngine.AssetBundleModule.dll","UnityEngine","AssetBundle",name,returns,{"System.String","System.UInt32","System.UInt64"});};
        fileInfo=file("LoadFromFile","UnityEngine.AssetBundle");asyncInfo=file("LoadFromFileAsync","UnityEngine.AssetBundleCreateRequest");
        struct Binding{const void* info;void* detour;void** original;const char* label;};
        std::vector<Binding> bindings={{fileInfo,reinterpret_cast<void*>(FileHook),reinterpret_cast<void**>(&originalFile),"LoadFromFile"},
            {asyncInfo,reinterpret_cast<void*>(AsyncHook),reinterpret_cast<void**>(&originalAsync),"LoadFromFileAsync"},
            {file("LoadFromFile_Internal","UnityEngine.AssetBundle"),reinterpret_cast<void*>(InternalHook),reinterpret_cast<void**>(&originalInternal),"LoadFromFile_Internal"},
            {file("LoadFromFileAsync_Internal","UnityEngine.AssetBundleCreateRequest"),reinterpret_cast<void*>(InternalAsyncHook),reinterpret_cast<void**>(&originalInternalAsync),"LoadFromFileAsync_Internal"}};
        auto helper=[&](const char* name,const char* returns){return Method(domain,"Common.Beyond.dll","Beyond.VFS","UnityFileLoaderHelper",name,returns,{"System.String","System.String","System.UInt64","Beyond.VFS.EFileLoaderPosType","System.UInt32"});};
        bindings.push_back({helper("ReadAssetBundle","UnityEngine.AssetBundle"),reinterpret_cast<void*>(HelperHook),reinterpret_cast<void**>(&originalHelper),"ReadAssetBundle"});
        bindings.push_back({helper("ReadAssetBundleAsync","UnityEngine.AssetBundleCreateRequest"),reinterpret_cast<void*>(HelperAsyncHook),reinterpret_cast<void**>(&originalHelperAsync),"ReadAssetBundleAsync"});
        auto other=[&](const char* cls,const char* name,const char* returns){return Method(domain,"Common.Beyond.dll","Beyond.VFS",cls,name,returns,{"System.String","System.String","System.UInt64","System.UInt32"});};
        bindings.push_back({other("UnityStreamingFileHelper","ReadStreamAssetBundle","UnityEngine.AssetBundle"),reinterpret_cast<void*>(StreamHook),reinterpret_cast<void**>(&originalStream),"ReadStreamAssetBundle"});
        bindings.push_back({other("UnityStreamingFileHelper","ReadStreamAssetBundleAsync","UnityEngine.AssetBundleCreateRequest"),reinterpret_cast<void*>(StreamAsyncHook),reinterpret_cast<void**>(&originalStreamAsync),"ReadStreamAssetBundleAsync"});
        bindings.push_back({other("UnityPersistFileHelper","ReadPersistAssetBundle","UnityEngine.AssetBundle"),reinterpret_cast<void*>(PersistHook),reinterpret_cast<void**>(&originalPersist),"ReadPersistAssetBundle"});
        bindings.push_back({other("UnityPersistFileHelper","ReadPersistAssetBundleAsync","UnityEngine.AssetBundleCreateRequest"),reinterpret_cast<void*>(PersistAsyncHook),reinterpret_cast<void**>(&originalPersistAsync),"ReadPersistAssetBundleAsync"});
        std::set<void*> addresses;
        for(const auto& b:bindings){void* address=nullptr;if(b.info)std::memcpy(&address,b.info,sizeof(address));
            if(!Executable(address)||!addresses.insert(address).second)throw std::runtime_error(std::string("loader entry absent/duplicate: ")+b.label);}
        for(const auto& b:bindings){void* address=nullptr;std::memcpy(&address,b.info,sizeof(address));
            if(DobbyHook(address,b.detour,b.original)!=0)throw std::runtime_error(std::string("DobbyHook failed: ")+b.label);
            installed.push_back(address);Log(std::string("HOOK ")+b.label);}
        installedHooks.store(static_cast<unsigned>(installed.size()));
        active.store(true,std::memory_order_release);
        Log("ARMED si routes="+std::to_string(routes.size()));Status("armed",installedHooks.load());
    } catch(const std::exception& e) {
        active.store(false,std::memory_order_release);
        for(auto it=installed.rbegin();it!=installed.rend();++it)DobbyDestroy(*it);
        Log(std::string("REJECTED ")+e.what());Status("rejected",0);
    }
    setupFinished.store(true,std::memory_order_release);
}

bool AdvanceEffFrame() {
    if(!setupFinished.load(std::memory_order_acquire))return false;
    if(!active.load(std::memory_order_acquire)||warmComplete)return true;
    static unsigned frame=0,attempts=0;
    if(++frame%30!=0)return false;
    auto call=[](const void* method,void* self,void** args=nullptr){
        void* exception=nullptr;void* result=api.invoke(method,self,args,&exception);
        if(exception)throw std::runtime_error("managed resource lease invocation failed");return result;
    };
    try {
        if(!warmQueued) {
            // Initial login frames precede mounting the main resource group.
            // Wait for a real replacement request as well as manager initialization.
            if(substitutions.load()==0)return false;
            auto initialized=call(resourcesInitialized,nullptr);
            auto initing=call(resourcesIniting,nullptr);
            if(!initialized||!*static_cast<uint8_t*>(api.unbox(initialized))||!initing||*static_cast<uint8_t*>(api.unbox(initing)))return false;
            // These normal game leases register donor Bundle dependencies in the game's cache.
            for(size_t next=warmHandles.size();next<warmPaths.size();++next) {
                const auto& path=warmPaths[next];
                void* text=api.newString(path.c_str());void* toHash[]={text};
                void* hash=call(pathImplicit,nullptr,toHash);
                if(!hash)throw std::runtime_error("source path hash absent");
                void* args[]={api.unbox(hash),objectType,&rootCategory,&resourcePriority};
                void* handle=call(loadResource,nullptr,args);
                if(!handle)throw std::runtime_error("source resource lease absent");
                warmHandles.push_back({handle,path,api.root(handle,false)});
            }
            warmQueued=true;Log("PREWARM queued="+std::to_string(warmHandles.size()));
        }
        for(const auto& handle:warmHandles) {
            auto done=call(resourceDone,api.unbox(handle.boxed));
            if(!done||!*static_cast<uint8_t*>(api.unbox(done)))return false;
            auto error=call(resourceError,api.unbox(handle.boxed));
            if(error&&*static_cast<uint8_t*>(api.unbox(error))) {Log("PREWARM FAILED "+handle.path);warmComplete=true;return true;}
            if(!call(resourceGet,api.unbox(handle.boxed))) {Log("PREWARM NULL "+handle.path);warmComplete=true;return true;}
        }
        warmComplete=true;Log("PREWARM ready="+std::to_string(warmHandles.size()));Status("donors_ready",installedHooks.load());return true;
    } catch(const std::exception& e) {
        Log(std::string("PREWARM retry ")+e.what());
        if(++attempts>=30){Log("PREWARM could not complete");warmComplete=true;return true;}
        return false;
    }
}

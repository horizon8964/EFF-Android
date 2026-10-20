#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "runtime_loader.h"
#include "hot_loader.h"
#include "compiler.h"

namespace {
std::string Quote(const std::string& text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 32) { char hex[7]; std::snprintf(hex, sizeof(hex), "\\u%04x", c); result += hex; }
                else result += static_cast<char>(c);
        }
    }
    return result + '"';
}

void Save(const std::string& root, const std::string& json) {
    const auto path = root + "/runtime-probe.json";
    const auto temporary = path + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << json << '\n';
    file.close();
    if (!file || std::rename(temporary.c_str(), path.c_str()) != 0)
        __android_log_print(ANDROID_LOG_ERROR, "EFFAndroid", "cannot publish diagnostic report");
}

bool EngineName(const std::string& name) {
    return name.find("il2cpp") != std::string::npos || name.find("libunity") != std::string::npos
        || name.find("GameAssembly") != std::string::npos;
}

std::set<std::string> EngineLibraries() {
    std::set<std::string> paths;
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* opaque) {
        if (info->dlpi_name && EngineName(info->dlpi_name))
            static_cast<std::set<std::string>*>(opaque)->insert(info->dlpi_name);
        return 0;
    }, &paths);
    return paths;
}

struct Library {
    void* handle = nullptr;
    std::string path;
    ~Library() { if (handle) dlclose(handle); }
    bool Open() {
        if (handle) return true;
        auto candidates = EngineLibraries();
        candidates.insert("libil2cpp.so");
        for (const auto& candidate : candidates) {
            void* loaded = dlopen(candidate.c_str(), RTLD_NOW | RTLD_NOLOAD);
            if (!loaded) continue;
            if (dlsym(loaded, "il2cpp_domain_get")) { handle = loaded; path = candidate; return true; }
            dlclose(loaded);
        }
        return false;
    }
};

template<typename T> T Symbol(void* handle, const char* name, std::vector<std::string>& missing) {
    auto value = reinterpret_cast<T>(dlsym(handle, name));
    if (!value) missing.emplace_back(name);
    return value;
}

void Probe(const std::string& root) {
    Library library;
    void* domain = nullptr;
    using DomainGet = void* (*)();
    DomainGet domainGet = nullptr;
    for (int attempt = 0; attempt < 120; ++attempt) {
        if (library.Open()) {
            domainGet = reinterpret_cast<DomainGet>(dlsym(library.handle, "il2cpp_domain_get"));
            domain = domainGet();
            if (domain) break;
        }
        if (attempt == 0) Save(root, "{\"stage\":\"waiting_for_il2cpp\",\"pid\":" + std::to_string(getpid()) + "}");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::ostringstream out;
    out << "{\"schema\":1,\"build\":" << Quote(EFF_ANDROID_VERSION) << ",\"pid\":" << getpid()
        << ",\"stage\":" << Quote(domain ? "metadata_probe" : "il2cpp_unavailable")
        << ",\"library\":" << Quote(library.path) << ",\"engine_libraries\":[";
    bool comma = false;
    for (const auto& path : EngineLibraries()) { if (comma) out << ','; comma = true; out << Quote(path); }
    out << "]";
    if (!domain) {
        out << ",\"model_replacement_enabled\":false}";
        Save(root, out.str());
        return;
    }
    std::vector<std::string> missing;
    auto current = Symbol<void* (*)()>(library.handle, "il2cpp_thread_current", missing);
    auto attach = Symbol<void* (*)(void*)>(library.handle, "il2cpp_thread_attach", missing);
    auto detach = Symbol<void (*)(void*)>(library.handle, "il2cpp_thread_detach", missing);
    // Android may omit enumeration while retaining the named lookup API.
    auto assemblies = reinterpret_cast<const void** (*)(void*, size_t*)>(dlsym(library.handle, "il2cpp_domain_get_assemblies"));
    auto assemblyOpen = reinterpret_cast<const void* (*)(void*, const char*)>(dlsym(library.handle, "il2cpp_domain_assembly_open"));
    if (!assemblies && !assemblyOpen) missing.emplace_back("il2cpp_domain_get_assemblies|il2cpp_domain_assembly_open");
    auto image = Symbol<const void* (*)(const void*)>(library.handle, "il2cpp_assembly_get_image", missing);
    auto imageName = Symbol<const char* (*)(const void*)>(library.handle, "il2cpp_image_get_name", missing);
    auto findClass = Symbol<void* (*)(const void*, const char*, const char*)>(library.handle, "il2cpp_class_from_name", missing);
    auto methods = Symbol<const void* (*)(void*, void**)>(library.handle, "il2cpp_class_get_methods", missing);
    auto methodName = Symbol<const char* (*)(const void*)>(library.handle, "il2cpp_method_get_name", missing);
    auto paramCount = Symbol<uint32_t (*)(const void*)>(library.handle, "il2cpp_method_get_param_count", missing);
    auto param = Symbol<const void* (*)(const void*, uint32_t)>(library.handle, "il2cpp_method_get_param", missing);
    auto returns = Symbol<const void* (*)(const void*)>(library.handle, "il2cpp_method_get_return_type", missing);
    auto typeName = Symbol<char* (*)(const void*)>(library.handle, "il2cpp_type_get_name", missing);
    auto freeName = Symbol<void (*)(void*)>(library.handle, "il2cpp_free", missing);
    auto methodFlags = Symbol<uint32_t (*)(const void*, uint32_t*)>(library.handle, "il2cpp_method_get_flags", missing);
    auto fields = Symbol<void* (*)(void*, void**)>(library.handle, "il2cpp_class_get_fields", missing);
    auto fieldName = Symbol<const char* (*)(void*)>(library.handle, "il2cpp_field_get_name", missing);
    auto fieldType = Symbol<const void* (*)(void*)>(library.handle, "il2cpp_field_get_type", missing);
    auto fieldOffset = Symbol<size_t (*)(void*)>(library.handle, "il2cpp_field_get_offset", missing);
    auto fieldFlags = Symbol<int (*)(void*)>(library.handle, "il2cpp_field_get_flags", missing);
    auto classType = Symbol<const void* (*)(void*)>(library.handle, "il2cpp_class_get_type", missing);
    auto typeKind = Symbol<int (*)(const void*)>(library.handle, "il2cpp_type_get_type", missing);
    auto valueSize = Symbol<int32_t (*)(void*, uint32_t*)>(library.handle, "il2cpp_class_value_size", missing);
    out << ",\"missing_exports\":[";
    for (size_t i = 0; i < missing.size(); ++i) { if (i) out << ','; out << Quote(missing[i]); }
    out << ']';
    if (!missing.empty()) { out << ",\"model_replacement_enabled\":false}"; Save(root, out.str()); return; }
    struct Attachment {
        void* value; void (*release)(void*);
        ~Attachment() { if (value) release(value); }
    } attachment{current() ? nullptr : attach(domain), detach};
    if (!current()) { out << ",\"error\":\"thread_attach_failed\"}"; Save(root, out.str()); return; }
    std::vector<const void*> images;
    if (assemblies) {
        size_t count = 0;
        const auto all = assemblies(domain, &count);
        if (all) for (size_t i = 0; i < count; ++i) if (const void* found = image(all[i])) images.push_back(found);
    } else {
        // Names verified against this client's metadata and the BE Android bindings.
        // This is a targeted inventory, never reported as a complete enumeration.
        const char* names[] = {"UnityEngine.CoreModule.dll", "UnityEngine.AssetBundleModule.dll",
            "Common.Beyond.dll", "Gameplay.Beyond.dll", "Entry.Beyond.dll", "Rendering.Beyond.dll",
            "UI.Beyond.dll", "UI.Gameplay.Beyond.dll", "Unsafe.Beyond.dll", "mscorlib.dll"};
        for (const char* name : names) {
            const void* assembly = assemblyOpen(domain, name);
            if (assembly) if (const void* found = image(assembly)) images.push_back(found);
        }
    }
    const size_t count = images.size();
    out << ",\"assembly_lookup\":" << Quote(assemblies ? "enumeration" : "named_candidates")
        << ",\"assembly_enumeration_available\":" << (assemblies ? "true" : "false");
    out << ",\"assemblies\":[";
    for (size_t i = 0; i < count; ++i) { if (i) out << ','; const char* name = imageName(images[i]); out << Quote(name ? name : ""); }
    out << "],\"classes\":[";
    struct Target { const char* space; const char* name; };
    const Target targets[] = {{"UnityEngine", "AssetBundle"}, {"UnityEngine", "Mesh"},
        {"UnityEngine", "SkinnedMeshRenderer"}, {"UnityEngine", "Texture2D"},
        {"Beyond.VFS", "VirtualFileSystem"}, {"Beyond.VFS", "VFSFileReadStream"},
        {"Beyond.VFS", "UnityFileLoaderHelper"}, {"Beyond.VFS", "UnityStreamingFileHelper"},
        {"Beyond.VFS", "UnityPersistFileHelper"}, {"Beyond.Resource", "StringPathHash"},
        {"Beyond.Resource", "StringPathHashBinary"}, {"Beyond.VFS", "EFileLoaderPosType"},
        {"UnityEngine", "AssetBundleCreateRequest"},
        {"Beyond.Gameplay.View", "BaseModelComponent"}, {"Beyond.Gameplay.View", "ModelManager"},
        {"Beyond.UI", "UIModelLoader"}, {"Beyond.Resource", "FAssetProxyHandle"},
        {"Beyond.Gameplay.View", "ComplexAnimatorComponent"}, {"Beyond.Gameplay.View", "CharacterAnimationComponent"},
        {"Beyond.Gameplay.View", "WeaponComponent"}, {"Beyond.Resource", "ResourceManager"},
        {"UnityEngine", "Object"}, {"UnityEngine", "AsyncOperation"}, {"UnityEngine", "AssetBundleRequest"}};
    auto typeText = [&](const void* type) {
        if (!type) return std::string("<null>");
        char* text = typeName(type);
        std::string result = text ? text : "<null>";
        if (text) freeName(text);
        return result;
    };
    size_t foundCount = 0;
    for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
        const auto& target = targets[t];
        void* klass = nullptr;
        std::string assemblyName;
        for (size_t i = 0; i < count; ++i) {
            const void* candidate = images[i];
            klass = findClass(candidate, target.space, target.name);
            if (klass) { const char* name = imageName(candidate); assemblyName = name ? name : ""; break; }
        }
        if (t) out << ',';
        out << "{\"namespace\":" << Quote(target.space) << ",\"name\":" << Quote(target.name)
            << ",\"found\":" << (klass ? "true" : "false") << ",\"assembly\":" << Quote(assemblyName);
        if (klass && (std::strcmp(target.name, "StringPathHash") == 0 || std::strcmp(target.name, "EFileLoaderPosType") == 0)) {
            const auto type = classType(klass);
            const int kind = type ? typeKind(type) : -1;
            out << ",\"type_kind\":" << kind;
            if (kind == 0x11) {
                uint32_t alignment = 0;
                const auto size = valueSize(klass, &alignment);
                out << ",\"value_size\":" << size << ",\"alignment\":" << alignment << ",\"instance_fields\":[";
                void* iterator = nullptr;
                bool fieldComma = false;
                while (void* field = fields(klass, &iterator)) {
                    if (fieldFlags(field) & 0x10) continue;
                    if (fieldComma) out << ',';
                    fieldComma = true;
                    const char* name = fieldName(field);
                    out << "{\"name\":" << Quote(name ? name : "") << ",\"type\":" << Quote(typeText(fieldType(field)))
                        << ",\"boxed_offset\":" << fieldOffset(field) << '}';
                }
                out << ']';
            }
        }
        out << ",\"methods\":[";
        bool methodComma = false;
        if (klass) {
            ++foundCount;
            void* iterator = nullptr;
            while (const void* method = methods(klass, &iterator)) {
                if (methodComma) out << ',';
                methodComma = true;
                const char* name = methodName(method);
                uint32_t implementationFlags = 0;
                const bool isStatic = (methodFlags(method, &implementationFlags) & 0x10) != 0;
                out << "{\"name\":" << Quote(name ? name : "") << ",\"static\":" << (isStatic ? "true" : "false")
                    << ",\"returns\":" << Quote(typeText(returns(method))) << ",\"parameters\":[";
                const auto n = paramCount(method);
                for (uint32_t p = 0; p < n; ++p) { if (p) out << ','; out << Quote(typeText(param(method, p))); }
                out << "]}";
            }
        }
        out << "]}";
    }
    out << "],\"classes_found\":" << foundCount << ",\"loader_status_file\":\"loader-status.json\"}";
    Save(root, out.str());
    StartEffLoader(library.handle, domain, root);
    StartHotLoader(library.handle, domain, root);
    __android_log_print(ANDROID_LOG_INFO, "EFFAndroid", "metadata probe complete: %zu assemblies, %zu target classes", count, foundCount);
}
}  // namespace

namespace {
jboolean Frame(JNIEnv*,jclass){AdvanceEffFrame();AdvanceHotFrame();return JNI_FALSE;}
std::string Utf(JNIEnv* env,jstring value){if(!value)return {};const char* chars=env->GetStringUTFChars(value,nullptr);if(!chars)return {};std::string text(chars);env->ReleaseStringUTFChars(value,chars);return text;}
jstring Compile(JNIEnv* env,jclass,jstring mod,jstring game,jstring output){auto error=CompileEff(Utf(env,mod),Utf(env,game),Utf(env,output));return env->NewStringUTF(error.c_str());}
void Request(JNIEnv* env,jclass,jstring output,jlong sequence){try{RequestHot(Utf(env,output),sequence);}catch(const std::exception& error){auto cls=env->FindClass("java/lang/IllegalStateException");if(cls)env->ThrowNew(cls,error.what());}}
void Controls(JNIEnv* env,jclass,jstring values){try{RequestShapeValues(Utf(env,values));}catch(const std::exception& error){auto cls=env->FindClass("java/lang/IllegalArgumentException");if(cls)env->ThrowNew(cls,error.what());}}
bool BindFrame(JNIEnv* env) {
    if(env->PushLocalFrame(16)!=JNI_OK)return false;
    bool success=false;
    do {
        jclass threadClass=env->FindClass("java/lang/Thread");if(!threadClass)break;
        auto current=env->GetStaticMethodID(threadClass,"currentThread","()Ljava/lang/Thread;");
        auto context=env->GetMethodID(threadClass,"getContextClassLoader","()Ljava/lang/ClassLoader;");if(!current||!context)break;
        jobject thread=env->CallStaticObjectMethod(threadClass,current);if(!thread||env->ExceptionCheck())break;
        jobject loader=env->CallObjectMethod(thread,context);if(!loader||env->ExceptionCheck())break;
        jclass loaderClass=env->FindClass("java/lang/ClassLoader");if(!loaderClass)break;
        auto load=env->GetMethodID(loaderClass,"loadClass","(Ljava/lang/String;)Ljava/lang/Class;");if(!load)break;
        jstring name=env->NewStringUTF("dev.eff.probe.NativeBridge");
        auto target=static_cast<jclass>(env->CallObjectMethod(loader,load,name));if(!target||env->ExceptionCheck())break;
        JNINativeMethod methods[]={
            {const_cast<char*>("frame"),const_cast<char*>("()Z"),reinterpret_cast<void*>(Frame)},
            {const_cast<char*>("compile"),const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),reinterpret_cast<void*>(Compile)},
            {const_cast<char*>("request"),const_cast<char*>("(Ljava/lang/String;J)V"),reinterpret_cast<void*>(Request)},
            {const_cast<char*>("controls"),const_cast<char*>("(Ljava/lang/String;)V"),reinterpret_cast<void*>(Controls)}};
        success=env->RegisterNatives(target,methods,4)==JNI_OK;
    }while(false);
    if(env->ExceptionCheck())env->ExceptionClear();
    env->PopLocalFrame(nullptr);return success;
}
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    const char* configured = std::getenv("EFF_ANDROID_DIAGNOSTICS");
    if (!configured || !*configured) return JNI_ERR;
    JNIEnv* env=nullptr;
    if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||!BindFrame(env))return JNI_ERR;
    const std::string root(configured);
    try {
        std::thread([root] {
            try { Probe(root); }
            catch (const std::exception& failure) { Save(root, "{\"stage\":\"probe_error\",\"error\":" + Quote(failure.what()) + "}"); }
            catch (...) { Save(root, "{\"stage\":\"probe_error\",\"error\":\"unknown native exception\"}"); }
        }).detach();
    } catch (...) { return JNI_ERR; }
    return JNI_VERSION_1_6;
}

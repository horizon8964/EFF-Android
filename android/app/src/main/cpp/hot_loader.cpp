#include "hot_loader.h"
#include <dobby.h>
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <atomic>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using M=const void*;
struct Api {
 M(*assembly)(void*,const char*);M(*image)(M);void*(*klass)(M,const char*,const char*);void*(*parent)(void*);
 M(*methods)(void*,void**);const char*(*name)(M);uint32_t(*count)(M);M(*param)(M,uint32_t);M(*returns)(M);uint32_t(*flags)(M,uint32_t*);
 char*(*typeName)(M);void(*free)(void*);void*(*invoke)(M,void*,void**,void**);void*(*unbox)(void*);
 void*(*stringNew)(const char*);const uint16_t*(*chars)(void*);int32_t(*length)(void*);
 M(*classType)(void*);void*(*typeObject)(M);uint32_t(*root)(void*,bool);uint32_t(*weak)(void*,bool);void*(*target)(uint32_t);void(*release)(uint32_t);
 void*(*field)(void*,const char*);void(*fieldGet)(void*,void*,void*);void(*staticGet)(void*,void*);void*(*objectClass)(void*);M(*virtualMethod)(void*,M);uint32_t(*fieldFlags)(void*);
}a{};
void* domain=nullptr;std::string diagnostics;
std::atomic<bool> ready{false};
std::atomic<long long> appliedSequence{-1};std::atomic<bool> enabled{false};
void* (*resolveIcall)(const char*)=nullptr;
std::mutex requestMutex,logMutex,modelMutex;
std::string pendingPath;long long pendingSequence=-1,seenSequence=-2;
void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void Log(const std::string& text){std::lock_guard<std::mutex> lock(logMutex);std::ofstream(diagnostics+"/hot-loader.log",std::ios::app)<<text<<'\n';__android_log_print(ANDROID_LOG_INFO,"EFFHot","%s",text.c_str());}
void Status(const std::string& text){std::lock_guard<std::mutex> lock(logMutex);std::ofstream f(diagnostics+"/hot-status.txt.tmp");f<<text;f.close();std::ofstream state(diagnostics+"/hot-status.json.tmp");state<<"{\"build\":\""<<EFF_ANDROID_VERSION<<"\",\"pid\":"<<getpid()<<",\"applied_sequence\":"<<appliedSequence.load()<<",\"enabled\":"<<(enabled.load()?"true":"false")<<"}\n";state.close();std::rename((diagnostics+"/hot-status.json.tmp").c_str(),(diagnostics+"/hot-status.json").c_str());std::rename((diagnostics+"/hot-status.txt.tmp").c_str(),(diagnostics+"/hot-status.txt").c_str());}
std::string Type(M type){char* name=a.typeName(type);std::string s=name?name:"";if(name)a.free(name);return s;}
void* Class(const char* space,const char* name){for(const char* assembly:{"Gameplay.Beyond.dll","Common.Beyond.dll","UnityEngine.CoreModule.dll","UnityEngine.AssetBundleModule.dll","UI.Beyond.dll","UI.Gameplay.Beyond.dll","mscorlib.dll"}){auto loaded=a.assembly(domain,assembly);if(loaded)if(auto cls=a.klass(a.image(loaded),space,name))return cls;}return nullptr;}
M Method(void* cls,const char* name,const char* result,std::initializer_list<const char*> parameters,bool stat=false,bool required=true){
 for(auto type=cls;type;type=a.parent(type)){void* it=nullptr;M found=nullptr;while(auto m=a.methods(type,&it)){
  if(strcmp(a.name(m),name)||a.count(m)!=parameters.size()||Type(a.returns(m))!=result)continue;uint32_t flags=0;if(bool(a.flags(m,&flags)&0x10)!=stat)continue;
  size_t i=0;bool matches=true;for(auto p:parameters)if(Type(a.param(m,i++))!=p)matches=false;if(matches){Check(!found,"ambiguous managed method");found=m;}
 }if(found)return found;}
 if(required)throw std::runtime_error(std::string("热切换接口缺失: ")+name);return nullptr;
}
void* Call(M method,void* self=nullptr,void** args=nullptr){Check(method,"managed method absent");void* exception=nullptr;void* result=a.invoke(method,self,args,&exception);Check(!exception,"游戏资源调用异常: "+std::string(a.name(method)));return result;}
std::string String(void* value){if(!value)return {};std::string s;auto length=a.length(value);Check(length>=0&&length<16384,"invalid managed string");auto chars=a.chars(value);for(int i=0;i<length;++i){auto c=chars[i];if(c<128)s+=char(c);else if(c<2048){s+=char(192|(c>>6));s+=char(128|(c&63));}else if(c>=0xD800&&c<=0xDBFF&&i+1<length&&chars[i+1]>=0xDC00&&chars[i+1]<=0xDFFF){uint32_t v=0x10000+((c-0xD800)<<10)+(chars[++i]-0xDC00);s+=char(240|(v>>18));s+=char(128|((v>>12)&63));s+=char(128|((v>>6)&63));s+=char(128|(v&63));}else{s+=char(224|(c>>12));s+=char(128|((c>>6)&63));s+=char(128|(c&63));}}return s;}
std::string Normal(std::string s){for(auto& c:s){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}return s;}
struct Root {
 uint32_t handle=0;Root(void* object,bool weak=false):handle(object?(weak?a.weak(object,false):a.root(object,false)):0){}
 ~Root(){if(handle)a.release(handle);}void* Get()const{return handle?a.target(handle):nullptr;}
};
using Ref=std::shared_ptr<Root>;
Ref Hold(void* object,bool weak=false){return std::make_shared<Root>(object,weak);}
bool Bool(M method,void* self=nullptr){auto result=Call(method,self);return result&&*static_cast<uint8_t*>(a.unbox(result));}
M boolObject=nullptr,instanceId=nullptr,pathImplicit=nullptr,resourceLoad=nullptr,resourceDone=nullptr,resourceError=nullptr,resourceGet=nullptr,resourceDispose=nullptr,initialized=nullptr,initing=nullptr;
M bundleLoad=nullptr,requestDone=nullptr,requestBundle=nullptr,assetLoad=nullptr,assetResult=nullptr,bundleUnload=nullptr;
M modelPath=nullptr,modelRoot=nullptr,modelRemove=nullptr,modelLoad=nullptr,modelEntity=nullptr,entityReleased=nullptr;
M animatorCom=nullptr,animationData=nullptr,animationChange=nullptr,animationIdle=nullptr,animationDash=nullptr;
M weaponCom=nullptr,weaponUnload=nullptr,weaponMounts=nullptr,weaponReload=nullptr;
M performExit=nullptr,entityTryComponent=nullptr,audioInvalidate=nullptr,followEffects=nullptr;Ref audioType;
M gameInstance=nullptr,poolDiscard=nullptr;void* managerField=nullptr;bool discardBoolean=false,managerStatic=false;
Ref objectType,prefabType;uint64_t category=0,priority=0;
int Id(void* object){if(!object)return 0;auto result=Call(instanceId,object);return result?*static_cast<int*>(a.unbox(result)):0;}
bool Alive(void* object){if(!object)return false;void* args[]={object};auto b=Call(boolObject,nullptr,args);return b&&*static_cast<uint8_t*>(a.unbox(b));}
void* Field(void* object,const char* name){for(auto cls=a.objectClass(object);cls;cls=a.parent(cls))if(auto field=a.field(cls,name)){void* value=nullptr;if(a.fieldFlags(field)&0x10)a.staticGet(field,&value);else a.fieldGet(object,field,&value);return value;}return nullptr;}
struct Lease{Ref handle,source;std::string path;bool done=false;};
Lease Queue(const std::string& path,void* type){auto str=a.stringNew(path.c_str());void* args[]={str};auto hash=Call(pathImplicit,nullptr,args);Check(hash,"path hash missing");void* load[]={a.unbox(hash),type,&category,&priority};auto h=Call(resourceLoad,nullptr,load);Check(h,"resource lease absent");return {Hold(h),{},path,false};}
bool Poll(Lease& lease){if(lease.done)return true;auto self=a.unbox(lease.handle->Get());if(!Bool(resourceDone,self))return false;Check(!Bool(resourceError,self),"原始资源加载失败: "+lease.path);lease.source=Hold(Call(resourceGet,self));Check(lease.source->Get(),"原始资源为空: "+lease.path);lease.done=true;return true;}
struct Input{std::string path,hash;std::vector<size_t> deps;Ref request,bundle;};
struct Asset{std::string path;size_t input=0;Lease source;Ref request,prefab;};
struct Control{double minimum=0,maximum=1,value=0;};
struct ShapeSpec{std::string marker,channel,key;int vertices=0,channels=0;};
struct Generation{std::map<std::string,Control> controls;std::vector<ShapeSpec> shapes;std::string directory;std::vector<Input> inputs;std::set<size_t> closure;std::vector<Asset> assets;std::vector<Lease> donors;std::vector<std::string> donorPaths;std::map<void*,Ref> replacements;std::set<std::string> paths;int phase=0;long long sequence=0;};
std::shared_ptr<Generation> active,loading;
std::map<std::string,std::shared_ptr<Generation>> retained;
std::shared_ptr<const std::map<void*,Ref>> routes=std::make_shared<const std::map<void*,Ref>>();
thread_local bool pristine=false;
using GetFn=void*(*)(void*,M);GetFn originalGet=nullptr;
std::atomic<unsigned> routedCalls{0};
void* Replacement(void* source){if(!source||pristine)return source;auto current=std::atomic_load(&routes);auto found=current->find(source);if(found==current->end())return source;auto count=++routedCalls;if(count<=20)Log("ROUTED prefab="+std::to_string(Id(source))+" count="+std::to_string(count));return found->second->Get();}
void* GetHook(void* self,M info){return Replacement(originalGet(self,info));}
using CloneFn=void*(*)(void*);using CloneParentFn=void*(*)(void*,void*,bool);
using InstFn=void*(*)(void*,void*,void*);using InstParentFn=void*(*)(void*,void*,void*,void*);
using InstImmediateFn=void*(*)(void*,void*,bool);using InstAsyncFn=void*(*)(void*,int,void*,void*,void*);
CloneFn originalClone=nullptr;CloneParentFn originalCloneParent=nullptr;InstFn originalInst=nullptr;InstParentFn originalInstParent=nullptr;InstImmediateFn originalImmediate=nullptr;InstAsyncFn originalInstAsync=nullptr;
void* Clone(void* source){return originalClone(Replacement(source));}
void* CloneParent(void* source,void* parent,bool world){return originalCloneParent(Replacement(source),parent,world);}
void* Instantiate(void* source,void* position,void* rotation){return originalInst(Replacement(source),position,rotation);}
void* InstantiateParent(void* source,void* parent,void* position,void* rotation){return originalInstParent(Replacement(source),parent,position,rotation);}
void* Immediate(void* source,void* parent,bool world){return originalImmediate(Replacement(source),parent,world);}
void* InstantiateAsync(void* source,int count,void* parent,void* position,void* rotation){return originalInstAsync(Replacement(source),count,parent,position,rotation);}
std::map<void*,Ref> models;
using AddFn=void(*)(void*,void*,M);AddFn originalAdd=nullptr;
void AddPart(void* self,void* part,M info){originalAdd(self,part,info);std::lock_guard<std::mutex> lock(modelMutex);models[self]=Hold(self,true);}
std::vector<std::string> Split(const std::string& line){std::istringstream in(line);std::string part;std::vector<std::string> out;while(std::getline(in,part,'\t'))out.push_back(part);return out;}
std::shared_ptr<Generation> ReadGraph(const std::string& directory,long long sequence){
 namespace fs=std::filesystem;auto g=std::make_shared<Generation>();g->directory=directory;g->sequence=sequence;std::ifstream in(directory+"/hot.tsv");std::string line;Check(bool(std::getline(in,line))&&line=="EFF_ANDROID_HOT\t1","热切换资源索引无效");
 while(std::getline(in,line)){auto c=Split(line);if(c.empty())continue;
  if(c[0]=="input"){Check(c.size()>=4&&std::stoull(c[1])==g->inputs.size(),"resource input index differs");auto root=fs::canonical(directory);auto path=fs::weakly_canonical(root/c[2]);auto relative=path.lexically_relative(root);Check(!relative.empty()&&*relative.begin()!=".."&&!relative.is_absolute(),"resource path escapes generation");Input item;item.path=path.string();item.hash=c[3];if(c.size()>4){std::istringstream d(c[4]);std::string s;while(std::getline(d,s,','))if(!s.empty())item.deps.push_back(std::stoull(s));}g->inputs.push_back(std::move(item));}
  else if(c[0]=="closure"){Check(c.size()==2,"closure row invalid");g->closure.insert(std::stoull(c[1]));}
  else if(c[0]=="root"){Check(c.size()==3,"root row invalid");Asset asset;asset.path=c[1];asset.input=std::stoull(c[2]);g->assets.push_back(std::move(asset));g->paths.insert(Normal(c[1]));}
  else if(c[0]=="donor"){Check(c.size()==2,"donor row invalid");g->donorPaths.push_back(c[1]);}
  else if(c[0]=="control"){Check(c.size()==5,"control row invalid");Control value{std::stod(c[2]),std::stod(c[3]),std::stod(c[4])};Check(std::isfinite(value.minimum)&&std::isfinite(value.maximum)&&std::isfinite(value.value)&&value.minimum<value.maximum&&value.value>=value.minimum&&value.value<=value.maximum,"control bounds invalid");Check(g->controls.emplace(c[1],value).second,"duplicate control key");}
  else if(c[0]=="shape"){Check(c.size()==6,"shape row invalid");g->shapes.push_back({c[1],c[2],c[3],std::stoi(c[4]),std::stoi(c[5])});}
  else throw std::runtime_error("unknown graph row");
 }
 for(const auto& shape:g->shapes)Check(g->controls.count(shape.key)&&shape.vertices>0&&shape.channels>0,"shape references invalid control");
 Check(!g->assets.empty()&&!g->closure.empty(),"empty reload graph");for(auto i:g->closure)Check(i<g->inputs.size()&&fs::is_regular_file(g->inputs[i].path),"compiled bundle absent");for(auto& asset:g->assets)Check(g->closure.count(asset.input),"prefab root outside isolated closure");return g;
}
struct Owner{Ref model,root,entity,animation,data,weapons,perform,audio;std::string path;};
std::vector<Owner> Owners(const std::set<std::string>& paths){
 std::vector<Ref> snapshot;{std::lock_guard<std::mutex> lock(modelMutex);for(auto it=models.begin();it!=models.end();){if(!it->second->Get())it=models.erase(it);else{snapshot.push_back(it->second);++it;}}}
 std::vector<Owner> out;
 for(auto& ref:snapshot){auto model=ref->Get();auto path=String(Call(modelPath,model));if(!paths.count(Normal(path)))continue;auto root=Call(modelRoot,model);if(!Alive(root))continue;auto entity=Call(modelEntity,model);if(!entity||Bool(entityReleased,entity))continue;
  Owner o{Hold(model),Hold(root),Hold(entity),{},{},{},{},{},path};auto animation=Call(animatorCom,entity);if(animation){o.animation=Hold(animation);o.data=Hold(Call(animationData,animation));Check(o.data->Get(),"动画配置不可用");}auto weapon=Call(weaponCom,entity);if(weapon)o.weapons=Hold(weapon);auto instance=Call(gameInstance);auto world=instance?Field(instance,"m_world"):nullptr;auto perform=world?Field(world,"charInteractPerformManager"):nullptr;if(perform)o.perform=Hold(perform);
  if(entityTryComponent&&audioType){void* audio=nullptr;uint8_t quiet=0;void* args[]={audioType->Get(),&audio,&quiet};auto found=Call(entityTryComponent,entity,args);if(found&&*static_cast<uint8_t*>(a.unbox(found))&&audio)o.audio=Hold(audio);}
  out.push_back(std::move(o));
 }
 return out;
}
void Rebuild(std::vector<Owner>& owners){
 auto instance=Call(gameInstance);Check(instance,"游戏世界未就绪");void* manager=nullptr;if(managerStatic)a.staticGet(managerField,&manager);else a.fieldGet(instance,managerField,&manager);Check(manager,"模型缓存管理器不可用");
 for(auto& o:owners){
  if(o.perform){void* args[]={o.entity->Get(),nullptr};Call(performExit,o.perform->Get(),args);}
  if(o.weapons){uint8_t all=1;void* args[]={&all};Call(weaponUnload,o.weapons->Get(),args);}
  int oldId=Id(o.root->Get());
  Call(modelRemove,o.model->Get());
  auto text=a.stringNew(o.path.c_str());void* toHash[]={text};auto hash=Call(pathImplicit,nullptr,toHash);uint8_t immediate=0;void* args[]={a.unbox(hash),&immediate};Call(poolDiscard,manager,args);
  Call(modelLoad,o.model->Get());auto root=Call(modelRoot,o.model->Get());Log("REBUILD loaded root="+std::to_string(Id(root))+" old="+std::to_string(oldId));Check(Alive(root)&&Id(root)!=oldId,"角色模型未成功刷新");
  if(o.animation){uint8_t resume=1;void* args2[]={o.data->Get(),&resume};auto result=Call(animationChange,o.animation->Get(),args2);Check(result&&*static_cast<uint8_t*>(a.unbox(result)),"动画重新绑定失败");
   if(animationIdle&&animationDash){auto cls=a.objectClass(o.animation->Get());bool character=false;for(auto type=cls;type;type=a.parent(type))if(type==Class("Beyond.Gameplay.View","CharacterAnimationComponent"))character=true;if(character){Call(animationIdle,o.animation->Get());Call(animationDash,o.animation->Get());}}
  }
  if(o.audio)Call(audioInvalidate,o.audio->Get());
  if(o.weapons){Call(weaponMounts,o.weapons->Get());uint8_t all=1;void* args3[]={&all};Call(weaponReload,o.weapons->Get(),args3);}
  auto effects=Field(instance,"effectManager");if(effects){void* args[]={o.entity->Get()};Call(followEffects,effects,args);}
  Log("REBUILT "+o.path+" old="+std::to_string(oldId)+" new="+std::to_string(Id(root)));
 }
}
void Commit(const std::shared_ptr<Generation>& next){
 std::set<std::string> paths;if(active)paths=active->paths;if(next)paths.insert(next->paths.begin(),next->paths.end());
 auto owners=Owners(paths);auto before=std::atomic_load(&routes);auto after=next?std::make_shared<const std::map<void*,Ref>>(next->replacements):std::make_shared<const std::map<void*,Ref>>();
 std::atomic_store(&routes,after);
 try{if(!owners.empty())Rebuild(owners);}catch(...){std::atomic_store(&routes,before);try{if(!owners.empty())Rebuild(owners);}catch(const std::exception& e){Log(std::string("ROLLBACK FAILED ")+e.what());}throw;}
 active=next;appliedSequence.store(seenSequence);enabled.store(bool(next));if(next)retained[next->directory]=next;
 Log("COMMIT sequence="+std::to_string(seenSequence)+" owners="+std::to_string(owners.size())+" enabled="+(next?"1":"0"));
 Status(next?"Mod 已应用 · 已刷新 "+std::to_string(owners.size())+" 个角色；角色界面重新打开后生效":"已恢复原版 · 已刷新 "+std::to_string(owners.size())+" 个角色");
}
void Step(Generation& g){
 struct Scope{bool old=pristine;Scope(){pristine=true;}~Scope(){pristine=old;}}scope;
 if(g.phase==0){for(auto& asset:g.assets)asset.source=Queue(asset.path,prefabType->Get());for(auto& path:g.donorPaths)g.donors.push_back(Queue(path,objectType->Get()));g.phase=1;Status("正在准备原始角色和材质依赖");return;}
 if(g.phase==1){bool done=true;for(auto& asset:g.assets)done=Poll(asset.source)&&done;for(auto& lease:g.donors)done=Poll(lease)&&done;if(!done)return;g.phase=2;Status("正在加载安卓 Mod 资源");}
 if(g.phase==2){size_t inFlight=0;bool complete=true;for(auto i:g.closure){auto& item=g.inputs[i];if(item.bundle)continue;complete=false;if(item.request){if(Bool(requestDone,item.request->Get())){auto object=Call(requestBundle,item.request->Get());Check(Alive(object),"安卓 Bundle 加载失败: "+item.path);item.bundle=Hold(object);}else ++inFlight;}}
  for(auto i:g.closure){auto& item=g.inputs[i];if(item.bundle||item.request)continue;if(inFlight>=3)break;auto path=a.stringNew(item.path.c_str());uint32_t crc=0;uint64_t offset=0;void* args[]={path,&crc,&offset};auto result=Call(bundleLoad,nullptr,args);Check(result,"Bundle 请求为空");item.request=Hold(result);++inFlight;}
  if(!complete)return;g.phase=3;Status("正在验证角色预制体");}
 if(g.phase==3){bool complete=true;size_t inFlight=0;for(auto& asset:g.assets){if(asset.prefab)continue;complete=false;if(asset.request){if(Bool(requestDone,asset.request->Get())){auto prefab=Call(assetResult,asset.request->Get());Check(Alive(prefab),"Mod 预制体为空: "+asset.path);asset.prefab=Hold(prefab);auto source=asset.source.source->Get();Check(Id(source)!=Id(prefab),"Mod 命名空间未隔离");g.replacements[source]=asset.prefab;}else ++inFlight;}}
  for(auto& asset:g.assets){if(asset.prefab||asset.request)continue;if(inFlight>=3)break;auto name=a.stringNew(asset.path.c_str());void* args[]={name,prefabType->Get()};asset.request=Hold(Call(assetLoad,g.inputs.at(asset.input).bundle->Get(),args));Check(asset.request->Get(),"预制体请求为空");++inFlight;}
  if(!complete)return;g.phase=4;}
}

M findSkinRenderers=nullptr,arrayLength=nullptr,arrayGet=nullptr,skinMesh=nullptr,meshName=nullptr,meshVertices=nullptr,meshShapeCount=nullptr,meshShapeIndex=nullptr,shapeGet=nullptr,shapeSet=nullptr;
Ref skinType;
struct BoundShape{Ref renderer,mesh;std::string key;int index=0;};
std::vector<BoundShape> shapeBindings;
std::shared_ptr<const std::map<std::string,double>> shapeValues=std::make_shared<const std::map<std::string,double>>();
std::atomic<uint64_t> shapeRevision{0};
std::shared_ptr<Generation> shapeGeneration;uint64_t reportedRevision=~uint64_t(0);size_t reportedBindings=SIZE_MAX;
int Int(M method,void* self){auto value=Call(method,self);Check(value,"managed integer absent");return *static_cast<int*>(a.unbox(value));}
void ShapeFrame(){
 if(!active||active->shapes.empty()){shapeBindings.clear();shapeGeneration=active;return;}
 static unsigned tick=0;bool changed=shapeGeneration!=active;
 if(changed){shapeBindings.clear();shapeGeneration=active;}
 if(changed||++tick%60==0){
  void* args[]={skinType->Get()};auto array=Hold(Call(findSkinRenderers,nullptr,args));int count=Int(arrayLength,array->Get());Check(count>=0&&count<100000,"renderer enumeration invalid");
  shapeBindings.clear();
  for(int i=0;i<count;++i){void* index[]={&i};auto renderer=Call(arrayGet,array->Get(),index);if(!Alive(renderer))continue;auto mesh=Call(skinMesh,renderer);if(!Alive(mesh))continue;auto marker=String(Call(meshName,mesh));
   for(const auto& spec:active->shapes){if(spec.marker!=marker)continue;
    Check(Int(meshVertices,mesh)==spec.vertices&&Int(meshShapeCount,mesh)==spec.channels,"形态网格来源或通道数量不符");
    auto channel=a.stringNew(spec.channel.c_str());void* named[]={channel};auto boxed=Call(meshShapeIndex,mesh,named);Check(boxed,"shape index absent");int channelIndex=*static_cast<int*>(a.unbox(boxed));Check(channelIndex>=0&&channelIndex<spec.channels,"形态通道缺失");
    shapeBindings.push_back({Hold(renderer,true),Hold(mesh),spec.key,channelIndex});
   }
  }
 }
 auto values=std::atomic_load(&shapeValues);auto revision=shapeRevision.load();std::map<std::string,double> verified;
 for(auto it=shapeBindings.begin();it!=shapeBindings.end();){auto renderer=it->renderer->Get();auto mesh=it->mesh->Get();
  if(!Alive(renderer)||!Alive(mesh)||Call(skinMesh,renderer)!=mesh){it=shapeBindings.erase(it);continue;}
  auto control=active->controls.at(it->key);auto request=values->find(it->key);double value=std::clamp(request==values->end()?control.value:request->second,control.minimum,control.maximum);float weight=float(value*100.0);int index=it->index;void* get[]={&index};auto old=Call(shapeGet,renderer,get);Check(old,"shape weight absent");
  if(std::abs(*static_cast<float*>(a.unbox(old))-weight)>0.0001f){void* args[]={&index,&weight};Call(shapeSet,renderer,args);}
  auto after=Call(shapeGet,renderer,get);Check(after&&std::abs(*static_cast<float*>(a.unbox(after))-weight)<0.0001f,"形态键权重回读失败");verified[it->key]=value;++it;
 }
 if(reportedRevision!=revision||reportedBindings!=shapeBindings.size()){
  reportedRevision=revision;reportedBindings=shapeBindings.size();std::ofstream f(diagnostics+"/shape-status.json.tmp");f<<"{\"pid\":"<<getpid()<<",\"revision\":"<<revision<<",\"bindings\":"<<shapeBindings.size()<<",\"values\":{";bool comma=false;for(const auto& pair:verified){if(comma)f<<',';comma=true;f<<'"'<<pair.first<<"\":"<<pair.second;}f<<"}}\n";f.close();std::rename((diagnostics+"/shape-status.json.tmp").c_str(),(diagnostics+"/shape-status.json").c_str());Log("SHAPES bindings="+std::to_string(shapeBindings.size())+" controls="+std::to_string(verified.size()));
 }
}
std::set<void*> hooked;
void Hook(M method,void* detour,void** original,bool required=true){if(!method){Check(!required,"required hook absent");return;}void* address=nullptr;memcpy(&address,method,sizeof(address));if(hooked.count(address)){Check(!required,"duplicate hook entry");return;}Check(DobbyHook(address,detour,original)==0,"热切换 Hook 安装失败");hooked.insert(address);}
}

void RequestHot(const std::string& output,long long sequence){std::lock_guard<std::mutex> lock(requestMutex);pendingPath=output;pendingSequence=sequence;}
void RequestShapeValues(const std::string& text){
 auto next=std::make_shared<std::map<std::string,double>>();std::istringstream rows(text);std::string line;
 while(std::getline(rows,line)){auto fields=Split(line);Check(fields.size()==2&&fields[0].size()<256,"invalid shape request");size_t end=0;double value=std::stod(fields[1],&end);Check(end==fields[1].size()&&std::isfinite(value),"nonfinite shape value");Check(next->emplace(fields[0],value).second,"duplicate shape value");Check(next->size()<=4096,"too many shape values");}
 std::atomic_store(&shapeValues,std::shared_ptr<const std::map<std::string,double>>(next));++shapeRevision;
}
void StartHotLoader(void* library,void* currentDomain,const std::string& root){diagnostics=root;domain=currentDomain;Log("BOOT pid="+std::to_string(getpid())+" build="+EFF_ANDROID_VERSION);try{
 resolveIcall=reinterpret_cast<decltype(resolveIcall)>(dlsym(library,"il2cpp_resolve_icall"));Check(resolveIcall,"il2cpp_resolve_icall absent");
#define API(field,symbol) a.field=reinterpret_cast<decltype(a.field)>(dlsym(library,"il2cpp_" symbol));Check(a.field,"missing il2cpp_" symbol)
 API(assembly,"domain_assembly_open");API(image,"assembly_get_image");API(klass,"class_from_name");API(parent,"class_get_parent");API(methods,"class_get_methods");API(name,"method_get_name");API(count,"method_get_param_count");API(param,"method_get_param");API(returns,"method_get_return_type");API(flags,"method_get_flags");API(typeName,"type_get_name");API(free,"free");API(invoke,"runtime_invoke");API(unbox,"object_unbox");API(stringNew,"string_new");API(chars,"string_chars");API(length,"string_length");API(classType,"class_get_type");API(typeObject,"type_get_object");API(root,"gchandle_new");API(weak,"gchandle_new_weakref");API(target,"gchandle_get_target");API(release,"gchandle_free");API(field,"class_get_field_from_name");API(fieldGet,"field_get_value");API(staticGet,"field_static_get_value");API(objectClass,"object_get_class");API(virtualMethod,"object_get_virtual_method");API(fieldFlags,"field_get_flags");
#undef API
 auto object=Class("UnityEngine","Object"),prefab=Class("UnityEngine","GameObject"),manager=Class("Beyond.Resource","ResourceManager"),handle=Class("Beyond.Resource","FAssetProxyHandle"),path=Class("Beyond.Resource","StringPathHash");
 Check(object&&prefab&&manager&&handle&&path,"resource types absent");objectType=Hold(a.typeObject(a.classType(object)));prefabType=Hold(a.typeObject(a.classType(prefab)));
 boolObject=Method(object,"op_Implicit","System.Boolean",{"UnityEngine.Object"},true);instanceId=Method(object,"GetInstanceID","System.Int32",{});
 pathImplicit=Method(path,"op_Implicit","Beyond.Resource.StringPathHash",{"System.String"},true);
 resourceLoad=Method(manager,"LoadAsync","Beyond.Resource.FAssetProxyHandle",{"Beyond.Resource.StringPathHash","System.Type","Beyond.Resource.RootCategory","Beyond.Resource.EResourceRequestPriority"},true);
 resourceDone=Method(handle,"get_isDone","System.Boolean",{});resourceError=Method(handle,"get_hasError","System.Boolean",{});resourceGet=Method(handle,"Get","UnityEngine.Object",{});resourceDispose=Method(handle,"Dispose","System.Void",{});
 initialized=Method(manager,"IsInitialized","System.Boolean",{},true);initing=Method(manager,"IsAsyncIniting","System.Boolean",{},true);
 auto categoryClass=Class("Beyond.Resource","RootCategory"),priorityClass=Class("Beyond.Resource","EResourceRequestPriority");Check(categoryClass&&priorityClass,"enum classes absent");auto main=a.field(categoryClass,"Main"),normal=a.field(priorityClass,"Default");Check(main&&normal,"enum constants absent");a.staticGet(main,&category);a.staticGet(normal,&priority);
 auto bundle=Class("UnityEngine","AssetBundle"),request=Class("UnityEngine","AssetBundleCreateRequest"),async=Class("UnityEngine","AsyncOperation"),asset=Class("UnityEngine","AssetBundleRequest");
 bundleLoad=Method(bundle,"LoadFromFileAsync","UnityEngine.AssetBundleCreateRequest",{"System.String","System.UInt32","System.UInt64"},true);requestDone=Method(async,"get_isDone","System.Boolean",{});requestBundle=Method(request,"get_assetBundle","UnityEngine.AssetBundle",{});assetLoad=Method(bundle,"LoadAssetAsync","UnityEngine.AssetBundleRequest",{"System.String","System.Type"});assetResult=Method(asset,"get_asset","UnityEngine.Object",{});bundleUnload=Method(bundle,"Unload","System.Void",{"System.Boolean"});
 auto model=Class("Beyond.Gameplay.View","BaseModelComponent"),entity=Class("Beyond.Gameplay.Core","Entity"),component=Class("Beyond.Gameplay.Core","BaseComponent");
 modelPath=Method(model,"get_modelPath","System.String",{});modelRoot=Method(model,"GetModelGo","UnityEngine.GameObject",{});modelRemove=Method(model,"RemoveMainPart","System.Void",{});modelLoad=Method(model,"LoadMainModelSync","System.Void",{});modelEntity=Method(component,"get_entity","Beyond.Gameplay.Core.Entity",{});entityReleased=Method(entity,"get_markReleased","System.Boolean",{});
 auto animation=Class("Beyond.Gameplay.View","ComplexAnimatorComponent");animatorCom=Method(entity,"get_animatorCom","Beyond.Gameplay.View.ComplexAnimatorComponent",{});animationData=Method(animation,"get_data","Beyond.Gameplay.View.ComplexAnimatorComponentData",{});animationChange=Method(animation,"ChangeAnimationConfig","System.Boolean",{"Beyond.Gameplay.View.ComplexAnimatorComponentData","System.Boolean"});
 auto character=Class("Beyond.Gameplay.View","CharacterAnimationComponent");animationIdle=Method(character,"_PreloadSpIdlePerform","System.Void",{},false,false);animationDash=Method(character,"_PreloadSpDashPerform","System.Void",{},false,false);
 auto weapon=Class("Beyond.Gameplay.View","WeaponComponent");weaponCom=Method(entity,"get_weaponCom","Beyond.Gameplay.View.WeaponComponent",{});weaponUnload=Method(weapon,"UnloadWeapons","System.Void",{"System.Boolean"});weaponMounts=Method(weapon,"UpdateAllWeaponMountPoint","System.Void",{});weaponReload=Method(weapon,"ReloadWeapon","System.Void",{"System.Boolean"});
 auto instance=Class("Beyond.Gameplay","GameInstance"),pool=Class("Beyond.Gameplay.View","ModelManager");gameInstance=Method(instance,"get_instance","Beyond.Gameplay.GameInstance",{},true);managerField=a.field(instance,"modelManager");Check(managerField,"modelManager field absent");managerStatic=(a.fieldFlags(managerField)&0x10)!=0;Log(std::string("modelManager static=")+(managerStatic?"1":"0"));poolDiscard=Method(pool,"DiscardPooledObjects","System.Void",{"Beyond.Resource.StringPathHash"},false,false);if(!poolDiscard){poolDiscard=Method(pool,"DiscardPooledObjects","System.Void",{"Beyond.Resource.StringPathHash","System.Boolean"});discardBoolean=true;}
 performExit=Method(Class("Beyond.Gameplay.Core.CharInteractPerform","CharInteractPerformManager"),"ForceExitAllPerformOnEntity","System.Void",{"Beyond.Gameplay.Core.Entity","System.String"});
 entityTryComponent=Method(entity,"TryGetComponent","System.Boolean",{"System.Type","Beyond.Gameplay.Core.BaseComponent&","System.Boolean"});
 auto audio=Class("Beyond.Gameplay.Core","CharacterAudioComponent");Check(audio,"CharacterAudioComponent absent");audioType=Hold(a.typeObject(a.classType(audio)));audioInvalidate=Method(audio,"InvalidateTransformReference","System.Void",{});
 followEffects=Method(Class("Beyond.Gameplay","EffectManager"),"ChangeEntityFollowEffect","System.Void",{"Beyond.Gameplay.Core.Entity"});
 auto skin=Class("UnityEngine","SkinnedMeshRenderer"),mesh=Class("UnityEngine","Mesh");Check(skin&&mesh,"shape types absent");skinType=Hold(a.typeObject(a.classType(skin)));
 findSkinRenderers=Method(Class("UnityEngine","Resources"),"FindObjectsOfTypeAll","UnityEngine.Object[]",{"System.Type"},true);
 auto arrayClass=Class("System","Array");arrayLength=Method(arrayClass,"get_Length","System.Int32",{});arrayGet=Method(arrayClass,"GetValue","System.Object",{"System.Int32"});
 skinMesh=Method(skin,"get_sharedMesh","UnityEngine.Mesh",{});meshName=Method(object,"get_name","System.String",{});meshVertices=Method(mesh,"get_vertexCount","System.Int32",{});meshShapeCount=Method(mesh,"get_blendShapeCount","System.Int32",{});meshShapeIndex=Method(mesh,"GetBlendShapeIndex","System.Int32",{"System.String"});shapeGet=Method(skin,"GetBlendShapeWeight","System.Single",{"System.Int32"});shapeSet=Method(skin,"SetBlendShapeWeight","System.Void",{"System.Int32","System.Single"});
 Hook(resourceGet,reinterpret_cast<void*>(GetHook),reinterpret_cast<void**>(&originalGet));
 auto add=Method(model,"AddPart","System.Void",{"Beyond.Gameplay.View.BaseModelViewPart"});Hook(add,reinterpret_cast<void*>(AddPart),reinterpret_cast<void**>(&originalAdd));
 auto native=[&](const char* name,void* detour,void** original){auto address=resolveIcall(name);Check(address,std::string("Unity 内部入口缺失: ")+name);Check(!hooked.count(address),"duplicate Unity internal hook");Check(DobbyHook(address,detour,original)==0,"Unity internal hook failed");hooked.insert(address);};
 native("UnityEngine.Object::Internal_CloneSingle(UnityEngine.Object)",reinterpret_cast<void*>(Clone),reinterpret_cast<void**>(&originalClone));
 native("UnityEngine.Object::Internal_CloneSingleWithParent(UnityEngine.Object,UnityEngine.Transform,System.Boolean)",reinterpret_cast<void*>(CloneParent),reinterpret_cast<void**>(&originalCloneParent));
 native("UnityEngine.Object::Internal_InstantiateSingle_Injected(UnityEngine.Object,UnityEngine.Vector3&,UnityEngine.Quaternion&)",reinterpret_cast<void*>(Instantiate),reinterpret_cast<void**>(&originalInst));
 native("UnityEngine.Object::Internal_InstantiateSingleWithParent_Injected(UnityEngine.Object,UnityEngine.Transform,UnityEngine.Vector3&,UnityEngine.Quaternion&)",reinterpret_cast<void*>(InstantiateParent),reinterpret_cast<void**>(&originalInstParent));
 native("UnityEngine.Object::Internal_InstantiateAsyncImmediatelyWithParent(UnityEngine.Object,UnityEngine.Transform,System.Boolean)",reinterpret_cast<void*>(Immediate),reinterpret_cast<void**>(&originalImmediate));
 native("UnityEngine.Object::Internal_InstantiateAsyncWithParent(UnityEngine.Object,System.Int32,UnityEngine.Transform,System.IntPtr,System.IntPtr)",reinterpret_cast<void*>(InstantiateAsync),reinterpret_cast<void**>(&originalInstAsync));
 ready.store(true,std::memory_order_release);Log("READY hooks="+std::to_string(hooked.size()));Status("游戏已连接，等待选择 Mod");
 }catch(const std::exception& e){for(void* address:hooked)DobbyDestroy(address);hooked.clear();Log(std::string("REJECTED ")+e.what());Status(std::string("热切换未启用：")+e.what());}}
void AdvanceHotFrame(){
 if(!ready.load(std::memory_order_acquire))return;
 try{ShapeFrame();}catch(const std::exception& e){static std::string last;std::string message=e.what();if(last!=message){last=message;Log("SHAPE FAILED "+message);Status("形态键设置失败："+message);}}
 static unsigned frames=0;if(++frames%3)return;
 try{
  if(!Bool(initialized)||Bool(initing))return;
  std::string path;long long sequence;{std::lock_guard<std::mutex> lock(requestMutex);path=pendingPath;sequence=pendingSequence;}
  if(sequence>=0&&sequence!=seenSequence&&!loading){
   if(!path.empty()){auto instance=Call(gameInstance);if(!instance||!Field(instance,"m_world")){static long long waiting=-1;if(waiting!=sequence){waiting=sequence;Status("等待进入大世界后加载 Mod");}return;}}
   // Drain an already submitted generation before processing a newer request.
   seenSequence=sequence;
   if(path.empty()){Commit({});return;}
   auto cached=retained.find(path);if(cached!=retained.end()){Commit(cached->second);return;}
   loading=ReadGraph(path,sequence);Log("BEGIN sequence="+std::to_string(sequence)+" roots="+std::to_string(loading->assets.size()));
  }
  if(loading){Step(*loading);if(loading->phase==4){auto next=loading;loading.reset();long long latest;{std::lock_guard<std::mutex> lock(requestMutex);latest=pendingSequence;}if(latest==next->sequence)Commit(next);else{retained[next->directory]=next;Log("SUPERSEDED prepared generation retained");}}}
 }catch(const std::exception& e){Log(std::string("FAILED ")+e.what());Status(std::string("切换失败：")+e.what());if(loading){retained[loading->directory+"-failed"]=loading;loading.reset();}}
}

// Included by hot_loader.cpp inside its anonymous namespace. Visual components
// only: never replace scene GameObjects, scripts, colliders or factory state.
M staticFind=nullptr,staticChildren=nullptr,staticFilterMesh=nullptr,staticFilterSet=nullptr;
M staticGameObject=nullptr,staticComponent=nullptr,staticTransform=nullptr,staticGoTransform=nullptr;
M staticParent=nullptr,staticSibling=nullptr,staticChild=nullptr,staticChildCount=nullptr;
M staticMaterials=nullptr,staticMaterialsSet=nullptr,staticEnabled=nullptr,staticEnabledSet=nullptr;
M staticScene=nullptr,staticSceneValid=nullptr,staticSceneLoaded=nullptr;
Ref staticFilterType,staticRendererType;
bool staticAvailable=false;
struct StaticRule{Ref sourceMesh,sourceMaterials,mesh,materials;bool enabled=true;};
struct StaticBinding{Ref filter,renderer,mesh,materials,appliedMesh,appliedMaterials;bool enabled=true,appliedEnabled=true;};
std::map<std::string,StaticRule> staticRules;
std::map<int,StaticBinding> staticBindings;

void StaticInit(){try{
 auto filter=Class("UnityEngine","MeshFilter"),renderer=Class("UnityEngine","MeshRenderer");
 auto component=Class("UnityEngine","Component"),go=Class("UnityEngine","GameObject"),transform=Class("UnityEngine","Transform");
 auto resources=Class("UnityEngine","Resources"),scene=Class("UnityEngine.SceneManagement","Scene");
 Check(filter&&renderer&&component&&go&&transform&&resources&&scene,"static visual types absent");
 staticFilterType=Hold(a.typeObject(a.classType(filter)));staticRendererType=Hold(a.typeObject(a.classType(renderer)));
 staticFind=Method(resources,"FindObjectsOfTypeAll","UnityEngine.Object[]",{"System.Type"},true);
 staticChildren=Method(go,"GetComponentsInChildren","UnityEngine.Component[]",{"System.Type","System.Boolean"});
 staticFilterMesh=Method(filter,"get_sharedMesh","UnityEngine.Mesh",{});
 staticFilterSet=Method(filter,"set_sharedMesh","System.Void",{"UnityEngine.Mesh"});
 staticGameObject=Method(component,"get_gameObject","UnityEngine.GameObject",{});
 staticComponent=Method(component,"GetComponent","UnityEngine.Component",{"System.Type"});
 staticTransform=Method(component,"get_transform","UnityEngine.Transform",{});
 staticGoTransform=Method(go,"get_transform","UnityEngine.Transform",{});
 staticParent=Method(transform,"get_parent","UnityEngine.Transform",{});
 staticSibling=Method(transform,"GetSiblingIndex","System.Int32",{});
 staticChild=Method(transform,"GetChild","UnityEngine.Transform",{"System.Int32"});
 staticChildCount=Method(transform,"get_childCount","System.Int32",{});
 staticMaterials=Method(renderer,"get_sharedMaterials","UnityEngine.Material[]",{});
 staticMaterialsSet=Method(renderer,"set_sharedMaterials","System.Void",{"UnityEngine.Material[]"});
 staticEnabled=Method(renderer,"get_enabled","System.Boolean",{});
 staticEnabledSet=Method(renderer,"set_enabled","System.Void",{"System.Boolean"});
 staticScene=Method(go,"get_scene","UnityEngine.SceneManagement.Scene",{});
 staticSceneValid=Method(scene,"IsValid","System.Boolean",{});
 staticSceneLoaded=Method(scene,"get_isLoaded","System.Boolean",{});
 staticAvailable=true;Log("STATIC interfaces ready");
 }catch(const std::exception& e){staticAvailable=false;Log(std::string("STATIC unavailable: ")+e.what());}
}
void* StaticAt(void* array,int index){void* args[]={&index};return Call(arrayGet,array,args);}
std::string StaticKey(void* mesh,void* materials){
 if(!Alive(mesh)||!materials)return {};
 int count=Int(arrayLength,materials);Check(count>=0&&count<=256,"static material array exceeds bounds");
 std::string key=std::to_string(Id(mesh))+":";
 for(int i=0;i<count;++i){auto material=StaticAt(materials,i);key+=std::to_string(Alive(material)?Id(material):0)+",";}
 return key;
}
bool StaticMaterialsEqual(void* first,void* second){
 if(!first||!second)return first==second;
 int count=Int(arrayLength,first);if(count!=Int(arrayLength,second))return false;
 Check(count>=0&&count<=256,"static materials exceed bounds");
 for(int i=0;i<count;++i)if(StaticAt(first,i)!=StaticAt(second,i))return false;
 return true;
}
void StaticRestore(){
 for(auto& item:staticBindings){auto& b=item.second;auto filter=b.filter->Get(),renderer=b.renderer->Get();
  if(!Alive(filter)||!Alive(renderer))continue;
  // A game subsystem may have changed the visual since our last write.
  if(Call(staticFilterMesh,filter)==b.appliedMesh->Get()){void* args[]={b.mesh->Get()};Call(staticFilterSet,filter,args);}
  if(StaticMaterialsEqual(Call(staticMaterials,renderer),b.appliedMaterials->Get())){void* args[]={b.materials->Get()};Call(staticMaterialsSet,renderer,args);}
  if(Bool(staticEnabled,renderer)==b.appliedEnabled){uint8_t enabled=b.enabled;void* args[]={&enabled};Call(staticEnabledSet,renderer,args);}
 }
 staticBindings.clear();
}
bool StaticSceneObject(void* filter){
 auto go=Call(staticGameObject,filter);if(!Alive(go))return false;
 auto boxed=Call(staticScene,go);return boxed&&Bool(staticSceneValid,a.unbox(boxed))&&Bool(staticSceneLoaded,a.unbox(boxed));
}
void* StaticPeer(void* originalFilter,void* sourceRoot,void* candidateRoot){
 auto top=Call(staticGoTransform,sourceRoot),node=Call(staticTransform,originalFilter);
 std::vector<std::pair<int,std::string>> steps;
 while(node&&node!=top){Check(steps.size()<256,"static prefab hierarchy too deep");steps.push_back({Int(staticSibling,node),String(Call(meshName,node))});node=Call(staticParent,node);}
 Check(node==top,"static source component outside prefab root");
 node=Call(staticGoTransform,candidateRoot);
 for(auto it=steps.rbegin();it!=steps.rend();++it){int index=it->first;if(index<0||index>=Int(staticChildCount,node))return nullptr;void* args[]={&index};node=Call(staticChild,node,args);if(String(Call(meshName,node))!=it->second)return nullptr;}
 void* args[]={staticFilterType->Get()};return Call(staticComponent,node,args);
}
std::map<std::string,StaticRule> StaticBuild(const std::shared_ptr<Generation>& generation){
 std::map<std::string,StaticRule> rules;std::set<std::string> ambiguous;
 if(!generation||generation->staticNames.empty()||!staticAvailable)return rules;
 for(const auto& asset:generation->assets){auto path=Normal(asset.path);
  if(path.find("/gameplay/interactives/")==std::string::npos&&path.find("/gameplay/factory/")==std::string::npos&&path.find("/gameplay/spaceship/")==std::string::npos)continue;
  uint8_t inactive=1;void* args[]={staticFilterType->Get(),&inactive};auto array=Hold(Call(staticChildren,asset.source.source->Get(),args));
  int count=Int(arrayLength,array->Get());Check(count>=0&&count<=20000,"too many static source filters");
  for(int i=0;i<count;++i){auto filter=StaticAt(array->Get(),i);auto sourceMesh=Call(staticFilterMesh,filter);if(!Alive(sourceMesh))continue;
   auto eligible=generation->staticNames.find(String(Call(meshName,sourceMesh)));if(eligible==generation->staticNames.end()||!eligible->second)continue;
   auto peer=StaticPeer(filter,asset.source.source->Get(),asset.prefab->Get());Check(Alive(peer),"static candidate component missing");
   void* typed[]={staticRendererType->Get()};auto renderer=Call(staticComponent,filter,typed),candidateRenderer=Call(staticComponent,peer,typed);Check(Alive(renderer)&&Alive(candidateRenderer),"static renderer pair missing");
   auto sourceMats=Hold(Call(staticMaterials,renderer)),newMats=Hold(Call(staticMaterials,candidateRenderer));auto newMesh=Call(staticFilterMesh,peer);
   Check(Alive(newMesh),"static candidate mesh missing");
   auto key=StaticKey(sourceMesh,sourceMats->Get());StaticRule rule{Hold(sourceMesh),sourceMats,Hold(newMesh),newMats,Bool(staticEnabled,candidateRenderer)};
   auto old=rules.find(key);if(old!=rules.end()&&(old->second.mesh->Get()!=newMesh||!StaticMaterialsEqual(old->second.materials->Get(),newMats->Get())||old->second.enabled!=rule.enabled))ambiguous.insert(key);
   else rules.emplace(key,std::move(rule));
  }
 }
 for(const auto& key:ambiguous)rules.erase(key);
 Log("STATIC rules="+std::to_string(rules.size())+" ambiguous_skipped="+std::to_string(ambiguous.size()));return rules;
}
size_t StaticApply(){
 if(staticRules.empty()||!staticAvailable)return 0;
 void* typed[]={staticFilterType->Get()};auto array=Hold(Call(staticFind,nullptr,typed));int count=Int(arrayLength,array->Get());Check(count>=0&&count<=250000,"too many loaded static filters");
 for(auto it=staticBindings.begin();it!=staticBindings.end();)if(!Alive(it->second.filter->Get()))it=staticBindings.erase(it);else ++it;
 for(int i=0;i<count;++i){auto filter=StaticAt(array->Get(),i);if(!Alive(filter)||staticBindings.count(Id(filter))||!StaticSceneObject(filter))continue;
  auto mesh=Call(staticFilterMesh,filter);if(!Alive(mesh))continue;
  void* rt[]={staticRendererType->Get()};auto renderer=Call(staticComponent,filter,rt);if(!Alive(renderer))continue;
  auto materials=Hold(Call(staticMaterials,renderer));auto rule=staticRules.find(StaticKey(mesh,materials->Get()));if(rule==staticRules.end())continue;
  const auto& r=rule->second;bool originalEnabled=Bool(staticEnabled,renderer),newEnabled=originalEnabled&&r.enabled;
  staticBindings.emplace(Id(filter),StaticBinding{Hold(filter,true),Hold(renderer,true),Hold(mesh),materials,r.mesh,r.materials,originalEnabled,newEnabled});
  void* m[]={r.mesh->Get()};Call(staticFilterSet,filter,m);void* mat[]={r.materials->Get()};Call(staticMaterialsSet,renderer,mat);
  uint8_t enabled=newEnabled;void* en[]={&enabled};Call(staticEnabledSet,renderer,en);
  Check(Call(staticFilterMesh,filter)==r.mesh->Get()&&StaticMaterialsEqual(Call(staticMaterials,renderer),r.materials->Get()),"static visual readback differs");
 }
 return staticBindings.size();
}
void StaticReport(){
 std::ofstream f(diagnostics+"/static-status.json.tmp");f<<"{\"pid\":"<<getpid()<<",\"interfaces_ready\":"<<(staticAvailable?"true":"false")<<",\"rules\":"<<staticRules.size()<<",\"bindings\":"<<staticBindings.size()<<"}\n";f.close();
 std::rename((diagnostics+"/static-status.json.tmp").c_str(),(diagnostics+"/static-status.json").c_str());
}
size_t StaticCommit(const std::shared_ptr<Generation>& next){
 if(!staticAvailable){StaticReport();return 0;}
 auto rules=StaticBuild(next);StaticRestore();staticRules=std::move(rules);
 try{auto count=StaticApply();StaticReport();Log("STATIC commit bindings="+std::to_string(count));return count;}
 catch(...){StaticRestore();staticRules.clear();StaticReport();throw;}
}
void StaticFrame(){
 static unsigned frame=0;if(++frame%120||staticRules.empty())return;
 auto previous=staticBindings.size();
 try{StaticApply();if(previous!=staticBindings.size()){StaticReport();Log("STATIC live bindings="+std::to_string(staticBindings.size()));}}
 catch(...){StaticRestore();staticRules.clear();StaticReport();throw;}
}

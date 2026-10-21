// Visual gating is independent of Renderer.enabled, so native LOD/pooling
// retains ownership of that flag. Restore each original forceRenderingOff.
M forceGet=nullptr,forceSet=nullptr;
void VisibilityInit(){auto renderer=Class("UnityEngine","Renderer");forceGet=Method(renderer,"get_forceRenderingOff","System.Boolean",{});forceSet=Method(renderer,"set_forceRenderingOff","System.Void",{"System.Boolean"});}
struct BoundVisibility{Ref renderer,mesh;size_t spec=0;bool original=false,applied=false;};
std::vector<BoundVisibility> visibilityBindings;
std::shared_ptr<Generation> visibilityGeneration;
void VisibilityWrite(void* renderer,bool value){uint8_t flag=value;void* args[]={&flag};Call(forceSet,renderer,args);Check(Bool(forceGet,renderer)==value,"外观切换状态回读失败");}
void VisibilityRestore(){
 for(const auto& bound:visibilityBindings){auto renderer=bound.renderer->Get();if(Alive(renderer)&&Call(skinMesh,renderer)==bound.mesh->Get()&&Bool(forceGet,renderer)==bound.applied)VisibilityWrite(renderer,bound.original);}
 visibilityBindings.clear();
}
void VisibilityFrame(){
 const bool changed=visibilityGeneration!=active;
 if(changed){VisibilityRestore();visibilityGeneration=active;}
 if(!active||active->visibility.empty())return;
 static unsigned tick=0;
 if(changed||++tick%30==0){
  visibilityBindings.erase(std::remove_if(visibilityBindings.begin(),visibilityBindings.end(),[](const auto& bound){auto renderer=bound.renderer->Get();return !Alive(renderer)||Call(skinMesh,renderer)!=bound.mesh->Get();}),visibilityBindings.end());
  std::set<void*> bound;for(const auto& item:visibilityBindings)bound.insert(item.renderer->Get());
  void* args[]={skinType->Get()};auto array=Hold(Call(findSkinRenderers,nullptr,args));int count=Int(arrayLength,array->Get());Check(count>=0&&count<100000,"visibility renderer enumeration invalid");
  for(int i=0;i<count;++i){void* index[]={&i};auto renderer=Call(arrayGet,array->Get(),index);if(!Alive(renderer)||bound.count(renderer)||!staticAvailable||!StaticSceneObject(renderer))continue;
   auto mesh=Call(skinMesh,renderer);if(!Alive(mesh))continue;auto marker=String(Call(meshName,mesh));
   for(size_t j=0;j<active->visibility.size();++j){const auto& spec=active->visibility[j];if(spec.marker!=marker)continue;Check(Int(meshVertices,mesh)==spec.vertices,"外观切换网格来源不符");bool original=Bool(forceGet,renderer);visibilityBindings.push_back({Hold(renderer,true),Hold(mesh),j,original,original});break;}
  }
 }
 auto values=std::atomic_load(&shapeValues);auto revision=shapeRevision.load();auto variables=active->visibilityDefaults;
 for(const auto& row:active->visibilityKeys){auto requested=values->find(row.second);if(requested==values->end())continue;auto control=active->controls.find(row.second);variables.at(row.first)=control==active->controls.end()?requested->second:std::clamp(requested->second,control->second.minimum,control->second.maximum);}
 std::vector<bool> visible;for(const auto& spec:active->visibility)visible.push_back(bool(spec.expression->Evaluate(variables)));
 size_t shown=0,hidden=0;
 for(auto& item:visibilityBindings){auto renderer=item.renderer->Get();if(!Alive(renderer))continue;bool current=Bool(forceGet,renderer);if(current!=item.applied)item.original=current;
  bool next=item.original||!visible.at(item.spec);if(next!=current)VisibilityWrite(renderer,next);item.applied=next;if(next)++hidden;else ++shown;
 }
 static uint64_t reported=~uint64_t(0);static size_t bindings=SIZE_MAX;
 if(changed||reported!=revision||bindings!=visibilityBindings.size()){
  reported=revision;bindings=visibilityBindings.size();std::ofstream out(diagnostics+"/visibility-status.json.tmp");out<<"{\"pid\":"<<getpid()<<",\"revision\":"<<revision<<",\"bindings\":"<<bindings<<",\"shown\":"<<shown<<",\"hidden\":"<<hidden<<",\"variables\":{";bool comma=false;
  for(const auto& row:variables){if(comma)out<<',';comma=true;out<<'"'<<row.first<<"\":"<<row.second;}out<<"}}\n";out.close();std::rename((diagnostics+"/visibility-status.json.tmp").c_str(),(diagnostics+"/visibility-status.json").c_str());Log("VISIBILITY bindings="+std::to_string(bindings)+" shown="+std::to_string(shown)+" hidden="+std::to_string(hidden));
 }
}

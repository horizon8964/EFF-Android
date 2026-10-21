#include "compiler.h"
#include "eiem_static_resource_sources.h"
#include "eiem_vfs_catalog.h"
#include "visibility_expression.h"
#include <iostream>
namespace {
std::string Json(const std::string& s){std::string out="\"";const char* hex="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
std::string Mobile(std::string text){const std::string a="bundles/windows/";if(text.rfind(a,0)==0)text.replace(0,a.size(),"bundles/android/");return text;}
}
std::string CompileEff(const std::string& mod,const std::string& game,const std::string& output){try{
 using namespace EiemDirectory;
 namespace fs=std::filesystem;
 const fs::path directory=output,gameRoot=game;
 Require(!fs::exists(directory/"hot.tsv"),"compiled generation already published");
 fs::create_directories(directory);
 auto status=[&](const std::string& text){EiemOfflineCompile::AtomicSave(directory/"progress.txt",Bytes(text.begin(),text.end()));};
 status("读取 EFF 作者资源");
 std::vector<Package> authors;std::istringstream roots(mod);std::string path;
 EiemStaticSources::Selection selected;std::set<std::string> names;
 while(std::getline(roots,path))if(!path.empty()){
  authors.push_back(Load(path));
  auto old=EiemStaticSources::Decode(Read(fs::path(path)/"source-inputs.bin"));
  for(const auto& p:old.assets){auto item=selected.assets.emplace(p.first,Mobile(p.second));Require(item.second||item.first->second==Mobile(p.second),"Mod 资源路径来源冲突: "+p.first);}
  for(const auto& d:old.dependencies){auto& dependencies=selected.dependencies[Mobile(d.first)];for(const auto& p:d.second)dependencies.insert(Mobile(p));}
  for(const auto& p:old.candidates)selected.candidates.insert(Mobile(p));
  for(const auto& p:old.inputs)names.insert(Mobile(p.first));
 }
 Require(!authors.empty(),"未选择 Mod");
 // Compose before touching native inputs: common CABs and prefab lists are
 // reconstructed once, and state variables are qualified by author identity.
 auto package=EiemComposedPackage::Compose(authors,true);
 status("匹配手机 VFS");
 auto current=EiemVfsCatalog::ReadVfs(gameRoot/"VFS",names);
 std::string fingerprint;
 for(const auto& p:EiemVfsCatalog::Files(gameRoot/"VFS",L".blc"))fingerprint+=p.filename().string()+EiemVfsCatalog::HashBytes(Read(p));
 selected.manifest=EiemVfsCatalog::HashBytes(Bytes(fingerprint.begin(),fingerprint.end()));
 std::map<std::string,std::string> missing;
 for(const auto& name:names){auto found=current.find(name);Require(found!=current.end(),"手机 VFS 缺少资源: "+name);auto& e=found->second;
  if(!fs::is_regular_file(e.physical)){
   auto relative=e.physical.lexically_relative(gameRoot/"VFS");auto overlay=gameRoot/"eff-android/vfs-chunks"/relative;
   std::ifstream marker(overlay.string()+".md5");std::string digest;marker>>digest;
   if(fs::is_regular_file(overlay)&&digest==e.chunkContentMd5)e.physical=overlay;
   else missing[relative.generic_string()]=e.chunkContentMd5;
  }
 }
 if(!missing.empty()){std::string required;for(const auto& e:missing)required+=e.first+'\t'+e.second+'\n';EiemOfflineCompile::AtomicSave(directory/"required-chunks.tsv",Bytes(required.begin(),required.end()));return "APK_CHUNKS_REQUIRED";}
 for(const auto& name:names){
  auto found=current.find(name);Require(found!=current.end(),"手机 VFS 缺少资源: "+name);
  const auto& e=found->second;Require(!e.encrypted,"尚未支持加密 VFS 条目: "+name);
  auto stamp=EiemNativeInput::FileStamp(e.physical);
  Require(e.offset<=stamp.bytes&&e.size<=stamp.bytes-e.offset&&e.size<=SIZE_MAX,"手机 VFS 数据尚未下载完整: "+name);
  std::ifstream stream(e.physical,std::ios::binary);Require(bool(stream),"无法读取手机 CHK");stream.seekg(e.offset);Bytes bytes(size_t(e.size));stream.read((char*)bytes.data(),bytes.size());Require(size_t(stream.gcount())==bytes.size(),"VFS 读取不完整");
  selected.inputs.emplace(name,EiemStaticSources::Entry{name,EiemResourceReloadPlan::Hash(bytes),e.physical,e.offset,e.size});
 }
 status("验证网格和预制体依赖");
 EiemStaticSources::Ensure(selected,directory/"baseline",directory/"discovery",package,gameRoot);
 auto stage=[&](const char* name,size_t n,size_t total){status(std::string(name)+" "+std::to_string(n)+"/"+std::to_string(total));};
 auto plan=EiemResourceReloadPlan::Build(package,directory/"baseline",directory/"generation",directory.filename().string(),{},stage,{},{},false,gameRoot);
 // Publish the isolated graph only after all files and dependencies are ready.
 std::ostringstream graph;graph<<"EFF_ANDROID_HOT\t3\n";
 for(size_t i=0;i<plan.inputs.size();++i){const auto& input=plan.inputs[i];
  graph<<"input\t"<<i<<'\t'<<input.path.lexically_relative(directory).generic_string()<<'\t'<<input.hash<<'\t';
  if(i<plan.inputDependencies.size())for(auto d:plan.inputDependencies[i])graph<<d<<',';
  graph<<'\n';
 }
 for(auto index:plan.closure)graph<<"closure\t"<<index<<'\n';
 for(const auto& r:plan.roots)graph<<"root\t"<<r.path<<'\t'<<r.input<<'\n';
 // Existing static visuals can be updated only for simple unskinned rules.
 for(const auto& target:package.targetIndex){const auto& rule=package.rules.at(target.second);bool live=rule.skip;
  if(rule.parts.size()==1){const auto& part=package.parts.at(rule.parts.front());const auto& mesh=package.meshes.at(part.mesh);
   live=mesh.bones.empty()&&mesh.shapes.empty()&&part.shapes.empty()&&!EiemDirectory::RuntimeVisibility(*part.visible);}
  graph<<"static\t"<<target.first.asset<<'\t'<<(live?1:0)<<'\n';
 }
 for(const auto& a:selected.assets)if(a.first.size()<4||a.first.substr(a.first.size()-4)!=".fbx")graph<<"donor\t"<<a.first<<'\n';
 std::map<std::string,std::string> controlKeys;
 std::ostringstream controls;controls<<"{\"controls\":[";bool comma=false;
 for(const auto& author:authors){
  const auto prefix=EiemComposedPackage::Prefix(author.id);
  for(const auto& value:author.defaults)graph<<"variable\t$"<<prefix<<value.first.substr(1)<<'\t'<<author.root.filename().string()<<":"<<value.first<<'\t'<<std::setprecision(17)<<value.second<<'\n';
  std::set<std::string> cycles;
  for(const auto& binding:author.bindings){
   if(!author.keySwitchEnabled||binding.type!="cycle"||author.controls.count(binding.variable)||!cycles.insert(binding.variable).second)continue;
   Require(binding.values.size()<=64,"外观切换状态过多");
   const auto key=author.root.filename().string()+":"+binding.variable;
   const auto bounds=std::minmax_element(binding.values.begin(),binding.values.end());
   const auto label=binding.key.vk>='A'&&binding.key.vk<='Z'?"外观切换（"+std::string(1,char(binding.key.vk))+"）":"外观切换";
   if(comma)controls<<',';comma=true;
   controls<<"{\"owner\":"<<Json(author.root.filename().string())<<",\"key\":"<<Json(key)<<",\"kind\":\"cycle\",\"label\":"<<Json(label)<<",\"min\":"<<*bounds.first<<",\"max\":"<<*bounds.second<<",\"default\":"<<author.defaults.at(binding.variable)<<",\"states\":[";
   for(size_t i=0;i<binding.values.size();++i){if(i)controls<<',';controls<<std::setprecision(17)<<binding.values[i];}controls<<"]}";
   graph<<"control\t"<<key<<'\t'<<*bounds.first<<'\t'<<*bounds.second<<'\t'<<author.defaults.at(binding.variable)<<'\n';
  }
 }
 for(const auto& author:authors)for(const auto& row:author.controls){
  const auto key=author.root.filename().string()+":"+row.first;
  const auto variable="$"+EiemComposedPackage::Prefix(author.id)+row.first.substr(1);
  controlKeys[variable]=key;const auto& c=row.second;
  if(comma)controls<<',';comma=true;
  controls<<"{\"owner\":"<<Json(author.root.filename().string())<<",\"key\":"<<Json(key)<<",\"label\":"<<Json(c.label)<<",\"min\":"<<std::setprecision(17)<<c.minimum<<",\"max\":"<<c.maximum<<",\"default\":"<<author.defaults.at(row.first)<<'}';
  graph<<"control\t"<<key<<'\t'<<std::setprecision(17)<<c.minimum<<'\t'<<c.maximum<<'\t'<<author.defaults.at(row.first)<<'\n';
 }
 for(const auto& author:authors)for(const auto& row:author.shapeAnimations){
  const auto& clip=row.second;const auto key=author.root.filename().string()+":"+clip.speedVariable;
  Require(clip.duration>=1e-6&&clip.duration<=86400&&clip.speed<=100,"动画时长或速度超出范围");
  if(comma)controls<<',';comma=true;
  controls<<"{\"owner\":"<<Json(author.root.filename().string())<<",\"key\":"<<Json(key)<<",\"label\":"<<Json(clip.label+" · 播放速度（0 暂停）")<<",\"min\":0,\"max\":3,\"default\":"<<clip.speed<<'}';
  graph<<"control\t"<<key<<"\t0\t"<<std::max(3.,clip.speed)<<'\t'<<clip.speed<<'\n';
  const auto id=EiemComposedPackage::Name(author.id,clip.id);
  graph<<"animation\t"<<id<<'\t'<<key<<'\t'<<std::setprecision(17)<<clip.duration<<'\t'<<(clip.loop?1:0)<<'\t'<<(clip.autoplay?1:0)<<'\n';
  for(const auto& track:clip.tracks){
   const auto variable="$"+EiemComposedPackage::Prefix(author.id)+track.first.substr(1);
   Require(controlKeys.count(variable)&&track.second.size()<=1000000,"动画轨道无效或过大");
   graph<<"track\t"<<id<<'\t'<<controlKeys.at(variable);for(float value:track.second)graph<<'\t'<<value;graph<<'\n';
  }
 }
 controls<<"]}";auto ui=controls.str();EiemOfflineCompile::AtomicSave(directory/"controls.json",Bytes(ui.begin(),ui.end()));
 for(const auto& p:plan.parts){const auto& part=package.parts.at(p.second.id);
  if(RuntimeVisibility(*part.visible)||!part.visible->Evaluate(package.defaults))graph<<"visibility\t"<<p.first<<'\t'<<p.second.vertices<<'\t'<<VisibilityText(*part.visible)<<'\n';
  for(const auto& binding:part.shapes){
  Require(controlKeys.count(binding.second),"形态键变量未导出");
  graph<<"shape\t"<<p.first<<'\t'<<binding.first<<'\t'<<controlKeys.at(binding.second)<<'\t'<<p.second.vertices<<'\t'<<p.second.shapes.size()<<'\n';
 }}
 auto text=graph.str();EiemOfflineCompile::AtomicSave(directory/"hot.tsv",Bytes(text.begin(),text.end()));
 status("安卓资源编译完成");return {};
}catch(const std::exception& e){return e.what();}}

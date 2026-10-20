#include "../third_party/eff/src/eiem_static_resource_sources.h"
#include <iostream>

// Offline check of the same composition/native resource pipeline used on Android.
// Arguments: game output (author-directory android-selection.bin){1..4}
int wmain(int argc,wchar_t** argv){try{
 using namespace EiemDirectory;
 Require(argc>=5&&argc<=11&&argc%2==1,"compile_eff_android_set game output (mod selection){1..4}");
 const auto game=std::filesystem::canonical(argv[1]);
 const auto output=std::filesystem::absolute(argv[2]);
 std::vector<Package> authors;EiemStaticSources::Selection selected;
 for(int i=3;i<argc;i+=2){
  authors.push_back(Load(argv[i]));
  auto next=EiemStaticSources::Decode(Read(argv[i+1]));
  for(const auto& p:next.assets){auto found=selected.assets.emplace(p);Require(found.second||found.first->second==p.second,"asset source conflict");}
  for(const auto& p:next.inputs){auto found=selected.inputs.emplace(p);Require(found.second||found.first->second.revision==p.second.revision,"input hash conflict");}
  for(const auto& p:next.dependencies)selected.dependencies[p.first].insert(p.second.begin(),p.second.end());
  selected.candidates.insert(next.candidates.begin(),next.candidates.end());
 }
 selected.manifest="offline-set-validation";
 auto package=EiemComposedPackage::Compose(authors,true);
 EiemStaticSources::Ensure(selected,output/"baseline",output/"discovery",package,game);
 auto plan=EiemResourceReloadPlan::Build(package,output/"baseline",output/"generation","android-set-validation",{}, {}, {}, {},false,game);
 std::cout<<"ANDROID SET READY authors="<<authors.size()<<" parts="<<plan.parts.size()<<" roots="<<plan.roots.size()<<std::endl;
 return 0;
}catch(const std::exception& e){std::cerr<<"ANDROID SET REJECTED: "<<e.what()<<std::endl;return 1;}}

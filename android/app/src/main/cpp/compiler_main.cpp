#include "eiem_static_resource_sources.h"
#include "eiem_vfs_catalog.h"
#include <iostream>
int main(int argc,char** argv){
 try{
  using namespace EiemDirectory;
  Require(argc==5,"eff_compile <mod> <selection.bin> <game-root> <output>");
  auto p=Load(argv[1]);
  auto selected=EiemStaticSources::Decode(Read(argv[2]));
  std::filesystem::path output=argv[4],game=argv[3];
  EiemStaticSources::Ensure(selected,output/"baseline",output/"discovery",p,game);
  auto plan=EiemResourceReloadPlan::Build(p,output/"baseline",output/"generation","android-native",{},[](const char* name,size_t n,size_t total){std::cout<<name<<" "<<n<<"/"<<total<<std::endl;},{},{},false,game);
  std::cout<<"ANDROID COMPILED roots="<<plan.roots.size()<<std::endl;
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}

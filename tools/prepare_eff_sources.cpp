#include "../third_party/eff/tools/eff_author_sources.h"
#include <iostream>
int wmain(int n,wchar_t** a){try{EiemDirectory::Require(n==4,"prepare <mod> <game> <index>");auto p=EiemDirectory::Load(a[1]);auto s=EiemAuthorSources::Prepare(p,a[2],a[3]);std::cout<<"Prepared inputs="<<s.inputs.size()<<" candidates="<<s.candidates.size()<<std::endl;return 0;}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}

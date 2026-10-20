#include "../third_party/eff/src/eiem_static_resource_sources.h"
#include <iostream>
using namespace EiemDirectory;
using namespace EiemNativeInput;
struct Info {std::string name;std::set<std::string> members,externals;};
Info InspectGraph(const Bytes& raw) {
    Info info;const auto bundle=EndfieldInput::Decode(raw);
    for(const auto& m:bundle.members)if(SerializedHeader(m.bytes)) {
        info.members.insert(m.name);const auto layout=ParseSerialized(m.bytes);
        for(const auto& e:EiemLoadingGraphAudit::ReadExternals(m.bytes,layout))info.externals.insert(e.member);
        for(const auto& row:layout.objects)if(layout.types.at(row.second.type).classId==142)
            info.name=EiemResourceReloadPlan::Name(layout.types.at(row.second.type),ObjectValue(m.bytes,layout,row.first));
    }
    return info;
}
std::string Join(const std::set<std::string>& values){std::string out;for(const auto& v:values){if(!out.empty())out+=',';out+=v;}return out;}
int wmain(int argc,wchar_t** argv){try{
    Require(argc==4,"audit_android_graph <selection> <game> <compiled>");
    const auto selected=EiemStaticSources::Decode(Read(argv[1]));const auto game=std::filesystem::canonical(argv[2]);const auto root=std::filesystem::canonical(argv[3]);
    std::vector<std::string> roots;const auto sources=EiemResourceReloadPlan::ReadBaseline(root/"baseline",roots,game);
    std::ofstream graph(root/"graph.tsv",std::ios::binary);graph<<"EFF_ANDROID_GRAPH\t1\n";
    std::ofstream additions(root/"added-dependencies.tsv",std::ios::binary);size_t added=0;
    for(const auto& s:sources){auto original=EiemResourceReloadPlan::ReadSource(root/"baseline",s);auto raw=Read(root/"generation/startup"/s.file);
        const auto before=InspectGraph(original),after=InspectGraph(raw);std::set<std::string> extra;
        std::set_difference(after.externals.begin(),after.externals.end(),before.externals.begin(),before.externals.end(),std::inserter(extra,extra.end()));
        std::string logical;for(const auto& e:selected.inputs)if(e.second.physical==s.physical&&e.second.offset==s.offset)logical=e.first;
        Require(!logical.empty(),"graph input lost source identity");
        graph<<s.file<<'\t'<<after.name<<'\t'<<logical<<'\t'<<Join(after.members)<<'\t'<<Join(after.externals)<<'\n';
        if(!extra.empty()){added+=extra.size();additions<<logical<<'\t'<<Join(extra)<<'\n';}
    }
    std::cout<<"Graph nodes="<<sources.size()<<" added dependency edges="<<added<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}

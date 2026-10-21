// Host regression check for the compact fixed-layout shape-array codec.
#include "../third_party/eff/src/eiem_serialized_writer.h"
#include <iostream>
using namespace EiemNativeInput;
int main(){try{
 Type type;
 auto node=[&](std::string kind,std::string name){size_t id=type.nodes.size();type.nodes.push_back({kind,name});return id;};
 node("Array","Array");node("int","size");node("BlendShapeVertex","data");type.nodes[0].children={1,2};
 for(const char* name:{"vertex","normal","tangent"}){size_t v=node("Vector3f",name);type.nodes[2].children.push_back(v);for(char axis:std::string("xyz")){size_t s=node("float",std::string(1,axis));type.nodes[v].children.push_back(s);}}
 auto index=node("unsigned int","index");type.nodes[2].children.push_back(index);
 Bytes bytes;AppendInteger(bytes,2,4);
 for(size_t record=0;record<2;++record){for(size_t scalar=0;scalar<9;++scalar){float value=float(record*10+scalar)/7;uint32_t bits;memcpy(&bits,&value,4);AppendInteger(bytes,bits,4);}AppendInteger(bytes,record,4);}
 auto packed=DecodeValue(type,0,bytes);Require(packed.children.empty()&&packed.count==2&&packed.raw.size()==80,"shape array was not packed");Require(EncodeValue(type,packed)==bytes,"packed shape roundtrip changed bytes");
 auto generic=type;generic.nodes[2].type="UnrecognizedRecord";auto expanded=DecodeValue(generic,0,bytes);Require(expanded.children.size()==2&&EncodeValue(generic,expanded)==bytes,"generic fallback/shape bytes differ");
 auto different=type;different.nodes[type.nodes[3].children[0]].name="other";Require(!PackedArrayStride(different,2),"unknown vector layout packed");
 auto truncated=bytes;truncated.pop_back();bool rejected=false;try{DecodeValue(type,0,truncated);}catch(const std::exception&){rejected=true;}Require(rejected,"truncated packed shape accepted");
 packed.count=3;rejected=false;try{EncodeValue(type,packed);}catch(const std::exception&){rejected=true;}Require(rejected,"inconsistent packed count accepted");
 std::cout<<"PACKED SHAPES OK: exact roundtrip, generic fallback, schema guard, truncation/count rejection\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

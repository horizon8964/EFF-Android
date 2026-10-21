#include "../android/app/src/main/cpp/visibility_expression.h"
#include <iostream>
#include <stdexcept>
int main(){try{
 EiemVariables variables{{"$modA_saber",0},{"$modB_saber",1}};
 for(const auto& text:std::vector<std::string>{"$modA_saber == 0","$modA_saber == 1","$modA_saber == 0 && $modB_saber == 1","!($modA_saber != 0) || -2 < -1"}){
  std::string error;auto source=EiemExpressionParser(text,error).Parse();if(!source||!source->Validate(variables,error))throw std::runtime_error(error);
  auto saved=VisibilityText(*source);auto copy=EiemExpressionParser(saved,error).Parse();if(!copy||!copy->Validate(variables,error))throw std::runtime_error(error);
  for(int a=0;a<2;++a)for(int b=0;b<2;++b){variables["$modA_saber"]=a;variables["$modB_saber"]=b;if(copy->Evaluate(variables)!=source->Evaluate(variables))throw std::runtime_error("visibility condition changed");}
 }
 std::string error,text="$missing == 1";auto missing=EiemExpressionParser(text,error).Parse();if(!missing||missing->Validate(variables,error))throw std::runtime_error("unknown visibility variable accepted");
 std::cout<<"VISIBILITY CONDITIONS OK: cycling, composed namespaces, roundtrip, missing-variable rejection\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

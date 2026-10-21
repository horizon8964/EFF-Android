#pragma once
#include "eiem_expression.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

// Preserve the verified EFF condition grammar, including composed namespaces.
inline std::string VisibilityText(const EiemExpression& expression){
 struct Frame{const EiemExpression* node;unsigned phase;};
 std::vector<Frame> stack{{&expression,0}};std::ostringstream out;out<<std::setprecision(17);
 const char* tokens[]={"","","!","-","==","!=","<","<=",">",">=","&&","||"};
 size_t visited=0;
 while(!stack.empty()){
  auto& frame=stack.back();const auto* node=frame.node;
  if(frame.phase==0){if(++visited>16384)throw std::runtime_error("visibility expression too large");
   if(node->op==EiemExpression::Number){out<<node->number;stack.pop_back();continue;}
   if(node->op==EiemExpression::Variable){out<<node->variable;stack.pop_back();continue;}
   if(node->op<EiemExpression::Not||node->op>EiemExpression::Or||!node->left)throw std::runtime_error("visibility operator invalid");
   out<<'(';if(node->op==EiemExpression::Not||node->op==EiemExpression::Negative)out<<tokens[node->op];
   frame.phase=1;stack.push_back({node->left.get(),0});continue;
  }
  if(frame.phase==1&&node->op!=EiemExpression::Not&&node->op!=EiemExpression::Negative){
   if(!node->right)throw std::runtime_error("visibility operand absent");out<<tokens[node->op];frame.phase=2;stack.push_back({node->right.get(),0});continue;
  }
  out<<')';stack.pop_back();
 }
 auto text=out.str();if(text.size()>65536)throw std::runtime_error("visibility expression text too large");return text;
}

#pragma once
#include <string>
// Returns an empty string on success; errors never publish a ready descriptor.
std::string CompileEff(const std::string& mod,const std::string& game,const std::string& output);

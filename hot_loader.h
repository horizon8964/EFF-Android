#pragma once
#include <string>
void StartHotLoader(void* library,void* domain,const std::string& diagnostics);
void AdvanceHotFrame();
void RequestHot(const std::string& output,long long sequence);

void RequestShapeValues(const std::string& values);

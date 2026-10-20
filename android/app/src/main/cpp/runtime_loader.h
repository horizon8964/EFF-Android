#pragma once
#include <string>
void StartEffLoader(void* library, void* domain, const std::string& diagnostics);
bool AdvanceEffFrame();

@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
 echo Run from an x64 Native Tools Command Prompt for Visual Studio.
 exit /b 1
)
pushd "%~dp0.."
if not exist research\si-compiler mkdir research\si-compiler
if /i "%~1"=="graph" (
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /bigobj /W3 tools\audit_android_graph.cpp /Foresearch\si-compiler\graph.obj /Feresearch\si-compiler\audit_android_graph.exe /link bcrypt.lib Cabinet.lib
if errorlevel 1 exit /b 1
exit /b 0
)
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /bigobj /W3 tools\compile_si_android.cpp /Foresearch\si-compiler\compiler.obj /Feresearch\si-compiler\compile_si_android.exe /link bcrypt.lib Cabinet.lib
set "compile_result=%errorlevel%"
popd
exit /b %compile_result%

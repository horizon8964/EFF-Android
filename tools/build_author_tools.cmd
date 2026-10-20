@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from an x64 Native Tools Command Prompt for Visual Studio.
  exit /b 1
)
pushd "%~dp0.."
if not exist build\author-tools mkdir build\author-tools
if not exist third_party\eff\nativepack mkdir third_party\eff\nativepack
cl /nologo /utf-8 /O2 /MD /LD /EHsc /std:c++17 /bigobj /Fo"build\author-tools\core.obj" third_party\eff\src\eiem_author_core.cpp /Fe"third_party\eff\nativepack\eff_author_core.dll" /link bcrypt.lib
if errorlevel 1 goto failed
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /bigobj /Fo"build\author-tools\prepare.obj" tools\prepare_eff_sources.cpp /Fe"build\author-tools\prepare_eff_sources.exe" /link bcrypt.lib Cabinet.lib
if errorlevel 1 goto failed
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /bigobj /Fo"build\author-tools\set.obj" tools\compile_eff_android_set.cpp /Fe"build\author-tools\compile_eff_android_set.exe" /link bcrypt.lib Cabinet.lib
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1

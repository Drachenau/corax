@rem SPDX-License-Identifier: Apache-2.0
@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "CORAX_PRESET=%~1"
if /I "%CORAX_PRESET%"=="ci-debug" goto preset_valid
if /I "%CORAX_PRESET%"=="ci-release" goto preset_valid
echo ::error::Expected the ci-debug or ci-release preset.
exit /b 2

:preset_valid
set "CORAX_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%CORAX_VSWHERE%" goto vswhere_found
echo ::error::Microsoft vswhere.exe was not found at %CORAX_VSWHERE%.
exit /b 1

:vswhere_found
set "CORAX_VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%CORAX_VSWHERE%" -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CORAX_VS_INSTALL=%%I"
if defined CORAX_VS_INSTALL goto vs_install_found
echo ::error::Visual Studio 2022 with the x64 C++ tools was not found.
exit /b 1

:vs_install_found
set "CORAX_VSDEVCMD=%CORAX_VS_INSTALL%\Common7\Tools\VsDevCmd.bat"
if exist "%CORAX_VSDEVCMD%" goto vsdevcmd_found
echo ::error::VsDevCmd.bat was not found at %CORAX_VSDEVCMD%.
exit /b 1

:vsdevcmd_found
call "%CORAX_VSDEVCMD%" -no_logo -arch=x64 -host_arch=x64
set "CORAX_EXIT=%ERRORLEVEL%"
if "%CORAX_EXIT%"=="0" goto environment_ready
echo ::error::VsDevCmd.bat failed with exit code %CORAX_EXIT%.
exit /b %CORAX_EXIT%

:environment_ready
echo ::group::MSVC toolchain
cmake --version
if errorlevel 1 goto toolchain_failed
where cl
if errorlevel 1 goto toolchain_failed
cl 2>&1
qmake -query QT_VERSION
if errorlevel 1 goto toolchain_failed
echo ::endgroup::
goto toolchain_ready

:toolchain_failed
echo ::endgroup::
echo ::error::The configured MSVC, CMake, or Qt toolchain is incomplete.
exit /b 1

:toolchain_ready

echo ::group::Configure %CORAX_PRESET%
cmake --preset "%CORAX_PRESET%"
set "CORAX_EXIT=%ERRORLEVEL%"
echo ::endgroup::
if not "%CORAX_EXIT%"=="0" exit /b %CORAX_EXIT%

echo ::group::Build %CORAX_PRESET%
cmake --build --preset "%CORAX_PRESET%"
set "CORAX_EXIT=%ERRORLEVEL%"
echo ::endgroup::
if not "%CORAX_EXIT%"=="0" exit /b %CORAX_EXIT%

echo ::group::Test %CORAX_PRESET%
ctest --preset "%CORAX_PRESET%" --output-on-failure
set "CORAX_EXIT=%ERRORLEVEL%"
echo ::endgroup::
exit /b %CORAX_EXIT%

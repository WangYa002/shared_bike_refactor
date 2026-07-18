@echo off
REM === Build bike_client.exe (after configure.bat) ======================
REM Uses the same VS env + vcpkg-downloaded CMake as configure.bat.
REM =======================================================================

set "VCPKG_ROOT=D:\vcpkg"
set "VS_VARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

call "%VS_VARS%"
if errorlevel 1 exit /b 1

"%VCPKG_ROOT%\downloads\tools\cmake-4.3.3-windows\cmake-4.3.3-windows-x86_64\bin\cmake.exe" ^
  --build "%~dp0build" ^
  --target bike_client ^
  -j

REM Bundle Qt + protobuf runtime DLLs next to the exe (one-time after rebuild)
if exist "%~dp0build\client\bike_client.exe" (
  "C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe" "%~dp0build\client\bike_client.exe"
  copy /Y "C:\Qt\6.10.1\msvc2022_64\bin\Qt6Concurrent.dll" "%~dp0build\client\" >nul
)

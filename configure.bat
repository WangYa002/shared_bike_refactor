@echo off
REM === Client-only configure for Windows (MSVC + Qt6 + vcpkg) ============
REM Adjust these paths for your machine if needed:
REM   - vcvars64.bat location (Visual Studio install)
REM   - vcpkg root
REM   - Qt6 msvc2022_64 path
REM Prereqs:
REM   - vcpkg bootstrap'd, with `vcpkg install protobuf --triplet x64-windows`
REM   - Qt 6.x with msvc2022_64 components
REM   - Visual Studio 2022+ with C++ desktop workload
REM =======================================================================

set "VCPKG_ROOT=D:\vcpkg"
set "QT_MSVC=C:/Qt/6.10.1/msvc2022_64"
set "VS_VARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

call "%VS_VARS%"
if errorlevel 1 exit /b 1

"%VCPKG_ROOT%\downloads\tools\cmake-4.3.3-windows\cmake-4.3.3-windows-x86_64\bin\cmake.exe" ^
  -B "%~dp0build" ^
  -S "%~dp0" ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT_MSVC%" ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DBIKE_BUILD_CLIENT=ON ^
  -DBIKE_BUILD_SERVER=OFF ^
  -DBIKE_BUILD_TESTS=OFF ^
  -DCMAKE_MAKE_PROGRAM="%VCPKG_ROOT%/downloads/tools/ninja-1.13.2-windows/ninja.exe"

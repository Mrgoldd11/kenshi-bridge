@echo off
rem Build KenshiBridge.dll with the VS2010 compiler (v100, x64), Release.
setlocal
call "%~dp0env.cmd"
if not exist "%VC64%\bin\amd64\cl.exe" (
  echo Compiler not found: unpack Windows SDK 7.1, see README.
  exit /b 1
)
if not exist "%DEPS%\KenshiLib\Libraries\KenshiLib.lib" (
  echo KenshiLib_Examples_deps not found in _downloads\deps, see README.
  exit /b 1
)
set "OUT=%KB_ROOT%\build"
if not exist "%OUT%" mkdir "%OUT%"
cd /d "%OUT%"
rem Boost libraries are picked by auto-link from boost_1_60_0\stage\lib (static, vc100).
cl /nologo /c /O2 /GL /MD /EHa /W3 /DNDEBUG /DWIN32 /DUNICODE /D_UNICODE /D_WINDOWS /D_USRDLL ^
  /wd4996 /wd4251 /wd4244 /wd4800 /wd4091 ^
  /I"%DEPS%\KenshiLib\Include" /I"%DEPS%\boost_1_60_0" ^
  "%KB_ROOT%\src\plugin.cpp" "%KB_ROOT%\src\server.cpp" "%KB_ROOT%\src\json.cpp" "%KB_ROOT%\src\trace.cpp" "%KB_ROOT%\src\rva.cpp" "%KB_ROOT%\src\borders.cpp" || exit /b 1
link /nologo /DLL /LTCG /OUT:KenshiBridge.dll plugin.obj server.obj json.obj trace.obj rva.obj borders.obj ^
  /LIBPATH:"%DEPS%\KenshiLib\Libraries" /LIBPATH:"%DEPS%\boost_1_60_0\stage\lib" KenshiLib.lib OgreMain_x64.lib ws2_32.lib || exit /b 1
echo BUILD_OK %OUT%\KenshiBridge.dll

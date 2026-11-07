@echo off
rem Build the protocol test harness (real server.cpp/json.cpp, no game) and run the Node tests.
setlocal
set "KB_USER_PATH=%PATH%"
call "%~dp0env.cmd"
set "OUT=%KB_ROOT%\build"
if not exist "%OUT%\harness" mkdir "%OUT%\harness"
cd /d "%OUT%\harness"
cl /nologo /O2 /MD /EHa /W3 /DNDEBUG /DWIN32 /DUNICODE /D_UNICODE /wd4996 /I"%KB_ROOT%\tests\stub" ^
  /Fe"%OUT%\bridge-harness.exe" "%KB_ROOT%\tests\harness.cpp" "%KB_ROOT%\src\server.cpp" "%KB_ROOT%\src\json.cpp" ws2_32.lib || exit /b 1
cd /d "%KB_ROOT%"
set "PATH=%KB_USER_PATH%"
node --test tests/protocol.test.mjs

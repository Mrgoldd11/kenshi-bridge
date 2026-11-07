@echo off
rem Install the bridge mod into the game: <Kenshi>\mods\KenshiBridge\ (3 files).
rem Usage: install.cmd "D:\SteamLibrary\steamapps\common\Kenshi"
rem An existing KenshiBridge folder is moved to build\game-backup\KenshiBridge (previous backup replaced).
setlocal
set "KB_ROOT=%~dp0.."
set "GAME=%~1"
if "%GAME%"=="" (
  echo Usage: install.cmd "path\to\Kenshi"
  exit /b 1
)
if not exist "%GAME%\Kenshi_x64.exe" (
  echo Kenshi_x64.exe not found in "%GAME%"
  exit /b 1
)
if not exist "%GAME%\RE_Kenshi.dll" (
  echo RE_Kenshi is not installed in "%GAME%"
  exit /b 1
)
if not exist "%KB_ROOT%\build\KenshiBridge.dll" (
  echo build\KenshiBridge.dll not found: run scripts\build.cmd first
  exit /b 1
)
set "DST=%GAME%\mods\KenshiBridge"
set "BAK=%KB_ROOT%\build\game-backup"
if exist "%DST%" (
  if exist "%BAK%\KenshiBridge" rmdir /s /q "%BAK%\KenshiBridge"
  if not exist "%BAK%" mkdir "%BAK%"
  xcopy /e /i /q /y "%DST%" "%BAK%\KenshiBridge" >nul || exit /b 1
  rmdir /s /q "%DST%" || exit /b 1
  echo Previous version kept in "%BAK%\KenshiBridge"
)
mkdir "%DST%" || exit /b 1
copy /y "%KB_ROOT%\build\KenshiBridge.dll" "%DST%\" >nul || exit /b 1
copy /y "%KB_ROOT%\mod\KenshiBridge\KenshiBridge.mod" "%DST%\" >nul || exit /b 1
copy /y "%KB_ROOT%\mod\KenshiBridge\RE_Kenshi.json" "%DST%\" >nul || exit /b 1
echo INSTALLED "%DST%"
echo Enable "KenshiBridge" in the game launcher (Mods tab).

@echo off
rem Remove the bridge mod from the game: deletes <Kenshi>\mods\KenshiBridge\ (only this folder).
rem Usage: uninstall.cmd "D:\SteamLibrary\steamapps\common\Kenshi"
rem Turn the mod off in the launcher first, otherwise the launcher keeps a missing entry in mods.cfg.
setlocal
set "GAME=%~1"
if "%GAME%"=="" (
  echo Usage: uninstall.cmd "path\to\Kenshi"
  exit /b 1
)
set "DST=%GAME%\mods\KenshiBridge"
if exist "%DST%" rmdir /s /q "%DST%" || exit /b 1
echo REMOVED "%DST%"

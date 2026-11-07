@echo off
rem Окружение компилятора VS2010 (v100, x64) из распакованного Windows SDK 7.1.
rem Ничего не меняет в системе: переменные действуют только в этом окне.
set "KB_ROOT=%~dp0.."
set "TC=%KB_ROOT%\toolchain"
set "VC32=%TC%\Program Files\Microsoft Visual Studio 10.0\VC"
set "VC64=%TC%\Program Files(64)\Microsoft Visual Studio 10.0\VC"
set "WSDK=%TC%\Program Files\Microsoft SDKs\Windows\v7.1"
set "DEPS=%KB_ROOT%\_downloads\deps"
set "PATH=%VC64%\bin\amd64;%WSDK%\Bin\x64;%WSDK%\Bin;%SystemRoot%\system32"
set "INCLUDE=%VC32%\include;%WSDK%\Include"
set "LIB=%VC64%\lib\amd64;%WSDK%\Lib\x64"

@echo off
setlocal

set "NATIVE_BASH=E:\toolsE\msys64\usr\bin\bash.exe"
set "BUILD_SCRIPT=%~dp0..\target\native\compat\build.sh"

if not exist "%NATIVE_BASH%" (
    echo Missing Windows-native MSYS2 bash: %NATIVE_BASH% 1>&2
    exit /b 1
)
if not exist "%BUILD_SCRIPT%" (
    echo Missing native build script: %BUILD_SCRIPT% 1>&2
    exit /b 1
)

"%NATIVE_BASH%" --noprofile --norc "%BUILD_SCRIPT%" %*
exit /b %errorlevel%

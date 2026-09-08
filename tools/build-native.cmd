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

if "%~1"=="" (
    rem Clear a stale test define so a plain build always boots the shell.
    "%NATIVE_BASH%" --noprofile --norc "%BUILD_SCRIPT%" DEFS=
) else (
    "%NATIVE_BASH%" --noprofile --norc "%BUILD_SCRIPT%" %*
)
exit /b %errorlevel%

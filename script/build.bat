@echo off
cd /d "%~dp0.."
REM 請在「x64 Native Tools Command Prompt for VS」中執行
if not exist bin mkdir bin
cl /nologo /EHsc /std:c++17 /O2 /utf-8 src\main.cpp /Fo:bin\ /Fe:bin\CameraPicker.exe /link /SUBSYSTEM:WINDOWS

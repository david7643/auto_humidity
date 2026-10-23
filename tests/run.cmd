@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist .tools\tests mkdir .tools\tests
cl /nologo /std:c++17 /EHsc /W4 /wd4244 /wd4267 /I tests\fakes tests\test_firmware.cpp /Fo.tools\tests\test_firmware.obj /Fe.tools\tests\test_firmware.exe
if errorlevel 1 exit /b 1
.tools\tests\test_firmware.exe

@echo off
setlocal EnableDelayedExpansion
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
set "HELTEC_TEST_OUT=%TEMP%\heltec-native-tests"
if not exist "%HELTEC_TEST_OUT%" mkdir "%HELTEC_TEST_OUT%"
for %%m in (1 2 3 4) do (
  for %%r in (0 1) do (
    cl /nologo /EHsc /std:c++17 /Itests\stubs /I. /DTRANSFER_MODE=%%m /DCURRENT_NODE_ROLE=%%r tests\native_tests.cpp /Fo"%HELTEC_TEST_OUT%\m%%m-r%%r.obj" /Fe"%HELTEC_TEST_OUT%\m%%m-r%%r.exe"
    if not "!errorlevel!"=="0" exit /b 1
    "%HELTEC_TEST_OUT%\m%%m-r%%r.exe"
    if not "!errorlevel!"=="0" exit /b 1
  )
)
exit /b 0

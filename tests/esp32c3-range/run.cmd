@echo off
setlocal
where cl >nul 2>nul
if not errorlevel 1 goto compile
set "rangeVsWhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%rangeVsWhere%" (
    echo Visual Studio C++ Build Tools not found. Run this script from a Developer Command Prompt.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%rangeVsWhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "rangeVsPath=%%i"
if not defined rangeVsPath exit /b 1
call "%rangeVsPath%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:compile
pushd "%~dp0"
if not exist "build" mkdir "build"
cl /nologo /std:c++17 /EHsc /W4 /WX /I. /Febuild\test_uart_range.exe /Fobuild\ test_uart_range.cpp ..\..\extras\esp32c3-range-test\PN532_All_Cards_UART_Range_Test\Pn532Hsu.cpp
if errorlevel 1 goto failed
build\test_uart_range.exe
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1

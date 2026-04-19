@echo off
:: post_build.bat — copy built hex to release folder with version number from main.h
::
:: Called by Keil after build via:
::   Options for Target → User → After Build → Run #1
::   Command: $Proj_Dir$\post_build.bat

set HEADER=..\Core\Inc\main.h
set HEX_SRC=STM32C011F6U6\STM32C011F6U6.hex
set RELEASE_DIR=..\release_hex

:: Extract version string from #define FIRMWARE_VERSION "..."
for /f "tokens=3 delims= " %%V in ('findstr /c:"#define FIRMWARE_VERSION" "%HEADER%"') do set VERSION=%%V

:: Strip surrounding quotes
set VERSION=%VERSION:"=%

:: Delete any existing rrf hex files in the release folder
for %%F in ("%RELEASE_DIR%\bd_pressure-rrf-*.hex") do (
    echo Post-build: deleting %%F
    del "%%F"
)

:: Copy hex with version as filename
set DEST=%RELEASE_DIR%\%VERSION%.hex
copy /Y "%HEX_SRC%" "%DEST%"
echo Post-build: copied %HEX_SRC% to %DEST%

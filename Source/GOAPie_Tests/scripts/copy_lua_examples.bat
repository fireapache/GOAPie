@echo off
setlocal enabledelayedexpansion

set "SRC_DIR=%~1"
set "DEST_ROOT=%~2"

if "%SRC_DIR%"=="" goto :usage
if "%DEST_ROOT%"=="" goto :usage

REM Use the source directory name as-is for the destination.
REM The runtime resolves scripts via examples/<g_exampleName>/scripts/
REM where g_exampleName matches the source directory name (e.g. heistOpenSafe_Lua).
for %%F in ("%SRC_DIR%") do set "BASENAME=%%~nF"

set "DEST=%DEST_ROOT%\%BASENAME%\scripts"

REM Mirror: copy newer/missing .lua files, remove stale ones from destination
robocopy "%SRC_DIR%" "%DEST%" *.lua /MIR /NJH /NJS /NDL /NP

REM Clean up legacy stripped-name directory (old script used to strip _Lua suffix)
set "LEGACY_NAME=%BASENAME%"
if /I "!LEGACY_NAME:~-4!"=="_Lua" (
    set "LEGACY_NAME=!LEGACY_NAME:~0,-4!"
    if exist "%DEST_ROOT%\!LEGACY_NAME!\scripts" (
        echo Cleaning legacy dir: !LEGACY_NAME!\scripts\
        rd /S /Q "%DEST_ROOT%\!LEGACY_NAME!\scripts"
    )
)

REM robocopy exit codes 0-7 are success, 8+ are errors
if %ERRORLEVEL% GEQ 8 exit /B 1
exit /B 0

:usage
echo Usage: copy_lua_examples.bat ^<source_dir^> ^<dest_root^>
echo Example: copy_lua_examples.bat "scripts\heistOpenSafe_Lua" "bin\examples"
exit /B 0

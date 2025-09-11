@echo off
setlocal enabledelayedexpansion

set "SRC_DIR=%~1"
set "DEST_ROOT=%~2"

if "%SRC_DIR%"=="" goto :usage
if "%DEST_ROOT%"=="" goto :usage

for %%F in ("%SRC_DIR%") do set "BASENAME=%%~nF"
set "RUNTIME_NAME=%BASENAME%"

REM Strip trailing _lua if present
if /I "!RUNTIME_NAME:~-4!"=="_lua" set "RUNTIME_NAME=!RUNTIME_NAME:~0,-4!"

set "DEST=%DEST_ROOT%\!RUNTIME_NAME!\scripts"

REM Create destination directory if it doesn't exist
if not exist "%DEST%" mkdir "%DEST%"

REM Copy lua files if they don't exist in destination
for %%S in ("%SRC_DIR%\*.lua") do (
    if not exist "%DEST%\%%~nxS" (
        echo Copying %%~nxS to !RUNTIME_NAME!\scripts\
        copy /Y "%%~fS" "%DEST%" >nul
    )
)

goto :end

:usage
echo Usage: copy_lua_examples.bat ^<source_dir^> ^<dest_root^>
echo Example: copy_lua_examples.bat "scripts\example6_lua" "bin\examples"

:end

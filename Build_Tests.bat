@ECHO off

ECHO.
ECHO ==========================================================
ECHO ======        Building GOAPie (Debug + Release)     ======
ECHO ==========================================================
ECHO.

REM Locate MSBuild
SET "MSBUILD="
FOR /F "delims=" %%M IN ('where MSBuild.exe 2^>nul') DO SET "MSBUILD=%%M"
IF "%MSBUILD%"=="" (
    IF EXIST "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" (
        SET "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    )
)
IF "%MSBUILD%"=="" (
    IF EXIST "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe" (
        SET "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
    )
)
IF "%MSBUILD%"=="" (
    IF EXIST "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe" (
        SET "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
    )
)
IF "%MSBUILD%"=="" (
    ECHO ERROR: Could not find MSBuild.exe. Make sure Visual Studio 2022 is installed.
    EXIT /B 1
)

REM Generate project files if solution is missing
IF NOT EXIST "GOAPie.slnx" (
    ECHO Generating project files...
    IF NOT EXIST "premake5.exe" (
        ECHO ERROR: premake5.exe not found. Run GenerateProjectFiles.bat first.
        EXIT /B 1
    )
    CALL premake5.exe vs2026
    IF ERRORLEVEL 1 EXIT /B 1
    ECHO.
)

REM Build Debug
ECHO [1/2] Building Debug x64...
"%MSBUILD%" GOAPie.slnx -p:Configuration=Debug -p:Platform=x64 -p:PlatformToolset=v143 -noLogo -v:minimal
IF ERRORLEVEL 1 (
    ECHO ERROR: Debug build failed.
    EXIT /B 1
)
ECHO.

REM Build Release
ECHO [2/2] Building Release x64...
"%MSBUILD%" GOAPie.slnx -p:Configuration=Release -p:Platform=x64 -p:PlatformToolset=v143 -noLogo -v:minimal
IF ERRORLEVEL 1 (
    ECHO ERROR: Release build failed.
    EXIT /B 1
)
ECHO.

ECHO ==========================================================
ECHO ======           Build completed successfully       ======
ECHO ==========================================================

@ECHO off

ECHO.
ECHO ==========================================================
ECHO ======   Generating Bagual Engine Project Files... =======
ECHO ==========================================================
ECHO.

IF NOT EXIST "premake5.exe" (
	ECHO Downloading Premake5.zip ...
	POWERSHELL -Command "Invoke-WebRequest https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-windows.zip -OutFile premake-5.0.0-beta8-windows.zip"
	ECHO Extracting Premake5.exe ...
	POWERSHELL -Command "expand-archive -path 'premake-5.0.0-beta8-windows.zip' -destinationpath '.\'"
	ECHO Deleting cached zip ...
	DEL "premake-5.0.0-beta8-windows.zip"
	DEL example.*
	DEL luasocket.*
	DEL premake5.pdb
	ECHO.
)

IF "%1" == "--unity" (
	set buildtype=--unity
) ELSE (
	set buildtype=""
)

IF EXIST "premake5.exe" (
	ECHO Running premake5 ...
	ECHO.
	CALL premake5.exe vs2026 %buildtype%
	ECHO.
) ELSE (
	ECHO Could not run premake5.exe, please get it manually from https://github.com/premake/premake-core/releases/
	ECHO.
)

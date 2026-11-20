@echo off
rem Builds the Windows version through MSYS2 (C:\msys64) and packs dist-win\ and pokeemerald-win.zip.
rem Needs the game files first (see docs\WINDOWS_BUILD.md). Run from anywhere.
setlocal
set "ROOT=%~dp0..\.."
pushd "%ROOT%"
set "ROOT=%CD%"
popd
set "MSYSTEM=MSYS"
"C:\msys64\usr\bin\bash.exe" -lc "cd \"$(cygpath -u '%ROOT%')\" && tools/windows/build.sh %* && tools/windows/package.sh"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo Done: %ROOT%\pokeemerald-win.zip

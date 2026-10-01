@echo off
setlocal
cd /d "%~dp0.."

REM ---- 找 MSVC 環境（Visual Studio 或 Build Tools 皆可）----
where cl >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "%VSWHERE%" (
    echo [錯誤] 找不到 Visual Studio / Build Tools，請先安裝 C++ 桌面開發工具。
    exit /b 1
  )
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
  if not defined VSPATH (
    echo [錯誤] 找不到 MSVC C++ 工具，請在 Build Tools 勾選「使用 C++ 的桌面開發」。
    exit /b 1
  )
  call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)

REM ---- 產生、編譯、打包 ----
if exist build rmdir /s /q build
cmake -S src -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
cd build
cpack -G ZIP || exit /b 1
cd ..

echo.
echo 完成！輸出檔案：
dir /b build\*.zip

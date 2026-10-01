# CameraPicker

使用 Win32 API 與 Media Foundation 製作的桌面程式：選擇攝影機與解析度，並即時預覽。

## 環境需求

- Windows 10 / 11
- [Visual Studio 2019/2022](https://visualstudio.microsoft.com/)（或 Build Tools），安裝時勾選「**使用 C++ 的桌面開發**」
- [CMake](https://cmake.org/download/) 3.15 以上（Visual Studio 已內建，也可單獨安裝並加入 PATH）
- （選用）[NSIS](https://nsis.sourceforge.io/)：要產生安裝程式 `.exe` 時才需要

## 安裝 CMake

任選一種方式：

```powershell
# 方法一：winget（推薦）
winget install Kitware.CMake

# 方法二：Chocolatey
choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System'

# 方法三：Scoop
scoop install cmake
```

或從 [cmake.org/download](https://cmake.org/download/) 下載 Windows x64 Installer（`.msi`），安裝時選擇 **Add CMake to the system PATH**。

Visual Studio 的「使用 C++ 的桌面開發」工作負載也內建 CMake，但需從「Developer PowerShell for VS」使用。

安裝後重新開啟終端機，驗證：

```bat
cmake --version
```

## 專案結構

```
landan-desktop-win32/
├── README.md
├── script/
│   ├── build.bat      # 不用 CMake 的 cl.exe 快速編譯
│   └── package.bat    # 一鍵編譯並打包成 zip
└── src/
    ├── CMakeLists.txt # CMake 建置與 CPack 打包設定
    └── main.cpp       # 程式原始碼
```

## 一行指令打包

在 repo 根目錄的 cmd 或 PowerShell 執行（會自動找到 MSVC，不需開特殊命令提示字元）：

```bat
script\package.bat
```

完成後 `build\CameraPicker-1.0.0-win64.zip` 就是成品。

## 快速編譯（不用 CMake）

在「**x64 Native Tools Command Prompt for VS**」執行：

```bat
script\build.bat
```

產出位置：`bin\CameraPicker.exe`

## 使用 CMake 手動建置

在 repo 根目錄執行：

```bat
:: 1. 產生建置檔
cmake -S src -B build -G "Visual Studio 17 2022" -A x64

:: 2. 編譯 Release 版
cmake --build build --config Release
```

產出位置：`build\Release\CameraPicker.exe`

> 使用 VS 2019 請把 `-G` 改為 `"Visual Studio 16 2019"`。
> 也可用 Ninja：`cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`，產出在 `build\CameraPicker.exe`。

## 打包

編譯完成後執行：

```bat
cd build
cpack -C Release
```

會在 `build\` 產生 `CameraPicker-1.0.0-win64.zip`，內含 `CameraPicker.exe`，解壓即可執行。

### 產生安裝程式（NSIS）

1. 安裝 NSIS 並確認 `makensis` 在 PATH 中。
2. 執行：

   ```bat
   cd build
   cpack -C Release -G NSIS
   ```

3. 會產生 `CameraPicker-1.0.0-win64.exe` 安裝檔。

### 僅複製檔案（不打包）

```bat
cmake --install build --config Release --prefix dist
```

執行檔會被放到 `dist\CameraPicker.exe`。

## 使用方式

1. 啟動程式，自動列出所有攝影機。
2. 在「相機」下拉選單選擇裝置。
3. 在「解析度」下拉選單選擇解析度，預覽會自動切換。
4. 插拔攝影機後按「重新整理」重新偵測。

如果狀態列顯示「無法開啟攝影機」，通常是被其他程式（Teams、Zoom、瀏覽器等）佔用。

## 發佈注意事項

- CMake 設定已使用靜態執行階段（`/MT`），目標電腦**不需**另裝 VC++ Redistributable。
- Media Foundation 為 Windows 內建元件。若是 Windows N / KN 版本，需另外安裝「Media Feature Pack」。
- 第一次執行時，Windows 11 可能要求相機權限：
  「設定 → 隱私權與安全性 → 相機」，需開啟「讓桌面應用程式存取相機」。

# Video Search Finder

一個基於 C++ 核心引擎與 Tauri GUI 的影片重複查找與搜尋工具。

## 專案結構

- `core_src/`: 核心 C++ 引擎，負責影片解碼、特徵提取與重複項比對。
- `gui/`: 基於 Tauri (Rust + React/TypeScript) 的圖形使用者介面。
- `test_videos/`: 用於測試的影片範例。

## 環境需求

### 核心引擎 (Core)
- **C++20** 相容編譯器
- **CMake** (3.20+)
- **FFmpeg**: 需包含 `libavcodec`, `libavformat`, `libavutil`, `libswscale`
- **RocksDB** (選填，用於加速快取)
- **pkg-config**

### 圖形介面 (GUI)
- **Rust** & **Cargo**
- **Node.js** & **npm**

## 編譯與執行

### 1. 編譯核心引擎
進入 `core_src` 資料夾並進行編譯：
```bash
cd core_src
cmake .
make
```
編譯完成後會產生 `dupe_engine` 執行檔。

### 2. 啟動 GUI 介面
進入 `gui` 資料夾並啟動開發模式：
```bash
cd gui
npm install
npm run tauri dev
```
啟動後，GUI 會自動調用 `core_src/dupe_engine` 進行掃描。

## Git 使用說明

### 初始化與推送到遠端
```bash
git init
git add .
git commit -m "Initial commit: Restructured project"
git remote add origin https://github.com/ericoder960803/Video_Search_Finder.git
git branch -M main
git push -u origin main
```

## 功能特點
- **高效能**: 使用 C++20 與多執行緒處理。
- **低負載**: 透過 FFmpeg 進行快速解碼與特徵採樣。
- **現代化介面**: 使用 Tauri 提供輕量級且美觀的桌面體驗。
- **快取支持**: 集成 RocksDB (可選) 以加速重複掃描。

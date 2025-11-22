# YT Music OBS Widget

適用於 [YouTube Music Desktop](https://github.com/ytmdesktop/ytmdesktop) 的 OBS Studio Widget。

<img width="677" height="252" alt="image" src="https://github.com/user-attachments/assets/cf51f56d-aca8-498e-9f67-ddbd17436679" />

## 功能

- **OBS 插件（`plugin/`）**：
  - 從 OBS 擷取音訊來源，並執行 Goertzel 分析。
  - 透過本機 WebSocket **輸出** 12 頻帶的音訊頻譜資料。

- **前端 Widget（`frontend/`）**：
  - 顯示專輯封面、曲名、演唱者、進度條與頻譜。
  - 透過本機 WebSocket **接收** 頻譜資料，並透過 YouTube Music Desktop Companion **接收** 播放狀態與曲目資訊。

## 結構

```
YT_Music_OBS_Widget/
├── frontend/   # 前端 Widget
├── plugin/     # OBS Studio Plugin
└── LICENSE
```

## 使用

### 方式一（僞頻譜）

若您無需準確的頻譜，可直接使用 frontend 下的 Widget。

1. 打開 OBS Studio，加入『瀏覽器』來源；
2. 勾選『本機檔案』，選擇 `index.html`，寬度設定為 `1000`，高度設定為 `360`；
3. 打開 YouTube Music Desktop，Settings - Integrations，依次勾選 `Companion server` → `Allow browser communication` → `Enable companion authorization`；
4. 回到 OBS Studio，重新整理加入的瀏覽器來源。片刻，YouTube Music Desktop 會彈出認證彈窗；
5. 無需細究認證碼，直接點擊 Allow 即可完成。

>Note 1: 若無窗口彈出，請重載軟體或是系統。
>
>Note 2: 在僅使用 HTML 的情況下，前端的頻譜是『僞動態效果』。

### 方式二（真頻譜）

若您需要準確的頻譜，需額外使用 plugin 下的頻譜插件。

1. 下載插件（dll/so），將其放在 OBS 插件目錄下；
2. 打開 OBS Studio，加入『Audio WebSocket Analyzer』來源，此時該插件預設擷取的是 OBS 輸出總線的聲音（所有加入混音的來源）；
3. 加入『應用程式音訊擷取（測試版）』，選擇 YouTube Music Desktop 視窗；
4. 靜音第三步中加入的聲音來源（拖動音量條至最小，而非點擊靜音按鈕）；
5. 重新整理瀏覽器來源，即可看到準確的頻譜在躍動。

>Note 1: 若無頻譜跳躍，請檢查 OBS Studio 的日誌。
>
>Note 2: 在使用插件的情況下，前端的『僞動態效果』將被關閉。

## 參數

主樣式在 `styles/viewer.css` 的 `:root` 中，你可以透過以下變數做整體調整：

- `--card-radius`：卡片圓角大小
- `--card-blur`：毛玻璃模糊強度
- `--progress-height`：進度條高度
- `--widget-scale`：整個部件的縮放倍率
- ……

## 編譯

若想編譯 OBS 插件，需[先編譯一次 OBS Studio](https://github.com/obsproject/obs-studio/wiki/Building-OBS-Studio)，獲取 OBS SDK。

## Linux

在 plugin 資料夾下，指向 OBS SDK：

```bash
cmake -B build -S . \
  -Dlibobs_DIR="/home/username/obs-studio/build/libobs" \
  -DCMAKE_BUILD_TYPE=Release
```

若指向無誤，則編譯之：

```bash
cmake --build build -j$(nproc)
```

## Windows

在 plugin 資料夾下，指向 OBS SDK 和 [SIMDE](https://github.com/simd-everywhere/simde)：

```cmd
cmake -B build -S . `
  -Dlibobs_DIR="D:/obs-studio/build/libobs" `
  -Dw32-pthreads_DIR="D:/obs-studio/build/deps/w32-pthreads" `
  -DSIMDe_INCLUDE_DIR="D:/simde"
```

若指向無誤，則編譯之：

```cmd
cmake --build build --config Release
```

## 授權
**[Waveform](https://github.com/phandasm/waveform)**

GPLv3，我參考了其命名和通信邏輯。

**[Youtube Music OBS Widget](https://github.com/topik/youtube-music-obs-widget)**

GPLv3，我基於該插件刪改了前端的樣式。

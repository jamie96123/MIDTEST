# TypeRush｜STM32 智慧打字反應遊戲

TypeRush 是結合 **網頁打字遊戲** 與 **STM32 NUCLEO-G474RE 開發板** 的微處理機專題。玩家透過網頁輸入英文單字，系統依照剩餘時間計算分數，並利用 UART 通訊控制 STM32 上的紅、黃、綠 LED，提供即時的燈光回饋。

本專題結合 GPIO、PWM、UART、Interrupt、FSM、Non-blocking 及軟體消彈跳等課堂技術。

## 一、專題發想

我們從一顆會亮的 LED 開始，想讓打字練習變得更有趣。於是把網頁連上 STM32，讓按鍵啟動挑戰、倒數決定得分，答對時燈光閃兩下。

## 二、專題功能

- **雙模式：** 自由試玩模式、STM32 連線模式。
- **三種難度：** 簡單 15 秒、普通 10 秒、困難 5 秒。
- **隨機題目：** 每局抽取 10 個不重複的英文單字。
- **即時計分：** 依照答對時的剩餘時間比例給分。
- **PWM 呼吸燈：** 待機時三色 LED 持續呼吸。
- **答對燈光回饋：** 對應顏色 LED 快速閃爍兩次。
- **實體按鍵：** PB0 開始遊戲、PB1 停止遊戲。
- **UART 雙向通訊：** 網頁傳送控制命令，STM32 回傳按鍵事件及通訊訊息。

## 三、遊戲規則

每局共有 10 題，玩家必須在時間內輸入正確的英文單字。

| 剩餘時間比例 | LED 顏色 | 答對得分 |
|---|---|---|
| ≥ 60% | 綠色 | +7 分 |
| 30%～未滿 60% | 黃色 | +5 分 |
| 未滿 30% | 紅色 | +3 分 |
| 時間結束 | 不觸發答對閃燈 | 0 分 |

答錯可以繼續輸入，直到答對或時間結束。

答對時，對應顏色 LED 快速閃爍兩次，接著進入下一題。完成 10 題後，顯示總分與正確率並返回待機。

## 四、開發環境

| 項目 | 使用工具 |
|---|---|
| 開發板 | NUCLEO-G474RE |
| 微控制器 | STM32G474RE |
| 腳位設定 | STM32CubeMX |
| 韌體開發 | STM32CubeIDE |
| 程式語言 | C、HTML、CSS、JavaScript |
| 韌體函式庫 | STM32 HAL Library |
| 網頁與 STM32 連線 | Web Serial API |
| 通訊介面 | LPUART1，115200 baud |

## 五、STM32 接腳配置

| STM32 腳位 | 功能 | 設定 |
|---|---|---|
| PB0 | START 按鍵 | GPIO Input / EXTI0 |
| PB1 | STOP 按鍵 | GPIO Input / EXTI1 |
| PC6 | 紅色 LED | TIM3_CH1 PWM |
| PC9 | 黃色 LED | TIM3_CH4 PWM |
| PC8 | 綠色 LED | TIM3_CH3 PWM |
| PA2 | UART TX | LPUART1_TX |
| PA3 | UART RX | LPUART1_RX |

按鍵使用內部 Pull-up，按下時連接 GND。每顆 LED 各串聯一顆 330 Ω 電阻。

### PWM 設定

TIM3 使用以下參數：

- Timer Clock：170 MHz
- Prescaler：169
- Counter Period：999
- PWM Frequency：1 kHz
- PWM Mode：PWM Mode 1

透過修改 CCR 暫存器的比較值，調整各顆 LED 的 Duty Cycle，實現呼吸燈效果。

## 六、使用的七項技術

| 技術 | 功能說明 |
|---|---|
| 消彈跳（Debounce） | 避免按鍵機械彈跳造成重複觸發 |
| PWM | 控制紅、黃、綠 LED 的亮度及呼吸效果 |
| Interrupt | 設定 EXTI 與 NVIC 中斷功能 |
| Non-blocking | 使用 HAL_GetTick() 管理燈光及按鍵時間 |
| UART | 建立網頁與 STM32 的雙向文字通訊 |
| FSM | 管理待機、遊戲進行與停止狀態 |
| GPIO | 讀取實體按鍵，配置輸入與輸出腳位功能 |

### 主要程式碼

**GPIO 按鍵讀取**

`HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);`

**軟體消彈跳**

`HAL_GetTick();`

**PWM 啟動**

`HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);`

**PWM 亮度控制**

`__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, value);`

**UART 傳送**

`HAL_UART_Transmit(&hlpuart1, data, length, 100);`

**UART 接收**

`HAL_UART_Receive(&hlpuart1, &byte, 1, 1);`

目前單檔 `main.c` 的按鍵事件主要使用 GPIO Polling 搭配軟體消彈跳；EXTI 和 NVIC 已配置，但不代表按鍵事件一定由中斷 Callback 執行。

LED 時間排程使用 Non-blocking 設計，UART 接收則採短逾時 Polling，沒有使用 Ring Buffer。

## 七、UART 通訊協定

STM32 與網頁透過 ST-LINK Virtual COM Port 進行通訊。

通訊設定為 **115200 / 8N1**，每筆指令以換行字元結束。

| 通訊方向 | 指令 | 功能 |
|---|---|---|
| 網頁 → STM32 | HELLO / PING | 測試連線 |
| STM32 → 網頁 | TYPERUSH READY | 握手回覆 |
| 網頁 → STM32 | RUN | 開始遊戲 |
| 網頁 → STM32 | IDLE | 返回待機 |
| 網頁 → STM32 | LEVEL GREEN | 綠燈呼吸 |
| 網頁 → STM32 | LEVEL YELLOW | 黃燈呼吸 |
| 網頁 → STM32 | LEVEL RED | 紅燈呼吸 |
| 網頁 → STM32 | HIT GREEN | 綠燈閃爍兩次 |
| 網頁 → STM32 | HIT YELLOW | 黃燈閃爍兩次 |
| 網頁 → STM32 | HIT RED | 紅燈閃爍兩次 |
| STM32 → 網頁 | EVT START | 實體開始按鍵事件 |
| STM32 → 網頁 | EVT STOP | 實體停止按鍵事件 |

收到 STM32 實際回傳的 `TYPERUSH READY`，才能確認 UART 握手成功。

目前韌體不會直接回報 PWM 的實際 Duty Cycle 或 GPIO 電壓。

## 八、程式運作流程

1. STM32 上電並初始化 GPIO、UART、TIM3。
2. 紅、黃、綠三色 LED 進入待機呼吸狀態。
3. 玩家選擇遊戲難度及自由／連線模式。
4. 連線模式透過 Web Serial 選擇 ST-LINK COM Port。
5. 網頁與 STM32 完成 UART 握手。
6. 玩家透過網站或 PB0 開始遊戲。
7. 系統隨機顯示英文單字並開始倒數。
8. 依照剩餘時間切換 LED 顏色。
9. 答對後計分並控制對應 LED 閃爍兩次。
10. 完成 10 題或按下 STOP，結束遊戲並返回待機。

## 九、如何執行專案

### STM32 韌體

1. 使用 STM32CubeMX 設定 GPIO、TIM3、LPUART1。
2. 在 STM32CubeIDE 的 `Core/Src/main.c` 放入 TypeRush 單檔整合程式。
3. 選擇 Build Project 編譯程式。
4. 透過 ST-LINK 將韌體燒錄至 NUCLEO-G474RE。
5. 確認三色 LED 的接線及待機呼吸效果。

本專題的單檔版本不需要另外新增其他 `.c` 檔案。

### 網頁執行

使用支援 Web Serial API 的桌面版 Chrome 或 Edge。

可透過 HTTPS 網頁使用，或在網頁資料夾執行：

`python -m http.server 8000`

接著在瀏覽器開啟：

http://localhost:8000/

這是本機開發網址，需先在自己的電腦啟動網站伺服器。

### STM32 連線驗證

1. 將 STM32 開發板連接至電腦。
2. 關閉其他占用 COM Port 的序列埠工具。
3. 在網頁選擇 STM32 連線模式。
4. 點擊「選擇 COM Port」。
5. 選取 ST-LINK Virtual COM Port。
6. 網頁送出 `HELLO`。
7. 確認收到 `TYPERUSH READY`。
8. 測試實體 START、STOP 按鍵與 LED 控制功能。

## 十、GitHub Pages 部署

專案網頁使用 HTML、CSS、JavaScript，可以部署至 GitHub Pages。

1. 建立 GitHub Repository。
2. 上傳網頁程式，確保根目錄包含 `index.html`。
3. 進入 Settings → Pages。
4. 選擇從 `main` 分支的根目錄發布。
5. 使用 GitHub 提供的 HTTPS 網址開啟遊戲。

GitHub Pages 只負責提供網頁，STM32 必須接在操作網站的電腦上，並由該電腦的瀏覽器授權存取 COM Port。

## 十一、測試狀態

**已完成：**

- 網頁遊戲基本流程
- 隨機單字、倒數與計分邏輯
- PWM 呼吸燈程式
- UART 指令與握手功能程式
- 瀏覽器與模擬序列埠測試
- STM32 程式本地語法及模擬測試

**尚待驗證：**

- STM32 實體板與網站的完整 UART 握手
- PB0、PB1 實體按鍵事件的完整連動
- 網頁控制實體 PWM LED 的動作

目前不將模擬測試視為實體板連線成功。

## 十二、專題總結

TypeRush 結合微處理機周邊控制與網頁互動，將傳統英文打字練習改造成具有實體燈光回饋的反應遊戲。

透過本專題，可以練習 GPIO、PWM、UART、FSM、Non-blocking 及軟體消彈跳等技術，並理解 STM32 與電腦之間的資料交換方式。

---

**課程範圍：** Microprocessor Lab 1～Lab 5  
**硬體平台：** STM32 NUCLEO-G474RE  
**專題名稱：** TypeRush — STM32 智慧打字反應遊戲

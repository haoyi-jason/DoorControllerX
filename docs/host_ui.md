# 目標: 建立 .NET MAUI Host (Windows + Android)

## 參考
- `D:\SourceRespo\AT32_WS\AT32F413RCT7_WorkBench_WDAQ\host`
- `D:\SourceRespo\AT32_WS\DoorControllerX\host\DoorControllerXHost`
- 參數(DF) 可以單一或批次讀寫, 並以CSV格式儲存/載入

## 平台策略
- Host UI 主方案採用 `.NET MAUI`
- 目標平台: `Windows` + `Android`
- 通訊方式採用 `Bluetooth Serial` 裝置
- 現有 `DoorControllerXHost` WinForms 專案作為通訊驗證原型, 不作為最終跨平台 UI

## 架構分層

### 1. Core 層
用途: 不依賴平台, 可同時被 Windows 與 Android 共用

建議內容:
- Protocol codec
- Frame parser
- DF/LD ID 定義
- Door state / error code / blocked source state 轉換
- Polling service
- ViewModel

建議專案:
- `host/DoorControllerXHost.Core`

### 2. Transport 抽象層
用途: 定義通訊介面, 不綁定 Win32 COM 或 Android API

建議介面:
- `ITransportClient`
- `ConnectAsync()`
- `DisconnectAsync()`
- `SendAsync()`
- `ReceiveAsync()`
- `QueryAsync()`

備註:
- Protocol 不應直接依賴 `SerialPort`
- MAUI ViewModel 只依賴 transport abstraction

### 3. Platform Transport 層
用途: 實作各平台 Bluetooth Serial 連線

Windows:
- 若 BT Serial 裝置映射為 `COMx`, 可直接沿用 serial port transport
- 可先採用 `System.IO.Ports`

Android:
- 需使用 Android Bluetooth SPP / RFCOMM
- 不使用 `System.IO.Ports`
- 需處理配對裝置列舉、權限、連線狀態、socket stream read/write

建議專案:
- `host/DoorControllerXHost.Maui`
- `Platforms/Windows/` 實作 Windows transport
- `Platforms/Android/` 實作 Android BT transport

## Bluetooth Serial 規劃

### Windows
- 若模組已建立虛擬 COM port, Host 可直接選取 `COMx`
- 對 UI 而言仍呈現為 Bluetooth 裝置, 但底層 transport 可走 serial port

### Android
- 採用 Bluetooth Classic SPP
- 典型流程:
	1. 掃描/列出已配對裝置
	2. 使用裝置 MAC Address 建立 RFCOMM socket
	3. 打開 input/output stream
	4. 以 byte stream 收發 UART Binary Protocol frame

### Android 權限
Android 12+ 需要至少考慮:
- `BLUETOOTH_CONNECT`
- `BLUETOOTH_SCAN`

若不做掃描、只使用已配對裝置清單, 仍需 `BLUETOOTH_CONNECT`。

## MAUI UI 建議頁面

### 1. ConnectionPage
- 選擇平台裝置
- Windows: COM port list
- Android: paired BT device list
- 連線 / 中斷 / 顯示目前連線狀態

### 2. DashboardPage
- `LD_SYS_STATE`
- `LD_M1_STATE`
- `LD_M2_STATE`
- `LD_ERROR_CODE`
- `LD_OPERATION_TIME_MS`

### 3. DiagnosticsPage
- `LD_BLOCK_COUNT`
- `LD_BLOCK_RETRY_COUNT`
- `LD_BLOCK_SOURCE_STATE`
- `LD_LOCK_RETRY_COUNT`
- `LD_M1_PWM`
- `LD_M2_PWM`
- `LD_M1_POS`
- `LD_M2_POS`

### 4. ParametersPage
- DF_ 參數單筆讀寫
- 批次讀取/寫入
- CSV 匯入/匯出

## 資料刷新策略
- 連線成功後建立 polling loop
- 快速資料: `100~250 ms`
- 參數資料: 使用者操作時才讀寫
- UI 更新需與 transport 執行緒分離

建議輪詢分組:
- Fast group: `LD_SYS_STATE`, `LD_M1_STATE`, `LD_M2_STATE`, `LD_ERROR_CODE`, `LD_OPERATION_TIME_MS`
- Diagnostic group: `LD_BLOCK_COUNT`, `LD_BLOCK_RETRY_COUNT`, `LD_BLOCK_SOURCE_STATE`, `LD_LOCK_RETRY_COUNT`
- Motion group: `LD_M1_POS`, `LD_M2_POS`, `LD_M1_PWM`, `LD_M2_PWM`

## Core 重構建議
現有 WinForms 原型中, 以下邏輯適合搬到共用 Core:
- `Protocol/DoorControllerProtocol.cs`
- `Models/LiveDataIds.cs`
- frame / command builder
- live data formatter

以下邏輯不應直接搬到 Core:
- `System.IO.Ports` 相關程式
- WinForms 控制項事件
- Android Bluetooth API 呼叫

## 建議專案結構
```text
host/
	DoorControllerXHost.Core/
		Protocol/
		Models/
		Services/
		Abstractions/
	DoorControllerXHost.Maui/
		Views/
		ViewModels/
		Platforms/Windows/
		Platforms/Android/
```

## 開發順序
1. 先抽出 `Core` 專案
2. 定義 `ITransportClient`
3. 保留 Windows serial transport 先跑通
4. 再補 Android Bluetooth SPP transport
5. 最後再把 UI 從 WinForms 遷移到 MAUI

## 風險
- Android Bluetooth 權限與背景行為限制較多
- 不同 BT Serial 模組對 SPP 穩定性有差異
- Windows 與 Android 的裝置發現流程不同, UI 不能完全共用同一套細節
- MAUI 跨平台 UI 可共用, 但 transport 一定要平台分離

## UART Binary Protocol
- Frame format: `[STX=0xAA][LEN][CMD][DATA...][CRC][ETX=0x55]`
- CRC: `LEN ^ CMD ^ DATA[0] ^ ...`
- `CMD_READ_PARAM = 0x01`: DATA=`[param_id]`, response=`ACK + value(4 bytes, big-endian)`
- `CMD_WRITE_PARAM = 0x02`: DATA=`[param_id][value(4 bytes, big-endian)]`
- `CMD_READ_LIVE  = 0x03`: DATA=`[live_id]`, response=`ACK + value(4 bytes, big-endian)`
- `CMD_WRITE_LIVE = 0x04`: DATA=`[live_id][value(4 bytes, big-endian)]`
- `CMD_ACK = 0xF0`, `CMD_NAK = 0xF1`

## Live Data IDs
依目前韌體 `database.h` 列舉順序，host 端可直接用以下 ID 呼叫 `CMD_READ_LIVE`:

| ID | Name | 說明 |
| --- | --- | --- |
| 3 | LD_M1_POS | 主門目前角度，單位為度 x 100 |
| 4 | LD_M2_POS | 副門目前角度，單位為度 x 100 |
| 14 | LD_BLOCK_COUNT | 單次動作內累計阻擋偵測次數 |
| 15 | LD_BLOCK_RETRY_COUNT | 進入 BLOCKED 後自動重試次數 |
| 16 | LD_BLOCK_SOURCE_STATE | 最後一次進入 BLOCKED 的來源狀態 |
| 17 | LD_ERROR_CODE | 錯誤代碼 |
| 18 | LD_OPEN_COUNT | 開門累計次數 |
| 19 | LD_CLOSE_COUNT | 關門累計次數 |
| 20 | LD_LOCK_RETRY_COUNT | 電鎖重試次數 |
| 21 | LD_OPERATION_TIME_MS | 目前動作已執行時間(ms) |

## Blocked Source State Values
`LD_BLOCK_SOURCE_STATE` 對應 `sys_state_t`:

| Value | Meaning |
| --- | --- |
| 1 | SYS_STATE_WAIT |
| 2 | SYS_STATE_OPENING |
| 4 | SYS_STATE_CLOSING |

說明:
- 韌體在待機或新動作開始前，會先把 `LD_BLOCK_SOURCE_STATE` 清為 `SYS_STATE_WAIT`。
- 若門在開啟過程阻擋，值為 `SYS_STATE_OPENING`。
- 若門在關閉過程阻擋，值為 `SYS_STATE_CLOSING`。

## Host UI 建議
- 主畫面固定輪詢 `LD_SYS_STATE`, `LD_M1_STATE`, `LD_M2_STATE`, `LD_M1_POS`, `LD_M2_POS`, `LD_ERROR_CODE`
- 診斷頁增加 `LD_BLOCK_COUNT`, `LD_BLOCK_RETRY_COUNT`, `LD_BLOCK_SOURCE_STATE`, `LD_LOCK_RETRY_COUNT`, `LD_OPERATION_TIME_MS`
- `LD_BLOCK_SOURCE_STATE` 建議顯示文字，不直接顯示數值
- `LD_OPERATION_TIME_MS` 建議每 100 ms 刷新即可

## 歸零誤差參數
- `DF_M1_ZERO_ERROR` 與 `DF_M2_ZERO_ERROR` 已存在於韌體與 Host 參數頁，可由使用者設定上電歸零允許誤差。
- 若上電回原點後 POT 角度落在此誤差範圍內，韌體會將當前角度設為零點；超出範圍則報錯並禁止操作。

## 上電自檢錯誤碼 (LD_ERROR_CODE)

| Code | Meaning |
| --- | --- |
| 0 | NONE |
| 1 | BLOCK_RETRY_EXCEEDED |
| 10 | STARTUP_UNLOCK_CHECK_FAIL |
| 11 | STARTUP_M2_DIRECTION_FAIL |
| 12 | STARTUP_M2_HOME_TIMEOUT |
| 13 | STARTUP_M2_HOME_SWITCH_FAIL |
| 14 | STARTUP_M1_DIRECTION_FAIL |
| 15 | STARTUP_M1_HOME_TIMEOUT |
| 16 | STARTUP_M1_HOME_SWITCH_FAIL |
| 17 | STARTUP_LOCK_CHECK_FAIL |

## 即時圖表
- 使用FastLine
- 可由LD/DF選取要顯示的資料
- 每秒一個資料點
- 横軸為時間
- 可設定横軸顯示長度
- 具備Y1/Y2, 可自定邊界值或自動設定

## 待辦事項

- [ ] 在 Host 畫面新增上電自檢結果摘要區塊（顯示最近一次檢查狀態）
- [ ] 實機驗證 DIP 組合（有無電鎖 / 單門雙門）在上電流程的分支行為
- [ ] 確認 M1/M2 方向一致性檢查的脈衝時間與角度閾值在機構端穩定
- [ ] 依實測結果微調上電回原點逾時參數
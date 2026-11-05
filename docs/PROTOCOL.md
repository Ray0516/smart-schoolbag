# 通訊協定交接

本文件後半保留歷史 v1 與 1.5 增補，若有衝突以雙端目前來源及「1.5 增補」優先。歷史段落的 1.4.8 版本、只同步今天、五次重連限制不代表目前完整行為；目前含七日 PLAN_DAY/DAYREQ/DAYCHECK、自動重試、NOTE 批次保存／讀回。

主要端點：GET /status（純文字狀態快照）、POST /control（UTF-8 text/plain、LF 分行、TAB 分欄）。原型 AP 未提供額外 HTTP 認證，密碼由裝置擁有者私下設定，僅適合可信近距離環境。

TIME、PLAN、PLAN_DAY、DAYREQ、DAYCHECK 的解析欄位與 ACK 請查 EPaperTestApp.h handleBleCommand；手機 _syncWifi 是傳送順序範本。不要自行猜遮罩位元或日期單位，先比對雙端。修改協定時兩端一起測試，並更新本文件。

以下為既有逐行協定與相容欄位資料：

# 智慧電子書包 BLE 同步協定 v1

## GATT

| 用途 | UUID | 權限 |
|---|---|---|
| Service | `8c3a0001-9c4a-4f2d-a2e1-6a4b53424701` | — |
| 控制特徵值 | `8c3a0002-9c4a-4f2d-a2e1-6a4b53424701` | Write、Write Without Response |
| 事件特徵值 | `8c3a0003-9c4a-4f2d-a2e1-6a4b53424701` | Read、Notify |

廣播名稱為 `SmartSchoolbag`。韌體 BLE 回呼只將收到的資料片段排入 FreeRTOS 佇列；NVS、電子紙與 RC522 共用 SPI 的狀態修改在 `poll()` 主流程執行，避免 BLE 工作直接碰共用硬體。

目前 App 以 ESP32 本地 Wi-Fi AP/HTTP 作為主要同步方式；BLE GATT v1 保留作為相容傳輸介面。兩者共用同一行文字協定。

## 線路格式

- 每行為 UTF-8，欄位以 TAB 分隔，結尾為 LF。
- App 每次 BLE 寫入最多 18 bytes；韌體將連續片段重組為一行，單行上限 191 bytes。韌體通知同樣以 18-byte 片段傳送，App 先累積到 LF 再解碼 UTF-8，避免預設 MTU 23 時長事件遭截斷或中文字被切壞。
- UID 空值用 `-` 表示；項目索引從 0 開始。
- ESP32 與 App 最多支援 20 個自訂物品；今日清單會依 `REQUIRED` 顯示今天需要攜帶的項目。電子紙每頁顯示五項，可翻頁。

### App → ESP32

| 命令 | 內容與作用 |
|---|---|
| `GET` | 回傳 `READY`、版本、20 個物品槽位、課表文字、便利貼與 GPS 狀態；不傳送電池資訊。 |
| `SYNC_BEGIN<TAB>n` | 開始暫存手機清單，`n` 範圍 0～20；完整資料未到齊前不改動目前清單。 |
| `ITEM<TAB>i<TAB>name<TAB>uid-or--<TAB>checked` | 同步項目名稱、RFID UID 與勾選狀態。 |
| `REQUIRED<TAB>i<TAB>0-or-1` | 標記該物品是否屬於今天的課表／行事曆清單；電子紙只列出今日需要的物品。 |
| `SYNC_DONE` | 確認每筆 `ITEM` 與 `REQUIRED` 都收到、UID 無重複後才寫入 NVS；成功才通知 `SYNC_OK` 並要求電子紙重繪。資料不完整或寫入失敗會回報 `ERR`，不套用半份手機清單。 |
| `CHECK<TAB>i<TAB>0-or-1` | 更新一項勾選狀態；已在清單頁時要求局部刷新。 |
| `PAIR<TAB>i` | 切到標籤配對頁，選定項目並等待刷卡。 |
| `UNPAIR<TAB>i` | 移除該項目 UID 並保存至 NVS。 |

### ESP32 → App

事件為 `READY<TAB>1`、`VERSION<TAB>v1.4.8`、`ITEM<TAB>i<TAB>name<TAB>uid-or--<TAB>checked`、`PLAN<TAB>今日課表行事`、`NOTE<TAB>便利貼`、`GPS<TAB>FIX<TAB>sat<TAB>lat<TAB>lon`、`GPS<TAB>NOFIX<TAB>sat`、`TAG<TAB>name<TAB>uid`、`TAG<TAB>UNKNOWN<TAB>uid`、`SYNC_OK`、`PAIR_READY`、`UNPAIR_OK`、`ERR<TAB>UNKNOWN_COMMAND`、`ERR<TAB>LINE_TOO_LONG`、`ERR<TAB>SYNC_COUNT`、`ERR<TAB>SYNC_INCOMPLETE`、`ERR<TAB>SYNC_DUPLICATE_TAG`、`ERR<TAB>NVS_UNAVAILABLE`、`ERR<TAB>NVS_WRITE<TAB>原因`。UID 欄空值必須用 `-`，不可留空造成連續 TAB。NVS 無法保存時不送 `SYNC_OK`；App 顯示同步錯誤，配對／解除配對／自動勾選寫入失敗也回報 `ERR`，並恢復未保存的舊狀態。同步資料先放在 RAM 暫存區，驗證完成後才保存；NVS 寫入失敗時恢復舊 RAM 狀態並嘗試把舊資料寫回 NVS，序列會記錄回滾結果。

`GPS FIX` 才有座標；無定位、逾時或裝置離線不補上舊座標。此協定不提供電池狀態。GATT 通知不包含座標時間戳；App 顯示的是手機收到該筆狀態的本機時間，不能當成衛星時間。

## 資料限制

- 手機 App 為本機資料來源，可管理課表、最多 20 個同步物品與提醒。清單同步後寫入裝置 NVS；名稱、RFID UID、勾選及今日必帶旗標均保存。同步少於 20 項時，剩餘 NVS 槽位明確寫入空名稱，避免刪除的舊項目重開機後復活。
- 課表和行事曆留在手機；每次同步只把「今天需帶哪些物品」旗標與最多 20 筆物品同步到電子紙。手機是名稱／順序來源，只有裝置回報名稱與手機同索引相符時才採用其 RFID UID／勾選狀態，避免連線時以裝置舊資料覆蓋手機改名。
- 手機短暫離線不刪除本機資料；App 前景使用期間最多自動重連五次，每次間隔五秒。Android／iOS 背景持續掃描不在 v1 範圍。
- Wi-Fi 僅連 ESP32 本地 AP 作近距離同步；沒有網際網路上傳、帳號、雲端日誌、家長推播或遠端地圖服務。位置分享偏好只保存在手機，不會產生網路傳輸。



## 1.5 增補

Wi-Fi 與 BLE 共用逐行 tab 分欄協定。HTTP POST /control 必須以 UTF-8 位元組數指定 Content-Length；ESP32 WebServer 此接收路徑不支援 chunked request body。GET /status 是唯讀裝置快照。

SYNC_BEGIN 與 SYNC_DONE 之間，每項 ITEM／REQUIRED 後可送 CATEGORY\tindex\tbook 或 CATEGORY\tindex\titem。分類保存到 NVS；裝置 GET 額外回 ITEM_CATEGORY\tindex\tbook|item。物品索引不因顯示分類排序而改變。

PLAN_DAY、DAYREQ、DAYCHECK 分別保存星期 1～7 的課表與必帶／完成遮罩。ITEM 回報包含末欄星期，避免手機把手動選星期的狀態套到今天。手機 TIME 校時不會清除裝置端的手動星期選擇。

NOTE_BEGIN count、NOTE_ITEM index payload、NOTE_DONE 將整份便利貼寫入 NVS，回 NOTE_OK；GET 回 NOTES_BEGIN count、NOTE_ITEM 與 NOTES_END，手機需讀回比對。payload 為 UTF-8，第一個 ASCII RS (0x1e) 分隔標題與內容，其餘 RS 表示內容換行；裝置顯示時將內容換行視為空白並依行寬折行。payload 合計上限 120 bytes，最多五則。舊單行 payload 當標題保留。

PAIR index 會進入明確配對狀態，回 PAIR_READY。讀到可用標籤後保存並回 ITEM 和 PAIR_OK；已綁定其他物品的 UID 回 ERR TAG_ALREADY_LINKED。配對不會自動改變該物品的當日勾選。手機在 Wi-Fi 配對等待中輪詢裝置快照，等待結束後恢復自動同步。

## 目前來源精確補充

處理函式名為 handleBleLine。TIME 的第二欄是十進位 Unix 秒；時區固定 CST-8（UTC+8）。PLAN_DAY 第二欄為星期 1～7、第三欄為課表文字；DAYREQ/DAYCHECK 第三欄是十六進位遮罩（不要加空白或把它誤當十進位），bit i 對應物品索引 i，只取 20 位。回覆分別 TIME_OK、PLAN_DAY_OK<TAB>weekday、DAYREQ_OK<TAB>weekday、DAYCHECK_OK<TAB>weekday。

SYNC_BEGIN/ITEM/REQUIRED/CATEGORY/SYNC_DONE 只涵蓋物品交易；七日計畫、遮罩與便利貼是之後分開保存，整次同步不是跨所有資料的單一原子交易。PLAN_DAY／PLAN 的 NVS 失敗回滾、NOTE_DONE 多鍵保存中斷的復原仍應列入韌體故障注入測試。不能把這些現有限制描述成完整斷電交易保證。

手動探測（將 <TAB> 替換為真正 tab、<LF> 替換為換行）：

```text
GET<LF>
TIME<TAB>1793836800<LF>
PLAN_DAY<TAB>1<TAB>國文 數學<LF>
DAYREQ<TAB>1<TAB>3<LF>
DAYCHECK<TAB>1<TAB>1<LF>
```

此遮罩例表示星期一物品 0、1 要帶，物品 0 已完成。實際狀態修改需測試資料，不能直接覆蓋使用者既有資料。

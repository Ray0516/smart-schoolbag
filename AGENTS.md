# 給接手 AI／開發代理的指令

所有中文文件與回覆使用臺灣繁體中文。這是實體硬體專案；不要把提案、舊紀錄或圖片當成已驗證能力。

## 必讀順序

1. README.md
2. docs/HANDOFF.md
3. docs/HARDWARE.md
4. docs/FEATURES.md
5. docs/PROTOCOL.md
6. docs/BUILD.md 與 docs/TESTING.md
7. 雙端原始碼及現有測試

## 不可破壞的約束

- 韌體來源以 firmware/EPaperTest 為唯一目前基線；手機以 mobile/SmartSchoolbagApp 為準。不得混用過往版本。
- GPIO 對照固定：EPD DIN11/CLK12/CS10/DC43/RST6/BUSY7；RC522 MOSI11/SCK12/MISO13/SS9/RST8；GPS TXD→44；按鍵上14/下5/左2/右1/OK4。
- GPIO14 是此 Nologo 版底部焊盤。保留 USB19/20；GPIO0/3/45/46 不接外部按鈕；GPIO3 僅經已核對板載 VBUS 電路唯讀偵測。
- EPD 與 RC522 共用 SPI、片選獨立。UART0 不可佔用 EPD DC43；GPS 接 UART1 RX44。
- 不得把 RC522 或 ESP32 GPIO 接 5V。GPS 本案 5V 是載板供電，不能泛化成裸晶片可接 5V。
- 面板控制器尚待核實，不得憑 2.9 吋或 296×128 自動改驅動或試危險 LUT。
- 電池 UI 已刪除。沒有外接負載開關時，不得宣稱 deep sleep 已切斷外設供電。
- 左鍵逐層返回；所有主選單直向。保留星期選擇與離線資料。
- 重複刷已完成物品不重畫、不反覆勾選。配對與清單完成是不同狀態；不自動覆寫其他物品的 UID。
- 同步不能只顯示「已連線」就宣稱成功；便利貼必須保存後讀回相符。HTTP 必須帶 UTF-8 位元組 Content-Length，不用 chunked body。
- 不做文字左右移動畫。切頁全刷，同頁局刷策略以目前來源為準；不要重啟已放棄的分區動畫實驗。
- 實體按鍵、畫面、GPS fix、電流、USB 喚醒都要真實操作證據。不能以 mock、序列「ONLINE」或編譯通過代替。
- 保留 SharedPreferences 與 NVS 相容性；重構前先補資料遷移測試。不要清空裝置快閃或手機資料排錯。

## 工作方法

先列出實際重現的問題與受影響來源，做最小必要修改。同步變更同時更新兩端、協定文件與測試。先 flutter analyze、flutter test，再建置；韌體使用固定 FQBN 編譯。編譯、燒錄、安裝分開；沒有該輪使用者明確要求，不執行燒錄、安裝或清除資料。

完成後記錄版本、修改、指令、結果與未驗收項。不得把未接線的蜂鳴器、雲端、拍照辨識或 GPS 地圖服務寫成已完成。不要要求使用者提供密碼或把私人資料加入 Git。


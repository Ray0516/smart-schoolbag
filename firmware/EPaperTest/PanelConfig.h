#pragma once

// 這是手動選擇，程式不會辨識面板型號，也不會自動輪試驅動。
// 1：黑白 Waveshare 2.9 V2，GDEM029T94／SSD1680（預設假設）
// 2：黑白舊版，GDEH029A1
// 3：紅黑白，GDEH029Z13／UC8151D
// 4：紅黑白，GDEM029C90／SSD1680
// 5：紅黑白舊版，GDEW029Z10／UC8151
// 6：GDEM029T94 專用四階灰階；另需安裝 GxEPD2_4G。
// 同為 296×128 或標示 V2/V3/V4，仍不保證面板相同；以面板型號核對。
#ifndef EPAPER_PROFILE
#define EPAPER_PROFILE 1
#endif

// 未知型號不套用灰階波形；確認 GDEM029T94 後才改成 1。
#ifndef CONFIRMED_GDEM029T94
#define CONFIRMED_GDEM029T94 0
#endif

// 選單／清單互動不額外等待；面板忙碌期間由 BUSY 訊號自然限速。
constexpr unsigned long REFRESH_INTERVAL_MS = 0UL;
constexpr unsigned long OPERATION_TIMEOUT_MS = 60000UL;

constexpr int EPD_CS = 10;
constexpr int EPD_DC = 43;   // 主板外圍 TX
constexpr int EPD_RST = 6;
constexpr int EPD_BUSY = 7;
constexpr int SPI_MOSI_PIN = 11;
constexpr int SPI_SCK_PIN = 12;
constexpr int SPI_MISO_PIN = 13;
constexpr int RFID_CS_PIN = 9;
constexpr int RFID_RST_PIN = 8;

// 設定頁可選 3、4 或 5 次整頁左至右四區局刷，再直接標準全波形整頁刷新。
constexpr uint8_t UI_PARTIAL_SWEEPS_BEFORE_DIRECT_FULL_REFRESH = 3;
constexpr unsigned long CARD_REFRESH_INTERVAL_MS = 1000UL;
constexpr unsigned long RFID_POLL_INTERVAL_MS = 50UL;
constexpr unsigned long RFID_RETRY_INTERVAL_MS = 2000UL;

constexpr int GPS_RX_PIN = 44;  // 外圍 RX，接 GPS TXD；不配置 GPS TX 輸出。
// Nologo SuperMini 原理圖將 GPIO3 經板載分壓器接到 USB VBUS 偵測。
// 僅作輸入與 USB 插入深睡喚醒；不可外接模組或設成輸出。複製板需先核對電路。
constexpr int USB_VBUS_SENSE_PIN = 3;
// GPIO15 is an underside solder pad, reserved for optional active-HIGH load-switch enables.
// Keep disabled until separate 3.3V and 5V peripheral rails are routed through load switches.
#define PERIPHERAL_POWER_GATE_ENABLED 0
constexpr int PERIPHERAL_POWER_GATE_PIN = 15;
constexpr unsigned long GPS_SCREEN_INTERVAL_MS = 60000UL;
constexpr unsigned long GPS_BAUD_SCAN_INTERVAL_MS = 6500UL;
constexpr unsigned long GPS_FIX_MAX_AGE_MS = 5000UL;
constexpr unsigned long GPS_STREAM_MAX_AGE_MS = 10000UL;


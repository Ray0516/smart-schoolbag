#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <atomic>
#include <algorithm>
#include <string>
#include <time.h>
#include <sys/time.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "PanelConfig.h"
#include "ChineseText.h"
#include "ChineseGlyphs.h"
#include "RfidText.h"
#include "src/MFRC522/MFRC522.h"
#include "GpsSupport.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "請選擇 ESP32S3 Dev Module。"
#endif
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || !ARDUINO_USB_CDC_ON_BOOT
#error "請將 USB CDC On Boot 設為 Enabled；GPIO43 已用於電子紙 DC。"
#endif

#if EPAPER_PROFILE == 6
  #if !CONFIRMED_GDEM029T94
    #error "灰階波形僅供已確認的 GDEM029T94；請先核對型號，再設定 CONFIRMED_GDEM029T94=1。"
  #endif
  #include <GxEPD2_4G_4G.h>
  using BasePanel = GxEPD2_290_T94;
#elif EPAPER_PROFILE == 1 || EPAPER_PROFILE == 2
  #include <GxEPD2_BW.h>
  #if EPAPER_PROFILE == 1
    using BasePanel = GxEPD2_290_T94_V2;
  #else
    using BasePanel = GxEPD2_290;
  #endif
#elif EPAPER_PROFILE >= 3 && EPAPER_PROFILE <= 5
  #include <GxEPD2_3C.h>
  #if EPAPER_PROFILE == 3
    using BasePanel = GxEPD2_290_Z13c;
  #elif EPAPER_PROFILE == 4
    using BasePanel = GxEPD2_290_C90c;
  #else
    using BasePanel = GxEPD2_290c;
  #endif
#else
  #error "EPAPER_PROFILE 必須為 1～6。"
#endif

namespace epaper_test {

// Setting toggle maps every logical foreground/background pixel in the UI.
bool blackWhiteReflected = false;
inline uint16_t uiBlack() { return blackWhiteReflected ? GxEPD_WHITE : GxEPD_BLACK; }
inline uint16_t uiWhite() { return blackWhiteReflected ? GxEPD_BLACK : GxEPD_WHITE; }

// 延長函式庫的內部等待上限，先由下方的 60 秒防護停止整個工作，
// 避免函式庫 BUSY 逾時後繼續送出下一組面板指令。
class CheckedPanel : public BasePanel {
 public:
  CheckedPanel(int cs, int dc, int rst, int busy)
      : BasePanel(cs, dc, rst, busy) {
    _busy_timeout = 120000000UL;  // 此欄位單位為微秒。
  }
};

#if EPAPER_PROFILE == 6
GxEPD2_4G_4G<CheckedPanel, BasePanel::HEIGHT> display(
    CheckedPanel(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
constexpr bool HAS_RED = false;
constexpr bool NATIVE_GREY = true;
constexpr const char* PROFILE_NAME = "GDEM029T94 / 4 GREY";
#elif EPAPER_PROFILE == 1 || EPAPER_PROFILE == 2
GxEPD2_BW<CheckedPanel, BasePanel::HEIGHT> display(
    CheckedPanel(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
constexpr bool HAS_RED = false;
constexpr bool NATIVE_GREY = false;
#if EPAPER_PROFILE == 1
constexpr const char* PROFILE_NAME = "BW V2 / GDEM029T94";
#else
constexpr const char* PROFILE_NAME = "BW / GDEH029A1";
#endif
#else
GxEPD2_3C<CheckedPanel, BasePanel::HEIGHT> display(
    CheckedPanel(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
constexpr bool HAS_RED = true;
constexpr bool NATIVE_GREY = false;
#if EPAPER_PROFILE == 3
constexpr const char* PROFILE_NAME = "BWR / GDEH029Z13";
#elif EPAPER_PROFILE == 4
constexpr const char* PROFILE_NAME = "BWR / GDEM029C90";
#else
constexpr const char* PROFILE_NAME = "BWR / GDEW029Z10";
#endif
#endif

enum class Frame : uint8_t { Pattern, White, Black, Grey, Text, Card, ReaderReady, ReaderError, GPS, ButtonTest, Menu, Checklist, TagSetup, SystemInfo, Schedule, Notes, Status, Settings, Standby };
enum class State : uint8_t { Idle, Running, Done, Fault };
enum class UiPage : uint8_t { Menu, Detail };
UiPage uiPage = UiPage::Menu;
uint8_t menuSelection = 0;
bool uiDirty = true;
bool uiFullRefresh = true;
struct UiWindow {
  int16_t x, y, w, h;
  bool valid;
  UiWindow() : x(0), y(0), w(0), h(0), valid(false) {}
  UiWindow(int16_t xValue, int16_t yValue, int16_t wValue, int16_t hValue, bool isValid)
      : x(xValue), y(yValue), w(wValue), h(hValue), valid(isValid) {}
};
constexpr uint8_t MAX_UI_WINDOWS = 6;
UiWindow pendingUiWindows[MAX_UI_WINDOWS];
UiWindow activeUiWindows[MAX_UI_WINDOWS];
uint8_t pendingUiWindowCount = 0;
uint8_t activeUiWindowCount = 0;
bool activeUiFullRefresh = true;
constexpr uint8_t MENU_ITEM_COUNT = 5;
constexpr const char* APP_VERSION = "v1.5.0";
const char* const MENU_ITEMS[MENU_ITEM_COUNT] = {"今日要帶", "課表與行事", "便利貼", "GPS狀態", "設定"};
uint8_t uiRefreshSweepsBeforeFull = UI_PARTIAL_SWEEPS_BEFORE_DIRECT_FULL_REFRESH;
uint8_t settingsRefreshSelection = UI_PARTIAL_SWEEPS_BEFORE_DIRECT_FULL_REFRESH - 3;
uint8_t settingsMenuSelection = UI_PARTIAL_SWEEPS_BEFORE_DIRECT_FULL_REFRESH - 3;
bool hasLastAppScreenFrame = false;
Frame lastAppScreenFrame = Frame::Menu;
constexpr uint8_t CHECK_ITEM_COUNT = 20;
char CHECK_ITEMS[CHECK_ITEM_COUNT][64] = {"國文課本", "英文課本", "自然課本", "作業本", "水壺"};
char linkedTagUid[CHECK_ITEM_COUNT][30] = {};
bool itemChecked[CHECK_ITEM_COUNT] = {};
bool itemIsBook[CHECK_ITEM_COUNT] = {true, true, true, false, false};
bool itemRequiredToday[CHECK_ITEM_COUNT] = {true, true, true, true, true};
constexpr uint8_t WEEKDAY_COUNT = 7;
uint8_t selectedWeekday = 1;  // ISO weekday: Monday=1, Sunday=7.
bool manualWeekdayOverride = false;
uint32_t dayCheckedMasks[WEEKDAY_COUNT] = {};
uint32_t dayRequiredMasks[WEEKDAY_COUNT] = {};
char weekdayPlans[WEEKDAY_COUNT][160] = {};
uint8_t activeChecklistItems[CHECK_ITEM_COUNT] = {0, 1, 2, 3, 4};
uint8_t activeChecklistCount = CHECK_ITEM_COUNT;
uint8_t checklistSelection = 0;
constexpr uint8_t CHECKLIST_VISIBLE_ROWS = 5;
bool pairingArmed = false;
char lastTagMessage[80] = "尚未刷卡";
char todaySchedule[160] = "尚未同步今日課表";
constexpr uint8_t MAX_STICKY_NOTES = 5;
constexpr size_t STICKY_NOTE_BYTES = 121;
char stickyNotes[MAX_STICKY_NOTES][STICKY_NOTE_BYTES] = {};
char pendingStickyNotes[MAX_STICKY_NOTES][STICKY_NOTE_BYTES] = {};
bool pendingStickyNoteReceived[MAX_STICKY_NOTES] = {};
uint8_t stickyNoteCount = 0;
uint8_t pendingStickyNoteCount = 0;
uint8_t stickyNoteSelection = 0;
bool stickyNoteDetailOpen = false;
bool checklistDayPickerOpen = true;
uint8_t weekdayPickerSelection = 1;
nvs_handle_t appNvsHandle = 0;
bool preferencesReady = false;
std::atomic<State> state{State::Idle};
Frame requestedFrame = Frame::Pattern;
uint32_t operationStartedMs = 0;
uint32_t completedMs = 0;
uint32_t busyPolls = 0;
bool hasCompletedFrame = false;
bool sequenceActive = false;
uint8_t sequenceStep = 0;
uint32_t lastCountdownMs = 0;
bool faultReported = false;
uint32_t lastHeartbeatMs = 0;
// Button order: UP, DOWN, LEFT, RIGHT, OK. GPIO3 is input-only for board VBUS sense.
constexpr uint8_t BUTTON_PINS[] = {14, 5, 2, 1, 4};
constexpr const char* BUTTON_NAMES[] = {"UP", "DOWN", "LEFT", "RIGHT", "OK"};
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
// Debounced key handling plus row-sized partial windows refreshes outlines and row text together.
constexpr uint32_t BUTTON_REFRESH_GAP_MS = 150;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 1500;
RTC_DATA_ATTR bool standbyAwaitRelease = false;
bool suppressOkUntilRelease = false;
uint8_t autoStandbyMinutes = 0;
uint32_t lastUserActivityMs = 0;
struct ButtonInput {
  bool rawLevel = HIGH;
  bool stableLevel = HIGH;
  uint32_t changedAtMs = 0;
  uint32_t pressedAtMs = 0;
  bool pressActive = false;
  bool actionHandled = false;
};
ButtonInput buttons[5];
uint32_t buttonPressCounts[5] = {};
uint32_t buttonEventSequence = 0;
uint32_t buttonTestGeneration = 0;
uint32_t buttonFrameGeneration = 0;
uint32_t displayedButtonTestGeneration = 0;
uint8_t latestButtonIndex = 0;
bool hasButtonEvent = false;
bool buttonTestExitArmed = false;
uint32_t lastButtonRefreshMs = 0;
bool standbyRequested = false;
bool standbyFrameStarted = false;
bool standbyFrameComplete = false;
// Preserve every debounced key action while the e-paper controller is busy.
// A one-slot queue made quick taps appear to be ignored during long refreshes.
constexpr uint8_t BUTTON_ACTION_QUEUE_CAPACITY = 32;
uint8_t queuedButtonActions[BUTTON_ACTION_QUEUE_CAPACITY] = {};
uint8_t queuedButtonHead = 0;
uint8_t queuedButtonCount = 0;
std::atomic<bool> uiNavigationInterrupt{false};
bool queuedStandbyRequest = false;
bool uiDiagnosticActive = false;
uint8_t uiDiagnosticStep = 0;
bool uiDiagnosticOriginalChecked = false;

// Ordinary changed-window partial refreshes, then a full-waveform cleanup.
MFRC522 reader(RFID_CS_PIN, RFID_RST_PIN);
struct CardInfo {
  char uid[30] = {};
  uint8_t uidSize = 0;
  MFRC522::PICC_Type type = MFRC522::PICC_TYPE_UNKNOWN;
  uint32_t count = 0;
  uint32_t readAtMs = 0;
};
CardInfo latestCard;
CardInfo displayedCard;
gps_support::View displayedGPS;
uint32_t lastGpsScreenMs = 0;
uint32_t lastStatusRefreshMs = 0;
bool readerOnline = false;
bool readerRFEnabled = true;
uint8_t readerVersion = 0;
uint8_t readerBadChecks = 0;
bool cardDisplayEnabled = false;
bool cardPending = false;
bool readerStatusPending = false;
uint32_t lastReaderPollMs = 0;
uint32_t lastReaderCheckMs = 0;
uint32_t lastCardErrorMs = 0;
// 以下狀態只在序列化的電子紙工作中使用。
bool panelPrepared = false;
bool lastFramePartialWindowRefresh = false;
bool lastFrameStandardCleanupAfterPartials = false;
uint8_t partialUpdatesSinceFullRefresh = 0;
bool forceStandardRefreshNext = false;
void markUiFullRefresh();
void markUiWindow(int16_t x, int16_t y, int16_t w, int16_t h);

int8_t checklistPosition(uint8_t itemIndex) {
  for (uint8_t pos = 0; pos < activeChecklistCount; ++pos)
    if (activeChecklistItems[pos] == itemIndex) return static_cast<int8_t>(pos);
  return -1;
}

uint8_t checklistFirstVisiblePosition() {
  const int8_t selected = checklistPosition(checklistSelection);
  if (selected < 0) return 0;
  return static_cast<uint8_t>((selected / CHECKLIST_VISIBLE_ROWS) * CHECKLIST_VISIBLE_ROWS);
}

int8_t checklistVisibleRow(uint8_t itemIndex) {
  const int8_t position = checklistPosition(itemIndex);
  const uint8_t first = checklistFirstVisiblePosition();
  if (position < first || position >= first + CHECKLIST_VISIBLE_ROWS) return -1;
  return static_cast<int8_t>(position - first);
}

void rebuildActiveChecklist() {
  activeChecklistCount = 0;
  for (uint8_t category = 0; category < 2; ++category)
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i)
      if (CHECK_ITEMS[i][0] && itemRequiredToday[i] && itemIsBook[i] == (category == 0))
        activeChecklistItems[activeChecklistCount++] = i;
  if (activeChecklistCount && checklistPosition(checklistSelection) < 0)
    checklistSelection = activeChecklistItems[0];
}

uint8_t moveChecklistSelection(uint8_t current, int8_t direction) {
  if (!activeChecklistCount) return current;
  int8_t position = checklistPosition(current);
  if (position < 0) return activeChecklistItems[0];
  int16_t next = position + direction;
  if (next < 0) next = activeChecklistCount - 1;
  if (next >= activeChecklistCount) next = 0;
  return activeChecklistItems[next];
}

void jumpChecklistPage(int8_t direction) {
  if (activeChecklistCount <= CHECKLIST_VISIBLE_ROWS) return;
  const int8_t position = checklistPosition(checklistSelection);
  if (position < 0) return;
  const uint8_t pageStart = (position / CHECKLIST_VISIBLE_ROWS) * CHECKLIST_VISIBLE_ROWS;
  const uint8_t pageCount = (activeChecklistCount + CHECKLIST_VISIBLE_ROWS - 1) /
                            CHECKLIST_VISIBLE_ROWS;
  int16_t nextPage = pageStart / CHECKLIST_VISIBLE_ROWS + direction;
  if (nextPage < 0) nextPage = pageCount - 1;
  if (nextPage >= pageCount) nextPage = 0;
  checklistSelection = activeChecklistItems[nextPage * CHECKLIST_VISIBLE_ROWS];
  markUiFullRefresh();
  Serial.printf("[UI] 清單跳至第 %u/%u 頁。\n",
                static_cast<unsigned>(nextPage + 1), static_cast<unsigned>(pageCount));
}

// BLE GATT protocol shared with SmartSchoolbagApp. BLE callbacks only enqueue bytes;
// NVS/SPI/display state is changed later in poll() to keep shared peripherals serialized.
constexpr char BLE_SERVICE_UUID[] = "8c3a0001-9c4a-4f2d-a2e1-6a4b53424701";
constexpr char BLE_CONTROL_UUID[] = "8c3a0002-9c4a-4f2d-a2e1-6a4b53424701";
constexpr char BLE_EVENT_UUID[] = "8c3a0003-9c4a-4f2d-a2e1-6a4b53424701";
BLEServer* bleServer = nullptr;
BLECharacteristic* bleEventCharacteristic = nullptr;
BLECharacteristic* bleControlCharacteristic = nullptr;
WebServer wifiHttpServer(80);
constexpr char WIFI_AP_SSID[] = "SmartSchoolbag";
#if __has_include("LocalWifiConfig.h")
#include "LocalWifiConfig.h"
#else
#error "Copy LocalWifiConfig.example.h to LocalWifiConfig.h and set a private AP password before building."
#endif
constexpr char WIFI_AP_PASSWORD[] = SCHOOLBAG_WIFI_PASSWORD;
static_assert(sizeof(WIFI_AP_PASSWORD) >= 9 && sizeof(WIFI_AP_PASSWORD) <= 64,
              "AP password must contain 8 to 63 bytes.");
bool wifiHttpCapture = false;
char wifiHttpResponse[4096] = {};
QueueHandle_t bleRxQueue = nullptr;
struct BleChunk { uint8_t length; uint8_t bytes[20]; };
char bleLineBuffer[192] = {};
uint16_t bleLineLength = 0;
bool bleDiscardingOversizeLine = false;
bool bleSyncInProgress = false;
uint8_t bleSyncExpectedItems = 0;
char bleSyncNames[CHECK_ITEM_COUNT][sizeof(CHECK_ITEMS[0])] = {};
char bleSyncTags[CHECK_ITEM_COUNT][sizeof(linkedTagUid[0])] = {};
bool bleSyncChecked[CHECK_ITEM_COUNT] = {};
bool bleSyncIsBook[CHECK_ITEM_COUNT] = {};
bool bleSyncRequired[CHECK_ITEM_COUNT] = {};
bool bleSyncItemReceived[CHECK_ITEM_COUNT] = {};
bool bleSyncRequiredReceived[CHECK_ITEM_COUNT] = {};
uint32_t lastBleGpsNotifyMs = 0;

void bleNotify(const char* text) {
  if (wifiHttpCapture) {
    size_t used = strlen(wifiHttpResponse);
    size_t available = sizeof(wifiHttpResponse) - used;
    if (available > 2) {
      int written = snprintf(wifiHttpResponse + used, available, "%s\n", text);
      if (written < 0 || static_cast<size_t>(written) >= available)
        wifiHttpResponse[sizeof(wifiHttpResponse) - 1] = '\0';
    }
  }
  if (!bleEventCharacteristic) return;
  // ATT notifications may carry only MTU-3 bytes (20 bytes at the default
  // MTU 23). Keep the protocol byte stream portable across phones by
  // fragmenting at 18 bytes and terminating every event with LF. UTF-8 may
  // cross a fragment boundary; the app reassembles bytes before decoding.
  const size_t length = strlen(text);
  size_t offset = 0;
  while (offset < length) {
    size_t count = std::min<size_t>(18, length - offset);
    bleEventCharacteristic->setValue(
        reinterpret_cast<const uint8_t*>(text + offset), count);
    bleEventCharacteristic->notify();
    offset += count;
    if (offset < length) delay(8);
  }
  const uint8_t newline = '\n';
  bleEventCharacteristic->setValue(&newline, 1);
  bleEventCharacteristic->notify();
}

esp_err_t saveChecklistItem(uint8_t index, bool commit = true) {
  if (!preferencesReady || index >= CHECK_ITEM_COUNT) return ESP_ERR_INVALID_STATE;
  char key[8]; snprintf(key, sizeof(key), "name%u", index);
  esp_err_t result = nvs_set_str(appNvsHandle, key, CHECK_ITEMS[index]);
  if (result != ESP_OK) return result;
  snprintf(key, sizeof(key), "tag%u", index);
  result = nvs_set_str(appNvsHandle, key, linkedTagUid[index]);
  if (result != ESP_OK) return result;
  snprintf(key, sizeof(key), "book%u", index);
  result = nvs_set_u8(appNvsHandle, key, itemIsBook[index] ? 1 : 0);
  if (result != ESP_OK) return result;
  snprintf(key, sizeof(key), "check%u", index);
  result = nvs_set_u8(appNvsHandle, key, itemChecked[index] ? 1 : 0);
  if (result != ESP_OK) return result;
  snprintf(key, sizeof(key), "req%u", index);
  result = nvs_set_u8(appNvsHandle, key, itemRequiredToday[index] ? 1 : 0);
  if (result != ESP_OK) return result;
  if (selectedWeekday >= 1 && selectedWeekday <= WEEKDAY_COUNT) {
    uint32_t checkedMask = 0;
    uint32_t requiredMask = 0;
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
      if (itemChecked[i]) checkedMask |= (1UL << i);
      if (itemRequiredToday[i]) requiredMask |= (1UL << i);
    }
    dayCheckedMasks[selectedWeekday - 1] = checkedMask;
    dayRequiredMasks[selectedWeekday - 1] = requiredMask;
    char dayKey[8];
    snprintf(dayKey, sizeof(dayKey), "dchk%u", selectedWeekday);
    result = nvs_set_u32(appNvsHandle, dayKey, checkedMask);
    if (result != ESP_OK) return result;
    snprintf(dayKey, sizeof(dayKey), "dreq%u", selectedWeekday);
    result = nvs_set_u32(appNvsHandle, dayKey, requiredMask);
    if (result != ESP_OK) return result;
  }
  return commit ? nvs_commit(appNvsHandle) : ESP_OK;
}

bool rtcTimeValid() {
  return time(nullptr) >= 1735689600;  // 2025-01-01 UTC; zero/epoch means unsynchronized.
}

uint8_t weekdayFromRtc() {
  time_t now = time(nullptr);
  struct tm local{};
  if (!rtcTimeValid() || !localtime_r(&now, &local)) return selectedWeekday;
  return local.tm_wday == 0 ? 7 : static_cast<uint8_t>(local.tm_wday);
}

void activateWeekday(uint8_t weekday) {
  if (weekday < 1 || weekday > WEEKDAY_COUNT) return;
  selectedWeekday = weekday;
  for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
    itemChecked[i] = (dayCheckedMasks[weekday - 1] & (1UL << i)) != 0;
    itemRequiredToday[i] = (dayRequiredMasks[weekday - 1] & (1UL << i)) != 0;
  }
  rebuildActiveChecklist();
}

esp_err_t saveDayMask(uint8_t weekday, bool checked) {
  if (!preferencesReady || weekday < 1 || weekday > WEEKDAY_COUNT) return ESP_ERR_INVALID_STATE;
  char key[8];
  snprintf(key, sizeof(key), checked ? "dchk%u" : "dreq%u", weekday);
  const uint32_t mask = checked ? dayCheckedMasks[weekday - 1]
                                : dayRequiredMasks[weekday - 1];
  esp_err_t result = nvs_set_u32(appNvsHandle, key, mask);
  if (result == ESP_OK) result = nvs_commit(appNvsHandle);
  if (result != ESP_OK)
    Serial.printf("[WEEK] 星期 %u 遮罩保存失敗：%s\n", weekday, esp_err_to_name(result));
  return result;
}

void rollbackChecklistItemNvs(uint8_t index) {
  if (!preferencesReady || index >= CHECK_ITEM_COUNT) return;
  // A failed multi-key save may leave some key updates pending in the NVS
  // handle. Re-stage the already-restored RAM values so a later commit cannot
  // accidentally persist only part of the failed RFID/checklist operation.
  esp_err_t result = saveChecklistItem(index, false);
  if (result == ESP_OK) result = nvs_commit(appNvsHandle);
  if (result != ESP_OK) {
    Serial.printf("[NVS] 項目 %u 回復持久化資料失敗：%s；請重新同步清單。\n",
                  static_cast<unsigned>(index), esp_err_to_name(result));
  }
}

void notifyChecklistItem(uint8_t index, uint8_t weekday = 0) {
  if (index >= CHECK_ITEM_COUNT) return;
  if (weekday == 0) weekday = selectedWeekday;
  char msg[160];
  if (weekday >= 1 && weekday <= WEEKDAY_COUNT) {
    snprintf(msg, sizeof(msg), "ITEM\t%u\t%s\t%s\t%u\t%u", index, CHECK_ITEMS[index],
             linkedTagUid[index][0] ? linkedTagUid[index] : "-",
             itemChecked[index] ? 1U : 0U, weekday);
  } else {
    snprintf(msg, sizeof(msg), "ITEM\t%u\t%s\t%s\t%u", index, CHECK_ITEMS[index],
             linkedTagUid[index][0] ? linkedTagUid[index] : "-",
             itemChecked[index] ? 1U : 0U);
  }
  bleNotify(msg);
}

void notifyNvsFailure(esp_err_t result) {
  char message[48];
  snprintf(message, sizeof(message), "ERR\tNVS_WRITE\t%s", esp_err_to_name(result));
  bleNotify(message);
  Serial.printf("[NVS] 寫入失敗：%s。\n", esp_err_to_name(result));
}

class BagBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { Serial.println("[BLE] 手機已連線。"); }
  void onDisconnect(BLEServer*) override {
    Serial.println("[BLE] 手機已離線；重新廣播。");
    BLEDevice::startAdvertising();
  }
};

class BagBleControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    if (!bleRxQueue) return;
    String value = characteristic->getValue();
    size_t offset = 0;
    while (offset < value.length()) {
      BleChunk chunk{};
      chunk.length = static_cast<uint8_t>(std::min<size_t>(sizeof(chunk.bytes), value.length() - offset));
      memcpy(chunk.bytes, value.c_str() + offset, chunk.length);
      if (xQueueSend(bleRxQueue, &chunk, 0) != pdTRUE) break;
      offset += chunk.length;
    }
  }
};

void initBle() {
  bleRxQueue = xQueueCreate(24, sizeof(BleChunk));
  if (!bleRxQueue) { Serial.println("[BLE] 接收佇列建立失敗。"); return; }
  BLEDevice::init("SmartSchoolbag");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new BagBleServerCallbacks());
  BLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  bleControlCharacteristic = service->createCharacteristic(
      BLE_CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  bleControlCharacteristic->setCallbacks(new BagBleControlCallbacks());
  bleEventCharacteristic = service->createCharacteristic(
      BLE_EVENT_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  bleEventCharacteristic->addDescriptor(new BLE2902());
  bleEventCharacteristic->setValue("READY");
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("[BLE] SmartSchoolbag 廣播中；控制協定 v1。");
}

void handleBleLine(char* line) {
  char* fields[6] = {};
  uint8_t fieldCount = 0;
  char* save = nullptr;
  for (char* token = strtok_r(line, "\t", &save); token && fieldCount < 6;
       token = strtok_r(nullptr, "\t", &save)) fields[fieldCount++] = token;
  if (!fieldCount) return;
  if (!strcmp(fields[0], "GET")) {
    bleNotify("READY\t1");
    { char versionMessage[32]; snprintf(versionMessage, sizeof(versionMessage), "VERSION\t%s", APP_VERSION); bleNotify(versionMessage); }
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) notifyChecklistItem(i);
    { char message[192]; snprintf(message, sizeof(message), "PLAN\t%s", todaySchedule); bleNotify(message); }
    { char message[32]; snprintf(message, sizeof(message), "NOTES_BEGIN\t%u", stickyNoteCount); bleNotify(message); }
    for (uint8_t i = 0; i < stickyNoteCount; ++i) {
      char message[160];
      snprintf(message, sizeof(message), "NOTE_ITEM\t%u\t%s", i, stickyNotes[i]);
      bleNotify(message);
    }
    bleNotify("NOTES_END");
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) { char msg[40]; snprintf(msg, sizeof(msg), "ITEM_CATEGORY\t%u\t%s", i, itemIsBook[i] ? "book" : "item"); bleNotify(msg); }
    const gps_support::View& g = gps_support::view;
    char gpsMessage[120];
    if (g.status == gps_support::Status::Fix)
      snprintf(gpsMessage, sizeof(gpsMessage), "GPS\tFIX\t%u\t%.6f\t%.6f", g.satellites, g.latitude, g.longitude);
    else snprintf(gpsMessage, sizeof(gpsMessage), "GPS\tNOFIX\t%u", g.satellites);
    bleNotify(gpsMessage);
    return;
  }
  if (!strcmp(fields[0], "TIME") && fieldCount >= 2) {
    const int64_t epoch = strtoll(fields[1], nullptr, 10);
    if (epoch < 1735689600LL || epoch > 4102444800LL) {
      bleNotify("ERR\tTIME_INVALID");
      return;
    }
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(epoch);
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) {
      bleNotify("ERR\tTIME_SET");
      return;
    }
    setenv("TZ", "CST-8", 1);
    tzset();
    if (!manualWeekdayOverride) activateWeekday(weekdayFromRtc());
    if (preferencesReady) {
      nvs_set_u8(appNvsHandle, "manWd", manualWeekdayOverride ? 1 : 0);
      nvs_commit(appNvsHandle);
    }
    if (uiPage == UiPage::Detail && (menuSelection == 0 || menuSelection == 1))
      markUiFullRefresh();
    bleNotify("TIME_OK");
    Serial.printf("[TIME] 手機同步 Unix=%lld；本地星期=%u；ESP32 RTC 在深睡期間持續計時。\n",
                  static_cast<long long>(epoch), selectedWeekday);
    return;
  }
  if (!strcmp(fields[0], "PLAN_DAY") && fieldCount >= 3) {
    const int weekday = atoi(fields[1]);
    if (weekday < 1 || weekday > WEEKDAY_COUNT ||
        strlen(fields[2]) >= sizeof(weekdayPlans[0])) {
      bleNotify("ERR\tPLAN_DAY_INVALID");
      return;
    }
    snprintf(weekdayPlans[weekday - 1], sizeof(weekdayPlans[0]), "%s", fields[2]);
    char key[8];
    snprintf(key, sizeof(key), "dpl%u", weekday);
    esp_err_t result = preferencesReady
        ? nvs_set_str(appNvsHandle, key, weekdayPlans[weekday - 1])
        : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nvs_commit(appNvsHandle);
    if (result != ESP_OK) { bleNotify("ERR\tNVS_WRITE"); return; }
    if (uiPage == UiPage::Detail && menuSelection == 1 && selectedWeekday == weekday)
      markUiFullRefresh();
    char acknowledgement[32];
    snprintf(acknowledgement, sizeof(acknowledgement), "PLAN_DAY_OK\t%d", weekday);
    bleNotify(acknowledgement);
    return;
  }
  if ((!strcmp(fields[0], "DAYREQ") || !strcmp(fields[0], "DAYCHECK")) && fieldCount >= 3) {
    const int weekday = atoi(fields[1]);
    if (weekday < 1 || weekday > WEEKDAY_COUNT) {
      bleNotify("ERR\tWEEKDAY_INVALID");
      return;
    }
    const uint32_t allowedMask = (1UL << CHECK_ITEM_COUNT) - 1UL;
    const uint32_t mask = static_cast<uint32_t>(strtoul(fields[2], nullptr, 16)) & allowedMask;
    const bool isChecked = !strcmp(fields[0], "DAYCHECK");
    const uint32_t previousMask = isChecked ? dayCheckedMasks[weekday - 1] : dayRequiredMasks[weekday - 1];
    if (isChecked) dayCheckedMasks[weekday - 1] = mask;
    else dayRequiredMasks[weekday - 1] = mask;
    const esp_err_t saveResult = saveDayMask(static_cast<uint8_t>(weekday), isChecked);
    if (saveResult != ESP_OK) {
      if (isChecked) dayCheckedMasks[weekday - 1] = previousMask;
      else dayRequiredMasks[weekday - 1] = previousMask;
      saveDayMask(static_cast<uint8_t>(weekday), isChecked);
      bleNotify("ERR\tNVS_WRITE");
      return;
    }
    if (selectedWeekday == weekday) {
      activateWeekday(static_cast<uint8_t>(weekday));
      if (uiPage == UiPage::Detail && menuSelection == 0) markUiFullRefresh();
    }
    char acknowledgement[32];
    snprintf(acknowledgement, sizeof(acknowledgement), "%s_OK\t%d",
             isChecked ? "DAYCHECK" : "DAYREQ", weekday);
    bleNotify(acknowledgement);
    return;
  }
  if (!strcmp(fields[0], "PLAN") && fieldCount >= 2) {
    if (strlen(fields[1]) >= sizeof(todaySchedule)) { bleNotify("ERR\tPLAN_TOO_LONG"); return; }
    snprintf(todaySchedule, sizeof(todaySchedule), "%s", fields[1]);
    esp_err_t result = preferencesReady
        ? nvs_set_str(appNvsHandle, "plan", todaySchedule) : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nvs_commit(appNvsHandle);
    if (result != ESP_OK) { bleNotify("ERR\tNVS_WRITE"); return; }
    if (uiPage == UiPage::Detail && menuSelection == 1) markUiFullRefresh();
    bleNotify("PLAN_OK");
    return;
  }
  if (!strcmp(fields[0], "NOTE") && fieldCount >= 2) {
    if (strlen(fields[1]) >= STICKY_NOTE_BYTES) { bleNotify("ERR\tNOTE_TOO_LONG"); return; }
    snprintf(stickyNotes[0], sizeof(stickyNotes[0]), "%s", fields[1]);
    stickyNoteCount = fields[1][0] ? 1 : 0;
    esp_err_t result = preferencesReady
        ? nvs_set_str(appNvsHandle, "note0", stickyNotes[0]) : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = nvs_set_u8(appNvsHandle, "nCount", stickyNoteCount);
    if (result == ESP_OK) result = nvs_commit(appNvsHandle);
    if (result != ESP_OK) { bleNotify("ERR\tNVS_WRITE"); return; }
    if (uiPage == UiPage::Detail && menuSelection == 2) markUiFullRefresh();
    bleNotify("NOTE_OK");
    return;
  }
  if (!strcmp(fields[0], "NOTE_BEGIN") && fieldCount >= 2) {
    int count = atoi(fields[1]);
    if (count < 0 || count > MAX_STICKY_NOTES) {
      bleNotify("ERR\tNOTE_COUNT");
      return;
    }
    pendingStickyNoteCount = static_cast<uint8_t>(count);
    memset(pendingStickyNotes, 0, sizeof(pendingStickyNotes));
    memset(pendingStickyNoteReceived, 0, sizeof(pendingStickyNoteReceived));
    return;
  }
  if (!strcmp(fields[0], "NOTE_ITEM") && fieldCount >= 3) {
    int index = atoi(fields[1]);
    if (index < 0 || index >= pendingStickyNoteCount ||
        strlen(fields[2]) >= STICKY_NOTE_BYTES) {
      bleNotify("ERR\tNOTE_ITEM_INVALID");
      return;
    }
    snprintf(pendingStickyNotes[index], sizeof(pendingStickyNotes[index]), "%s", fields[2]);
    pendingStickyNoteReceived[index] = true;
    return;
  }
  if (!strcmp(fields[0], "NOTE_DONE")) {
    for (uint8_t i = 0; i < pendingStickyNoteCount; ++i) {
      if (!pendingStickyNoteReceived[i] || !pendingStickyNotes[i][0]) {
        bleNotify("ERR\tNOTE_INCOMPLETE");
        return;
      }
    }
    if (!preferencesReady) {
      bleNotify("ERR\tNVS_UNAVAILABLE");
      return;
    }
    esp_err_t result = ESP_OK;
    for (uint8_t i = 0; i < MAX_STICKY_NOTES && result == ESP_OK; ++i) {
      char key[8];
      snprintf(key, sizeof(key), "note%u", i);
      result = nvs_set_str(appNvsHandle, key,
                           i < pendingStickyNoteCount ? pendingStickyNotes[i] : "");
    }
    if (result == ESP_OK) result = nvs_set_u8(appNvsHandle, "nCount", pendingStickyNoteCount);
    if (result == ESP_OK) result = nvs_commit(appNvsHandle);
    if (result != ESP_OK) {
      bleNotify("ERR\tNVS_WRITE");
      return;
    }
    memcpy(stickyNotes, pendingStickyNotes, sizeof(stickyNotes));
    stickyNoteCount = pendingStickyNoteCount;
    if (stickyNoteSelection >= stickyNoteCount) stickyNoteSelection = 0;
    if (uiPage == UiPage::Detail && menuSelection == 2) markUiFullRefresh();
    bleNotify("NOTE_OK");
    return;
  }
  if (!strcmp(fields[0], "SYNC_BEGIN") && fieldCount >= 2) {
    int requestedCount = atoi(fields[1]);
    if (requestedCount < 0 || requestedCount > CHECK_ITEM_COUNT) {
      bleSyncInProgress = false;
      bleNotify("ERR\tSYNC_COUNT");
      return;
    }
    bleSyncExpectedItems = static_cast<uint8_t>(requestedCount);
    bleSyncInProgress = true;
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
      bleSyncIsBook[i] = false;
      bleSyncNames[i][0] = '\0';
      bleSyncTags[i][0] = '\0';
      bleSyncChecked[i] = false;
      bleSyncRequired[i] = false;
      bleSyncItemReceived[i] = false;
      bleSyncRequiredReceived[i] = false;
    }
    return;
  }
  if (!strcmp(fields[0], "ITEM") && bleSyncInProgress && fieldCount >= 5) {
    int index = atoi(fields[1]);
    if (index < 0 || index >= bleSyncExpectedItems) return;
    if (!fields[2][0] || strlen(fields[2]) >= sizeof(bleSyncNames[index]) ||
        (strcmp(fields[3], "-") && strlen(fields[3]) >= sizeof(bleSyncTags[index])) ||
        (strcmp(fields[4], "0") && strcmp(fields[4], "1"))) return;
    snprintf(bleSyncNames[index], sizeof(bleSyncNames[index]), "%s", fields[2]);
    snprintf(bleSyncTags[index], sizeof(bleSyncTags[index]), "%s", !strcmp(fields[3], "-") ? "" : fields[3]);
    bleSyncChecked[index] = atoi(fields[4]) != 0;
    bleSyncItemReceived[index] = true;
    return;
  }
  if (!strcmp(fields[0], "CATEGORY") && bleSyncInProgress && fieldCount >= 3) {
    const int index = atoi(fields[1]);
    if (index < 0 || index >= bleSyncExpectedItems ||
        (strcmp(fields[2], "book") && strcmp(fields[2], "item"))) {
      bleNotify("ERR\tCATEGORY_INVALID"); return;
    }
    bleSyncIsBook[index] = !strcmp(fields[2], "book");
    return;
  }
  if (!strcmp(fields[0], "REQUIRED") && bleSyncInProgress && fieldCount >= 3) {
    int index = atoi(fields[1]);
    if (index >= 0 && index < bleSyncExpectedItems &&
        (!strcmp(fields[2], "0") || !strcmp(fields[2], "1"))) {
      bleSyncRequired[index] = atoi(fields[2]) != 0;
      bleSyncRequiredReceived[index] = true;
    }
    return;
  }
  if (!strcmp(fields[0], "SYNC_DONE") && bleSyncInProgress) {
    bleSyncInProgress = false;
    for (uint8_t i = 0; i < bleSyncExpectedItems; ++i) {
      if (!bleSyncItemReceived[i] || !bleSyncRequiredReceived[i]) {
        bleNotify("ERR\tSYNC_INCOMPLETE");
        Serial.printf("[BLE] 同步未套用：第 %u 項資料不完整。\n", static_cast<unsigned>(i));
        return;
      }
      if (bleSyncTags[i][0]) {
        for (uint8_t j = 0; j < i; ++j) {
          if (strcmp(bleSyncTags[i], bleSyncTags[j]) == 0) {
            bleNotify("ERR\tSYNC_DUPLICATE_TAG");
            Serial.println("[BLE] 同步未套用：標籤 UID 重複。");
            return;
          }
        }
      }
    }
    if (!preferencesReady) {
      bleNotify("ERR\tNVS_UNAVAILABLE");
      Serial.println("[BLE] 同步未套用：NVS 無法使用，原清單保持不變。");
      return;
    }
    char oldNames[CHECK_ITEM_COUNT][sizeof(CHECK_ITEMS[0])];
    char oldTags[CHECK_ITEM_COUNT][sizeof(linkedTagUid[0])];
    bool oldChecked[CHECK_ITEM_COUNT];
    bool oldIsBook[CHECK_ITEM_COUNT];
    memcpy(oldIsBook, itemIsBook, sizeof(oldIsBook));
    bool oldRequired[CHECK_ITEM_COUNT];
    memcpy(oldNames, CHECK_ITEMS, sizeof(oldNames));
    memcpy(oldTags, linkedTagUid, sizeof(oldTags));
    memcpy(oldChecked, itemChecked, sizeof(oldChecked));
    memcpy(oldRequired, itemRequiredToday, sizeof(oldRequired));
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
      snprintf(CHECK_ITEMS[i], sizeof(CHECK_ITEMS[i]), "%s", i < bleSyncExpectedItems ? bleSyncNames[i] : "");
      snprintf(linkedTagUid[i], sizeof(linkedTagUid[i]), "%s", i < bleSyncExpectedItems ? bleSyncTags[i] : "");
      itemIsBook[i] = i < bleSyncExpectedItems && bleSyncIsBook[i];
      itemChecked[i] = i < bleSyncExpectedItems && bleSyncChecked[i];
      itemRequiredToday[i] = i < bleSyncExpectedItems && bleSyncRequired[i];
    }
    // Persist empty trailing slots too, or deleted items from the prior sync
    // would be loaded back from NVS after the next reboot.
    esp_err_t saveResult = ESP_OK;
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT && saveResult == ESP_OK; ++i)
      saveResult = saveChecklistItem(i, false);
    if (saveResult == ESP_OK) saveResult = nvs_commit(appNvsHandle);
    if (saveResult != ESP_OK) {
      memcpy(CHECK_ITEMS, oldNames, sizeof(oldNames));
      memcpy(linkedTagUid, oldTags, sizeof(oldTags));
      memcpy(itemIsBook, oldIsBook, sizeof(oldIsBook));
      memcpy(itemChecked, oldChecked, sizeof(oldChecked));
      memcpy(itemRequiredToday, oldRequired, sizeof(oldRequired));
      esp_err_t rollbackResult = ESP_OK;
      for (uint8_t i = 0; i < CHECK_ITEM_COUNT && rollbackResult == ESP_OK; ++i)
        rollbackResult = saveChecklistItem(i, false);
      if (rollbackResult == ESP_OK) rollbackResult = nvs_commit(appNvsHandle);
      rebuildActiveChecklist();
      markUiFullRefresh();
      char errorMessage[48];
      snprintf(errorMessage, sizeof(errorMessage), "ERR\tNVS_WRITE\t%s",
               esp_err_to_name(saveResult));
      bleNotify(errorMessage);
      Serial.printf("[BLE] 同步未套用：NVS 寫入失敗 (%s)，RAM 已還原；NVS 回滾 %s。\n",
                    esp_err_to_name(saveResult), esp_err_to_name(rollbackResult));
      return;
    }
    rebuildActiveChecklist();
    markUiFullRefresh();
    bleNotify("SYNC_OK");
    Serial.printf("[BLE] 手機同步完成：%u 項物品。\n", bleSyncExpectedItems);
    return;
  }
  if (!strcmp(fields[0], "CHECK") && fieldCount >= 3) {
    int index = atoi(fields[1]);
    if (index >= 0 && index < CHECK_ITEM_COUNT) {
      bool checked = atoi(fields[2]) != 0;
      bool previousChecked = itemChecked[index];
      if (previousChecked != checked) {
        itemChecked[index] = checked;
        esp_err_t saveResult = saveChecklistItem(static_cast<uint8_t>(index));
        if (saveResult != ESP_OK) {
          itemChecked[index] = previousChecked;
          rollbackChecklistItemNvs(static_cast<uint8_t>(index));
          notifyNvsFailure(saveResult);
        }
      }
      if (uiPage == UiPage::Detail && menuSelection == 0) {
        int8_t row = checklistVisibleRow(static_cast<uint8_t>(index));
        if (row >= 0) markUiWindow(3, 42 + row * 38 - 16, 122, 29);
        markUiWindow(3, 264, 122, 18);
      }
      notifyChecklistItem(index);
    }
    return;
  }
  if (!strcmp(fields[0], "CHECKDAY") && fieldCount >= 4) {
    const int weekday = atoi(fields[1]);
    const int index = atoi(fields[2]);
    if (weekday < 1 || weekday > WEEKDAY_COUNT || index < 0 ||
        index >= CHECK_ITEM_COUNT ||
        (strcmp(fields[3], "0") && strcmp(fields[3], "1"))) {
      bleNotify("ERR\tCHECKDAY_INVALID");
      return;
    }
    const uint32_t bit = 1UL << index;
    const bool checked = fields[3][0] == '1';
    const bool wasChecked = (dayCheckedMasks[weekday - 1] & bit) != 0;
    if (wasChecked != checked) {
      if (checked) dayCheckedMasks[weekday - 1] |= bit;
      else dayCheckedMasks[weekday - 1] &= ~bit;
      const esp_err_t saveResult = saveDayMask(static_cast<uint8_t>(weekday), true);
      if (saveResult != ESP_OK) {
        if (wasChecked) dayCheckedMasks[weekday - 1] |= bit;
        else dayCheckedMasks[weekday - 1] &= ~bit;
        saveDayMask(static_cast<uint8_t>(weekday), true);
        bleNotify("ERR\tNVS_WRITE");
        return;
      }
      if (selectedWeekday == weekday) {
        itemChecked[index] = checked;
        if (uiPage == UiPage::Detail && menuSelection == 0) {
          const int8_t row = checklistVisibleRow(static_cast<uint8_t>(index));
          if (row >= 0) markUiWindow(3, 42 + row * 38 - 16, 122, 29);
        }
        notifyChecklistItem(static_cast<uint8_t>(index));
      }
    }
    return;
  }
  if ((!strcmp(fields[0], "PAIR") || !strcmp(fields[0], "UNPAIR")) && fieldCount >= 2) {
    int index = atoi(fields[1]);
    if (index >= 0 && index < CHECK_ITEM_COUNT) {
      checklistSelection = static_cast<uint8_t>(index);
      if (!strcmp(fields[0], "PAIR")) {
        uiPage = UiPage::Detail; menuSelection = 0; checklistDayPickerOpen = false; pairingArmed = true;
        snprintf(lastTagMessage, sizeof(lastTagMessage), "手機要求配對，請刷標籤");
        forceStandardRefreshNext = true; markUiFullRefresh(); bleNotify("PAIR_READY");
      } else {
        char previousUid[sizeof(linkedTagUid[index])];
        snprintf(previousUid, sizeof(previousUid), "%s", linkedTagUid[index]);
        linkedTagUid[index][0] = '\0';
        esp_err_t saveResult = saveChecklistItem(static_cast<uint8_t>(index));
        if (saveResult != ESP_OK) {
          snprintf(linkedTagUid[index], sizeof(linkedTagUid[index]), "%s", previousUid);
          rollbackChecklistItemNvs(static_cast<uint8_t>(index));
          notifyNvsFailure(saveResult);
          notifyChecklistItem(static_cast<uint8_t>(index));
        } else {
          notifyChecklistItem(static_cast<uint8_t>(index));
          bleNotify("UNPAIR_OK");
        }
      }
    }
    return;
  }
  bleNotify("ERR\tUNKNOWN_COMMAND");
}

void handleWifiControl() {
  if (!wifiHttpServer.hasArg("plain")) {
    wifiHttpServer.send(400, "text/plain; charset=utf-8", "ERR\tEMPTY_BODY\n");
    return;
  }
  String body = wifiHttpServer.arg("plain");
  if (body.length() > 6144) {
    wifiHttpServer.send(413, "text/plain; charset=utf-8", "ERR\tBODY_TOO_LARGE\n");
    return;
  }
  wifiHttpResponse[0] = '\0';
  wifiHttpCapture = true;
  size_t start = 0;
  while (start < body.length()) {
    size_t end = body.indexOf('\n', start);
    if (end == static_cast<size_t>(-1)) end = body.length();
    String requestLine = body.substring(start, end);
    requestLine.trim();
    if (requestLine.length() && requestLine.length() < sizeof(bleLineBuffer)) {
      char line[sizeof(bleLineBuffer)];
      requestLine.toCharArray(line, sizeof(line));
      handleBleLine(line);
    }
    start = end + 1;
  }
  wifiHttpCapture = false;
  wifiHttpServer.send(200, "text/plain; charset=utf-8", wifiHttpResponse);
}

void initWifiControl() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  const bool started = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  wifiHttpServer.on("/status", HTTP_GET, []() {
    wifiHttpResponse[0] = '\0';
    wifiHttpCapture = true;
    char getCommand[] = "GET";
    handleBleLine(getCommand);
    wifiHttpCapture = false;
    wifiHttpServer.send(200, "text/plain; charset=utf-8", wifiHttpResponse);
  });
  wifiHttpServer.on("/control", HTTP_POST, handleWifiControl);
  wifiHttpServer.onNotFound([]() { wifiHttpServer.send(404, "text/plain", "Not found\n"); });
  wifiHttpServer.begin();
  Serial.printf("[WiFi] AP %s；IP %s；HTTP %s\n",
                started ? WIFI_AP_SSID : "啟動失敗",
                WiFi.softAPIP().toString().c_str(), started ? "已啟動" : "未啟動");
}

void pollBle() {
  if (!bleRxQueue) return;
  BleChunk chunk{};
  while (xQueueReceive(bleRxQueue, &chunk, 0) == pdTRUE) {
    for (uint8_t i = 0; i < chunk.length; ++i) {
      char ch = static_cast<char>(chunk.bytes[i]);
      if (ch == '\r') continue;
      if (ch == '\n') {
        if (bleDiscardingOversizeLine) {
          bleNotify("ERR\tLINE_TOO_LONG");
          bleDiscardingOversizeLine = false;
        } else {
          bleLineBuffer[bleLineLength] = '\0';
          handleBleLine(bleLineBuffer);
        }
        bleLineLength = 0;
      } else if (!bleDiscardingOversizeLine) {
        if (bleLineLength + 1 < sizeof(bleLineBuffer))
          bleLineBuffer[bleLineLength++] = ch;
        else {
          bleLineLength = 0;
          bleDiscardingOversizeLine = true;
        }
      }
    }
  }
}

void markUiFullRefresh() {
  uiDirty = true;
  uiFullRefresh = true;
  pendingUiWindowCount = 0;
}

void markUiWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (uiFullRefresh) { uiDirty = true; return; }
  UiWindow next{x, y, w, h, true};
  for (uint8_t i = 0; i < pendingUiWindowCount; ++i) {
    UiWindow& current = pendingUiWindows[i];
    bool horizontalOverlap = next.x < current.x + current.w && current.x < next.x + next.w;
    int16_t verticalGap = next.y >= current.y + current.h ? next.y - (current.y + current.h) :
                          current.y >= next.y + next.h ? current.y - (next.y + next.h) : 0;
    bool shouldMerge = horizontalOverlap && verticalGap <= 14;
    if (shouldMerge) {
      int16_t x1 = std::min(current.x, next.x);
      int16_t y1 = std::min(current.y, next.y);
      int16_t x2 = std::max<int16_t>(current.x + current.w, next.x + next.w);
      int16_t y2 = std::max<int16_t>(current.y + current.h, next.y + next.h);
      current = {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1), true};
      uiDirty = true;
      return;
    }
  }
  if (pendingUiWindowCount < MAX_UI_WINDOWS) {
    pendingUiWindows[pendingUiWindowCount++] = next;
  } else {
    int16_t x1 = next.x, y1 = next.y;
    int16_t x2 = next.x + next.w, y2 = next.y + next.h;
    for (uint8_t i = 0; i < pendingUiWindowCount; ++i) {
      x1 = std::min(x1, pendingUiWindows[i].x);
      y1 = std::min(y1, pendingUiWindows[i].y);
      x2 = std::max<int16_t>(x2, pendingUiWindows[i].x + pendingUiWindows[i].w);
      y2 = std::max<int16_t>(y2, pendingUiWindows[i].y + pendingUiWindows[i].h);
    }
    pendingUiWindows[0] = {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1), true};
    pendingUiWindowCount = 1;
  }
  uiDirty = true;
}

void markMenuRowChange(uint8_t oldIndex, uint8_t newIndex) {
  markUiWindow(4, 38 + oldIndex * 42 - 12, 120, 30);
  markUiWindow(4, 38 + newIndex * 42 - 12, 120, 30);
}

void markChecklistRowChange(uint8_t oldIndex, uint8_t newIndex) {
  int8_t oldPosition = checklistPosition(oldIndex);
  int8_t newPosition = checklistPosition(newIndex);
  if (oldPosition >= 0 && newPosition >= 0 &&
      (oldPosition < CHECKLIST_VISIBLE_ROWS ? 0 : oldPosition - CHECKLIST_VISIBLE_ROWS + 1) !=
      (newPosition < CHECKLIST_VISIBLE_ROWS ? 0 : newPosition - CHECKLIST_VISIBLE_ROWS + 1)) {
    markUiFullRefresh();
    return;
  }
  const int8_t oldRow = checklistVisibleRow(oldIndex);
  const int8_t newRow = checklistVisibleRow(newIndex);
  if (oldRow >= 0) markUiWindow(3, 42 + oldRow * 38 - 16, 122, 29);
  if (newRow >= 0) markUiWindow(3, 42 + newRow * 38 - 16, 122, 29);
}

bool isCardFrame(Frame frame) {
  return frame == Frame::Card || frame == Frame::ReaderReady ||
         frame == Frame::ReaderError || frame == Frame::GPS ||
         frame == Frame::ButtonTest || frame == Frame::SystemInfo;
}

bool isLiveDataFrame(Frame frame) {
  return frame == Frame::Card || frame == Frame::ReaderReady ||
         frame == Frame::ReaderError || frame == Frame::GPS ||
         frame == Frame::ButtonTest;
}

const char* shortCardType(MFRC522::PICC_Type type) {
  switch (type) {
    case MFRC522::PICC_TYPE_MIFARE_MINI: return "MIFARE Mini";
    case MFRC522::PICC_TYPE_MIFARE_1K: return "MIFARE 1K";
    case MFRC522::PICC_TYPE_MIFARE_4K: return "MIFARE 4K";
    case MFRC522::PICC_TYPE_MIFARE_UL: return "MIFARE Ultralight";
    case MFRC522::PICC_TYPE_ISO_14443_4: return "ISO14443-4";
    case MFRC522::PICC_TYPE_ISO_18092: return "ISO18092";
    default: return "Other / Unknown";
  }
}

void processSchoolbagTag(const char* uid) {
  if (pairingArmed) {
    const uint8_t index = checklistSelection;
    for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
      if (i != index && linkedTagUid[i][0] && !strcmp(linkedTagUid[i], uid)) {
        bleNotify("ERR\tTAG_ALREADY_LINKED");
        snprintf(lastTagMessage, sizeof(lastTagMessage), "標籤已綁定其他物品");
        cardPending = false; markUiFullRefresh(); return;
      }
    }
    char previous[sizeof(linkedTagUid[0])];
    snprintf(previous, sizeof(previous), "%s", linkedTagUid[index]);
    snprintf(linkedTagUid[index], sizeof(linkedTagUid[index]), "%s", uid);
    const esp_err_t result = saveChecklistItem(index);
    if (result != ESP_OK) {
      snprintf(linkedTagUid[index], sizeof(linkedTagUid[index]), "%s", previous);
      rollbackChecklistItemNvs(index); notifyNvsFailure(result);
    } else {
      pairingArmed = false;
      snprintf(lastTagMessage, sizeof(lastTagMessage), "已綁定：%s", CHECK_ITEMS[index]);
      notifyChecklistItem(index); bleNotify("PAIR_OK");
    }
    cardPending = false; markUiFullRefresh(); return;
  }
  for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
    if (linkedTagUid[i][0] && strcmp(linkedTagUid[i], uid) == 0) {
      if (itemChecked[i]) {
        // A completed RFID item is idempotent: don't notify the phone or queue
        // another card/full-screen frame just because the tag stayed in range.
        cardPending = false;
        Serial.printf("[BAG] %s 已完成；忽略重複刷卡，不刷新畫面。\n", CHECK_ITEMS[i]);
        return;
      }
      bool changed = !itemChecked[i];
      itemChecked[i] = true;
      esp_err_t saveResult = changed ? saveChecklistItem(i) : ESP_OK;
      if (saveResult != ESP_OK) {
        itemChecked[i] = false;
        rollbackChecklistItemNvs(i);
        snprintf(lastTagMessage, sizeof(lastTagMessage), "NVS 寫入失敗，未勾選");
        notifyNvsFailure(saveResult);
        notifyChecklistItem(i, selectedWeekday);
        if (uiPage == UiPage::Detail && menuSelection == 0) {
          const int8_t row = checklistVisibleRow(i);
          if (row >= 0) markUiWindow(3, 42 + row * 38 - 16, 122, 29);
          markUiWindow(3, 264, 122, 18);
        }
        return;
      } else {
        snprintf(lastTagMessage, sizeof(lastTagMessage), "已勾選：%s", CHECK_ITEMS[i]);
        if (changed) Serial.printf("[BAG] %s 已自動勾選（UID=%s）。\n", CHECK_ITEMS[i], uid);
      }
      notifyChecklistItem(i, selectedWeekday);
      char tagMessage[128]; snprintf(tagMessage, sizeof(tagMessage), "TAG\t%s\t%s\t%u", CHECK_ITEMS[i], uid, selectedWeekday);
      bleNotify(tagMessage);
      if (uiPage == UiPage::Detail && menuSelection == 0) {
        // Keep the checklist on screen; the row update is the only redraw.
        cardPending = false;
        int8_t row = checklistVisibleRow(i);
        if (row >= 0) markUiWindow(3, 42 + row * 38 - 16, 122, 29);
        markUiWindow(3, 264, 122, 18);
      }
      return;
    }
  }
  if (uiPage != UiPage::Detail || menuSelection != 0 || checklistDayPickerOpen || !activeChecklistCount) {
    snprintf(lastTagMessage, sizeof(lastTagMessage), "未知卡，請到今日清單刷卡");
    char tagMessage[72]; snprintf(tagMessage, sizeof(tagMessage), "TAG\tUNKNOWN\t%s", uid); bleNotify(tagMessage);
    Serial.printf("[RFID] 未知 UID=%s；請先進入今日清單選擇物品。\n", uid);
    return;
  }
  const uint8_t index = checklistSelection;
  if (!preferencesReady) {
    snprintf(lastTagMessage, sizeof(lastTagMessage), "NVS無法使用，未綁定標籤");
    bleNotify("ERR\tNVS_UNAVAILABLE");
    markUiWindow(3, 244, 122, 30);
    return;
  }
  char previousUid[sizeof(linkedTagUid[index])];
  snprintf(previousUid, sizeof(previousUid), "%s", linkedTagUid[index]);
  const bool previousChecked = itemChecked[index];
  snprintf(linkedTagUid[index], sizeof(linkedTagUid[index]), "%s", uid);
  itemChecked[index] = true;
  const esp_err_t saveResult = saveChecklistItem(index);
  if (saveResult != ESP_OK) {
    snprintf(linkedTagUid[index], sizeof(linkedTagUid[index]), "%s", previousUid);
    itemChecked[index] = previousChecked;
    rollbackChecklistItemNvs(index);
    notifyNvsFailure(saveResult);
    snprintf(lastTagMessage, sizeof(lastTagMessage), "儲存失敗，請重新刷卡");
    notifyChecklistItem(index);
  } else {
    snprintf(lastTagMessage, sizeof(lastTagMessage), "已綁定並勾選：%.20s", CHECK_ITEMS[index]);
    notifyChecklistItem(index);
    char tagMessage[128]; snprintf(tagMessage, sizeof(tagMessage), "TAG\t%s\t%s\t%u", CHECK_ITEMS[index], uid, selectedWeekday); bleNotify(tagMessage);
    Serial.printf("[RFID] UID=%s 已直接綁定並勾選：%s。\n", uid, CHECK_ITEMS[index]);
  }
  int8_t row = checklistVisibleRow(index);
  if (row >= 0) markUiWindow(3, 42 + row * 38 - 16, 122, 29);
  markUiWindow(3, 244, 122, 30);
}

void loadSchoolbagTags() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("[RFID SETUP] NVS 空間／版本異常；不自動清除既有資料，標籤配對只保存在 RAM。");
    return;
  }
  if (err != ESP_OK) {
    Serial.printf("[RFID SETUP] NVS 初始化失敗：%s\n", esp_err_to_name(err));
    return;
  }
  err = nvs_open("schoolbag", NVS_READWRITE, &appNvsHandle);
  if (err != ESP_OK) {
    Serial.printf("[RFID SETUP] NVS 開啟失敗：%s\n", esp_err_to_name(err));
    return;
  }
  preferencesReady = true;
  setenv("TZ", "CST-8", 1);
  tzset();
  uint8_t savedManualDay = 0;
  uint8_t savedWeekday = 1;
  nvs_get_u8(appNvsHandle, "manWd", &savedManualDay);
  if (nvs_get_u8(appNvsHandle, "selWd", &savedWeekday) == ESP_OK &&
      savedWeekday >= 1 && savedWeekday <= WEEKDAY_COUNT)
    selectedWeekday = savedWeekday;
  manualWeekdayOverride = savedManualDay != 0;
  if (!manualWeekdayOverride && rtcTimeValid()) selectedWeekday = weekdayFromRtc();
  for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "tag%u", i);
    size_t length = sizeof(linkedTagUid[i]);
    if (nvs_get_str(appNvsHandle, key, linkedTagUid[i], &length) != ESP_OK)
      linkedTagUid[i][0] = '\0';
    snprintf(key, sizeof(key), "name%u", i);
    length = sizeof(CHECK_ITEMS[i]);
    if (nvs_get_str(appNvsHandle, key, CHECK_ITEMS[i], &length) != ESP_OK) {
      static const char* defaults[] = {"國文課本", "英文課本", "自然課本", "作業本", "水壺"};
      snprintf(CHECK_ITEMS[i], sizeof(CHECK_ITEMS[i]), "%s",
               i < sizeof(defaults) / sizeof(defaults[0]) ? defaults[i] : "");
    }
    snprintf(key, sizeof(key), "book%u", i);
    uint8_t isBook = strstr(CHECK_ITEMS[i], "課本") ? 1 : 0;
    nvs_get_u8(appNvsHandle, key, &isBook);
    itemIsBook[i] = isBook != 0;
    snprintf(key, sizeof(key), "check%u", i);
    uint8_t checked = 0;
    itemChecked[i] = nvs_get_u8(appNvsHandle, key, &checked) == ESP_OK && checked != 0;
    snprintf(key, sizeof(key), "req%u", i);
    uint8_t required = 1;
    itemRequiredToday[i] = nvs_get_u8(appNvsHandle, key, &required) == ESP_OK
                                ? required != 0
                                : true;
  }
  uint32_t legacyCheckedMask = 0;
  uint32_t legacyRequiredMask = 0;
  for (uint8_t i = 0; i < CHECK_ITEM_COUNT; ++i) {
    if (itemChecked[i]) legacyCheckedMask |= (1UL << i);
    if (itemRequiredToday[i]) legacyRequiredMask |= (1UL << i);
  }
  for (uint8_t weekday = 1; weekday <= WEEKDAY_COUNT; ++weekday) {
    char key[8];
    snprintf(key, sizeof(key), "dchk%u", weekday);
    if (nvs_get_u32(appNvsHandle, key, &dayCheckedMasks[weekday - 1]) != ESP_OK)
      dayCheckedMasks[weekday - 1] = weekday == selectedWeekday ? legacyCheckedMask : 0;
    snprintf(key, sizeof(key), "dreq%u", weekday);
    if (nvs_get_u32(appNvsHandle, key, &dayRequiredMasks[weekday - 1]) != ESP_OK)
      dayRequiredMasks[weekday - 1] = weekday == selectedWeekday ? legacyRequiredMask : 0;
    snprintf(key, sizeof(key), "dpl%u", weekday);
    size_t length = sizeof(weekdayPlans[0]);
    if (nvs_get_str(appNvsHandle, key, weekdayPlans[weekday - 1], &length) != ESP_OK)
      weekdayPlans[weekday - 1][0] = '\0';
  }
  size_t planLength = sizeof(todaySchedule);
  if (nvs_get_str(appNvsHandle, "plan", todaySchedule, &planLength) != ESP_OK)
    snprintf(todaySchedule, sizeof(todaySchedule), "尚未同步今日課表");
  uint8_t savedNoteCount = 0;
  if (nvs_get_u8(appNvsHandle, "nCount", &savedNoteCount) == ESP_OK &&
      savedNoteCount <= MAX_STICKY_NOTES) {
    stickyNoteCount = savedNoteCount;
    for (uint8_t i = 0; i < stickyNoteCount; ++i) {
      char key[8];
      snprintf(key, sizeof(key), "note%u", i);
      size_t length = sizeof(stickyNotes[i]);
      if (nvs_get_str(appNvsHandle, key, stickyNotes[i], &length) != ESP_OK)
        stickyNotes[i][0] = '\0';
    }
  } else {
    size_t legacyLength = sizeof(stickyNotes[0]);
    if (nvs_get_str(appNvsHandle, "note", stickyNotes[0], &legacyLength) == ESP_OK &&
        stickyNotes[0][0]) {
      stickyNoteCount = 1;
      nvs_set_str(appNvsHandle, "note0", stickyNotes[0]);
      nvs_set_u8(appNvsHandle, "nCount", stickyNoteCount);
      nvs_commit(appNvsHandle);
    }
  }
  uint8_t savedRefreshSweeps = UI_PARTIAL_SWEEPS_BEFORE_DIRECT_FULL_REFRESH;
  if (nvs_get_u8(appNvsHandle, "swpN", &savedRefreshSweeps) == ESP_OK &&
      (savedRefreshSweeps == 0 || (savedRefreshSweeps >= 3 && savedRefreshSweeps <= 5))) {
    uiRefreshSweepsBeforeFull = savedRefreshSweeps;
  }
  settingsRefreshSelection = uiRefreshSweepsBeforeFull == 0
                                 ? 3 : uiRefreshSweepsBeforeFull - 3;
  settingsMenuSelection = settingsRefreshSelection;
  uint8_t savedInvert = 0;
  if (nvs_get_u8(appNvsHandle, "invBW", &savedInvert) == ESP_OK)
    blackWhiteReflected = savedInvert != 0;
  nvs_get_u8(appNvsHandle, "idleM", &autoStandbyMinutes);
  if (!(autoStandbyMinutes == 0 || autoStandbyMinutes == 5 ||
        autoStandbyMinutes == 10 || autoStandbyMinutes == 15 ||
        autoStandbyMinutes == 30 || autoStandbyMinutes == 60))
    autoStandbyMinutes = 0;
  activateWeekday(selectedWeekday);
}

void applyRefreshSetting() {
  const uint8_t selectedSweeps = settingsRefreshSelection == 3
                                     ? 0 : settingsRefreshSelection + 3;
  const uint8_t previousSweeps = uiRefreshSweepsBeforeFull;
  if (selectedSweeps != previousSweeps && preferencesReady) {
    esp_err_t result = nvs_set_u8(appNvsHandle, "swpN", selectedSweeps);
    if (result == ESP_OK) result = nvs_commit(appNvsHandle);
    if (result != ESP_OK) {
      nvs_set_u8(appNvsHandle, "swpN", previousSweeps);
      nvs_commit(appNvsHandle);
      settingsRefreshSelection = previousSweeps == 0 ? 3 : previousSweeps - 3;
      Serial.printf("[UI] 刷新頻率儲存失敗：%s；已恢復原設定。\n", esp_err_to_name(result));
      markUiFullRefresh();
      return;
    }
  }
  uiRefreshSweepsBeforeFull = selectedSweeps;
  if (uiRefreshSweepsBeforeFull == 0) {
    Serial.println("[UI] 已設定：同一頁永遠使用整頁單次局刷；切換互動頁面時才標準全波形全刷。");
  } else {
    Serial.printf("[UI] 已設定前 %u 次整頁單次局刷，第 %u 次直接全波形全刷。\n",
                  static_cast<unsigned>(uiRefreshSweepsBeforeFull),
                  static_cast<unsigned>(uiRefreshSweepsBeforeFull + 1));
  }
  markUiFullRefresh();
}

void toggleBlackWhiteReflection() {
  const bool previous = blackWhiteReflected;
  const bool next = !previous;
  esp_err_t result = ESP_OK;
  if (preferencesReady) {
    result = nvs_set_u8(appNvsHandle, "invBW", next ? 1 : 0);
    if (result == ESP_OK) result = nvs_commit(appNvsHandle);
    if (result != ESP_OK) {
      nvs_set_u8(appNvsHandle, "invBW", previous ? 1 : 0);
      nvs_commit(appNvsHandle);
      Serial.printf("[UI] 黑白反射設定儲存失敗：%s；保留原值。\n", esp_err_to_name(result));
      return;
    }
  }
  blackWhiteReflected = next;
  forceStandardRefreshNext = true;
  Serial.printf("[UI] 黑白反射：%s%s。\n", blackWhiteReflected ? "開啟" : "關閉",
                preferencesReady ? "（已保存）" : "（僅本次開機有效：NVS 不可用）");
  markUiFullRefresh();
}

void initReader() {
  // 只在電子紙工作未使用 SPI 時呼叫。兩條片選不能同時拉低。
  digitalWrite(EPD_CS, HIGH);
  reader.PCD_Init();
  readerRFEnabled = true;
  delay(4);
  readerVersion = reader.PCD_ReadRegister(MFRC522::VersionReg);
  readerOnline = readerVersion != 0x00 && readerVersion != 0xFF;
  readerBadChecks = 0;
  lastReaderCheckMs = millis();
  readerStatusPending = true;
  if (!readerOnline) cardPending = false;
  Serial.printf("[RC522] FW=0x%02X，%s。\n", readerVersion,
                readerOnline ? "SPI 通訊已回應，可感應卡片" : "無有效回應，請檢查 3.3V、SS9、RST8、MISO13");
  if (readerOnline && readerVersion != 0x91 && readerVersion != 0x92 &&
      readerVersion != 0x90 && readerVersion != 0x88) {
    Serial.println("[RC522] 版本碼非已知常見值，仍嘗試讀卡，不據此判定為正品。");
  }
}

void pollReader() {
  // 主程式與顯示工作固定於同一核心；Running 時不碰共用 SPI。
  if (state.load() != State::Idle) return;
  uint32_t now = millis();
  if (!readerOnline) {
    if (now - lastReaderCheckMs >= RFID_RETRY_INTERVAL_MS) {
      bool wasOnline = readerOnline;
      initReader();
      // 持續離線時不反覆刷新同一張錯誤畫面。
      if (!wasOnline && !readerOnline) readerStatusPending = false;
    }
    return;
  }
  if (now - lastReaderCheckMs >= RFID_RETRY_INTERVAL_MS) {
    lastReaderCheckMs = now;
    uint8_t version = reader.PCD_ReadRegister(MFRC522::VersionReg);
    if (version == 0x00 || version == 0xFF) {
      if (++readerBadChecks >= 3) {
        readerOnline = false;
        readerVersion = version;
        readerStatusPending = true;
        cardPending = false;
        Serial.println("[RC522] 連續三次通訊無回應，進入離線重試。");
      }
    } else {
      readerBadChecks = 0;
    }
  }
  if (!readerOnline || !readerRFEnabled || now - lastReaderPollMs < RFID_POLL_INTERVAL_MS) return;
  lastReaderPollMs = now;
  if (!reader.PICC_IsNewCardPresent()) return;
  if (!reader.PICC_ReadCardSerial()) {
    if (now - lastCardErrorMs >= 2000) {
      lastCardErrorMs = now;
      Serial.println("[RC522] 感應到回應但 UID 讀取未完成，請將一張卡放穩。");
    }
    return;
  }
  if (reader.uid.size == 4 || reader.uid.size == 7 || reader.uid.size == 10) {
    lastUserActivityMs = millis();
    char* out = latestCard.uid;
    size_t remaining = sizeof(latestCard.uid);
    for (uint8_t i = 0; i < reader.uid.size; ++i) {
      int length = snprintf(out, remaining, i ? " %02X" : "%02X", reader.uid.uidByte[i]);
      out += length;
      remaining -= static_cast<size_t>(length);
    }
    latestCard.uidSize = reader.uid.size;
    latestCard.type = reader.PICC_GetType(reader.uid.sak);
    latestCard.readAtMs = millis();
    ++latestCard.count;
    cardPending = true;
    readerStatusPending = false;
    processSchoolbagTag(latestCard.uid);
    Serial.printf("[CARD] #%lu UID=%s，UID長度=%u，SAK=0x%02X，類型=%s\n",
                  static_cast<unsigned long>(latestCard.count), latestCard.uid,
                  latestCard.uidSize, reader.uid.sak, shortCardType(latestCard.type));
  } else {
    Serial.printf("[RC522] UID 長度異常：%u，未顯示。\n", reader.uid.size);
  }
  reader.PICC_HaltA();
  reader.PCD_StopCrypto1();
}

const Frame sequenceFrames[] = {
    Frame::White, Frame::Black, Frame::Pattern, Frame::Grey};

// 4×4 Bayer 點陣：每格覆蓋 0～16 個黑點，提供 17 種覆蓋率。
constexpr uint8_t BAYER[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

void ditherRect(int x, int y, int w, int h, uint8_t coverage) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      display.drawPixel(xx, yy, BAYER[yy & 3][xx & 3] < coverage
                                    ? uiBlack() : uiWhite());
    }
  }
}

void textAt(int x, int y, const char* text, uint16_t color = uiBlack()) {
  display.setTextColor(color);
  display.setCursor(x, y);
  display.print(text);
}

uint32_t nextUtf8Codepoint(const char*& text) {
  const uint8_t first = static_cast<uint8_t>(*text++);
  if (first < 0x80) return first;
  if ((first & 0xE0) == 0xC0) {
    const uint8_t second = static_cast<uint8_t>(*text++);
    return ((first & 0x1F) << 6) | (second & 0x3F);
  }
  if ((first & 0xF0) == 0xE0) {
    const uint8_t second = static_cast<uint8_t>(*text++);
    const uint8_t third = static_cast<uint8_t>(*text++);
    return ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
  }
  return '?';
}

const ChineseGlyph12* findChineseGlyph(uint32_t codepoint) {
  uint16_t low = 0;
  uint16_t high = ZH_GLYPH_COUNT;
  while (low < high) {
    uint16_t mid = low + (high - low) / 2;
    uint16_t value = pgm_read_word(&ZH_GLYPHS[mid].codepoint);
    if (value < codepoint) low = mid + 1;
    else high = mid;
  }
  return low < ZH_GLYPH_COUNT && pgm_read_word(&ZH_GLYPHS[low].codepoint) == codepoint
             ? &ZH_GLYPHS[low] : nullptr;
}

// y is the text baseline; CJK glyphs are 12x12, ASCII remains the built-in 6x8 font.
void textZhAtColor(int x, int y, const char* text, uint16_t color) {
  int cursorX = x;
  display.setTextSize(1);
  display.setTextColor(color);
  while (*text && cursorX < display.width() - 5) {
    uint32_t codepoint = nextUtf8Codepoint(text);
    if (codepoint < 0x80) {
      // Match the bottom baseline of the 12-pixel CJK bitmap; Adafruit's
      // built-in ASCII font is 8 pixels high and setCursor() expects its top.
      // A one-pixel overprint gives Latin letters and digits weight closer to
      // the rasterized Chinese glyphs while keeping a shared baseline.
      display.setCursor(cursorX, y - 7);
      display.write(static_cast<uint8_t>(codepoint));
      if (codepoint != ' ') {
        display.setCursor(cursorX + 1, y - 7);
        display.write(static_cast<uint8_t>(codepoint));
      }
      cursorX += codepoint == ' ' ? 4 : 7;
      continue;
    }
    const ChineseGlyph12* glyph = findChineseGlyph(codepoint);
    if (glyph) {
      for (uint8_t row = 0; row < 12; ++row) {
        uint16_t bits = pgm_read_word(&glyph->rows[row]);
        for (uint8_t column = 0; column < 12; ++column) {
          if (bits & (0x0800 >> column))
            display.drawPixel(cursorX + column, y - 11 + row, color);
        }
      }
      cursorX += 12;
    } else {
      // Use a visible missing-character box for uncommon CJK/emoji instead of
      // rendering a misleading question mark in ordinary Chinese text.
      display.drawRect(cursorX + 2, y - 10, 8, 9, color);
      display.drawLine(cursorX + 3, y - 9, cursorX + 8, y - 2, color);
      display.drawLine(cursorX + 8, y - 9, cursorX + 3, y - 2, color);
      cursorX += 12;
    }
  }
}

void textZhAt(int x, int y, const char* text) {
  textZhAtColor(x, y, text, uiBlack());
}

void textZhAndNumberAt(int x, int y, const char* label, int32_t value) {
  textZhAt(x, y, label);
  int width = 0;
  for (const char* p = label; *p;) {
    uint32_t codepoint = nextUtf8Codepoint(p);
    width += codepoint < 0x80 ? (codepoint == ' ' ? 4 : 6) : 12;
  }
  display.setTextSize(1);
  display.setTextColor(uiBlack());
  display.setCursor(x + width, y - 7);
  display.print(value);
}

void drawGreyStrip(int y, int h) {
  int stripWidth = display.width() - 8;
  for (int level = 0; level < 17; ++level) {
    int x0 = 4 + level * stripWidth / 17;
    int x1 = 4 + (level + 1) * stripWidth / 17;
    ditherRect(x0, y, x1 - x0, h, level);
  }
  display.drawRect(3, y - 1, stripWidth + 2, h + 2, uiBlack());
}

void drawPattern() {
  display.fillScreen(uiWhite());
  textAt(4, 4, "E-PAPER TEST / PORTRAIT");
  textAt(4, 18, PROFILE_NAME);
  display.fillRect(4, 34, display.width() - 8, 34, uiBlack());
  display.drawRect(4, 78, display.width() - 8, 34, uiBlack());
  if (HAS_RED) {
    display.fillRect(4, 122, display.width() - 8, 34, GxEPD_RED);
  } else {
    ditherRect(4, 122, display.width() - 8, 34, 8);
    display.drawRect(4, 122, display.width() - 8, 34, uiBlack());
  }
  textAt(4, 68, "BLACK");
  textAt(4, 112, "WHITE");
  textAt(4, 156, HAS_RED ? "RED TEST" : "BW DITHER");
  textAt(4, 174, "17-LEVEL DITHER STRIP");
  drawGreyStrip(190, 48);
  textAt(4, 252, "WHITE -> BLACK");
  textAt(4, 270, "296 x 128 PANEL");
}

void drawGrey() {
  display.fillScreen(uiWhite());
#if EPAPER_PROFILE == 6
  textAt(4, 4, "NATIVE 4 GREY / T94");
  const uint16_t colors[] = {
      uiWhite(), GxEPD_LIGHTGREY, GxEPD_DARKGREY, uiBlack()};
  const char* labels[] = {"WHITE", "LIGHT", "DARK", "BLACK"};
  for (int i = 0; i < 4; ++i) {
    int y = 24 + i * 62;
    display.fillRect(4, y, display.width() - 8, 42, colors[i]);
    display.drawRect(4, y, display.width() - 8, 42, uiBlack());
    textAt(4, y + 50, labels[i]);
  }
  textAt(4, 280, "SOLID PIXELS, NOT DITHER");
#else
  textAt(4, 4, "SIMULATED GREY / BW DITHER");
  textAt(4, 22, "17 COVERAGE LEVELS");
  drawGreyStrip(48, 76);
  textAt(4, 136, "WHITE -> BLACK");
  textAt(4, 160, "DOT COVERAGE TEST");
  textAt(4, 274, "NOT NATIVE GREY");
#endif
}

void drawVersionBadge() {
  if (requestedFrame != Frame::Standby) {
    display.setTextSize(1); display.setTextColor(uiBlack());
    display.setCursor(2, display.height() - 8);
    display.print("UP/DN OK BACK");
  }
  display.setTextSize(1);
  display.setTextColor(uiBlack());
  display.setCursor(display.width() - 4 - strlen(APP_VERSION) * 6, display.height() - 8);
  display.print(APP_VERSION);
}

void drawBitmapGlyphColumn(int x, int y, const uint8_t* bitmap,
                           int bitmapWidth, int bitmapHeight, int firstGlyph) {
  const int glyphWidth = 31;
  const int stride = (bitmapWidth + 7) / 8;
  for (int glyph = 0; glyph < 4; ++glyph) {
    int sourceX = (firstGlyph + glyph) * glyphWidth;
    int destY = y + glyph * 43;
    for (int yy = 0; yy < bitmapHeight; ++yy) {
      for (int xx = 0; xx < glyphWidth; ++xx) {
        int sx = sourceX + xx;
        uint8_t bits = pgm_read_byte(bitmap + yy * stride + sx / 8);
        if (bits & (0x80 >> (sx & 7))) {
          display.drawPixel(x + xx, destY + yy, uiBlack());
        }
      }
    }
  }
}

void drawTextPage() {
  display.fillScreen(uiWhite());
  textZhAt(4, 13, "繁體中文測試");
  drawBitmapGlyphColumn(86, 26, HELLO_ZH, HELLO_ZH_W, HELLO_ZH_H, 0);
  drawBitmapGlyphColumn(40, 26, HELLO_ZH, HELLO_ZH_W, HELLO_ZH_H, 4);
  display.setTextSize(1);
  display.drawFastVLine(78, 24, 190, uiBlack());
  textZhAt(4, 234, "中文顯示正常");
  textZhAt(4, 252, "直式畫面 128x296");
  textZhAt(4, 270, "繁體文字測試完成");
}

void drawMenuPage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  textZhAt(4, 13, "智慧書包");
  display.drawFastHLine(4, 17, 120, uiBlack());
  for (uint8_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    int y = 40 + i * 42;
    if (i == menuSelection) {
      display.drawFastVLine(4, y - 7, 16, uiBlack());
    }
    textZhAt(12, y + 2, MENU_ITEMS[i]);
  }
  display.drawFastHLine(4, 251, 120, uiBlack());
}

const char* gpsStatusChinese(gps_support::Status status);

void drawStatusPage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  textZhAt(4, 13, "GPS狀態");
  display.drawFastHLine(4, 17, 120, uiBlack());

  textZhAt(4, 39, "GPS");
  textZhAt(28, 39, gpsStatusChinese(displayedGPS.status));
  textZhAt(4, 61, "使用衛星");
  display.setCursor(70, 54);
  if (displayedGPS.satellitesKnown) display.print(displayedGPS.satellites);
  else display.print("--");
  textZhAt(4, 81, "可見衛星");
  display.setCursor(70, 74);
  if (displayedGPS.visibleKnown) display.print(displayedGPS.visible);
  else display.print("--");
  textZhAt(4, 101, "最高訊號");
  display.setCursor(70, 94);
  if (displayedGPS.visibleKnown) display.printf("C/N0 %u", displayedGPS.peakCno);
  else display.print("C/N0 --");
  if (displayedGPS.status == gps_support::Status::Fix) {
    textZhAt(4, 123, "緯度");
    display.setCursor(34, 116);
    display.printf("%.6f", displayedGPS.latitude);
    textZhAt(4, 143, "經度");
    display.setCursor(34, 136);
    display.printf("%.6f", displayedGPS.longitude);
  } else {
    textZhAt(4, 123, "尚無有效座標");
  }

  textZhAt(4, 188, "接收鮑率");
  display.setCursor(72, 181);
  display.printf("%lu baud", static_cast<unsigned long>(displayedGPS.baud));
}

void drawSettingsPage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  textZhAt(4, 13, "設定");
  display.drawFastHLine(4, 17, 120, uiBlack());
  const char* const labels[] = {
      "局刷3次後全刷", "局刷4次後全刷", "局刷5次後全刷", "切頁才全刷",
      blackWhiteReflected ? "黑白反射：開" : "黑白反射：關", ""};
  static char standbyLabel[32];
  if (autoStandbyMinutes == 0) snprintf(standbyLabel, sizeof(standbyLabel), "閒置待機：關");
  else snprintf(standbyLabel, sizeof(standbyLabel), "閒置待機：%u分", autoStandbyMinutes);
  for (uint8_t i = 0; i < 6; ++i) {
    const int16_t y = 27 + i * 31;
    if (i == settingsMenuSelection) display.drawFastVLine(3, y + 5, 16, uiBlack());
    textZhAt(8, y + 18, i == 5 ? standbyLabel : labels[i]);
    if (i < 4 && uiRefreshSweepsBeforeFull == (i == 3 ? 0 : i + 3))
      textZhAt(103, y + 18, "目前");
  }
}

void drawCardPage();

void drawChecklistPage() {
  if (pairingArmed) {
    display.fillScreen(uiWhite());
    textZhAt(4, 13, "標籤配對");
    textZhAt(8, 60, CHECK_ITEMS[checklistSelection]);
    textZhAt(8, 105, "請靠近一張RFID");
    textZhAt(8, 150, lastTagMessage);
    return;
  }
  if (checklistDayPickerOpen) {
    display.fillScreen(uiWhite());
    textZhAt(4, 13, "選擇星期");
    const char* names[] = {"星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"};
    for (uint8_t i = 0; i < 7; ++i) {
      const int y = 43 + i * 29;
      if (weekdayPickerSelection == i + 1) display.drawFastVLine(4, y - 10, 15, uiBlack());
      textZhAt(14, y, names[i]);
    }
    return;
  }
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  static const char* const weekdayNames[] = {
      "星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"};
  char title[32];
  snprintf(title, sizeof(title), "%s要帶物品", weekdayNames[selectedWeekday - 1]);
  textZhAt(4, 13, title);
  display.drawFastHLine(4, 17, 120, uiBlack());
  if (!activeChecklistCount) textZhAt(12, 74, "今日沒有指定物品");
  const uint8_t firstVisible = checklistFirstVisiblePosition();
  const uint8_t visibleCount = std::min<uint8_t>(activeChecklistCount - firstVisible,
                                                 CHECKLIST_VISIBLE_ROWS);
  for (uint8_t row = 0; row < visibleCount; ++row) {
    const uint8_t pos = firstVisible + row;
    uint8_t i = activeChecklistItems[pos];
    int y = 42 + row * 38;
    display.drawRect(7, y - 11, 13, 13, uiBlack());
    if (itemChecked[i]) {
      display.drawLine(9, y - 5, 12, y - 1, uiBlack());
      display.drawLine(12, y - 1, 19, y - 10, uiBlack());
    }
    if (i == checklistSelection) {
      display.drawFastVLine(3, y - 11, 15, uiBlack());
    }
    textZhAt(24, y, itemIsBook[i] ? "書" : "物");
    textZhAt(42, y, CHECK_ITEMS[i]);
  }
  if (activeChecklistCount > CHECKLIST_VISIBLE_ROWS) {
    const uint8_t page = checklistFirstVisiblePosition() / CHECKLIST_VISIBLE_ROWS + 1;
    const uint8_t pages = (activeChecklistCount + CHECKLIST_VISIBLE_ROWS - 1) /
                          CHECKLIST_VISIBLE_ROWS;
    display.setCursor(90, 225);
    display.printf("Pg%u/%u", static_cast<unsigned>(page), static_cast<unsigned>(pages));
  }

  textZhAt(4, 259, lastTagMessage);
  textZhAt(4, 271, "已完成");
  display.setCursor(52, 264);
  uint8_t checkedToday = 0;
  for (uint8_t pos = 0; pos < activeChecklistCount; ++pos)
    if (itemChecked[activeChecklistItems[pos]]) ++checkedToday;
  display.printf("%u/%u", checkedToday, activeChecklistCount);
}

void drawSchedulePage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  static const char* const weekdayNames[] = {
      "星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"};
  char title[32];
  snprintf(title, sizeof(title), "課表 %s", weekdayNames[selectedWeekday - 1]);
  textZhAt(4, 13, title);
  display.drawFastHLine(4, 17, 120, uiBlack());
  char lines[12][48] = {};
  char events[52] = {};
  char source[sizeof(weekdayPlans[0])];
  snprintf(source, sizeof(source), "%s", weekdayPlans[selectedWeekday - 1]);
  char* save = nullptr;
  for (char* part = strtok_r(source, "|", &save); part;
       part = strtok_r(nullptr, "|", &save)) {
    if (!strncmp(part, "行.", strlen("行."))) {
      snprintf(events, sizeof(events), "%s", part + strlen("行."));
      continue;
    }
    char* dot = strchr(part, '.');
    if (!dot) continue;
    *dot = '\0';
    const int period = atoi(part);
    if (period >= 1 && period <= 12)
      snprintf(lines[period - 1], sizeof(lines[0]), "%s", dot + 1);
  }
  for (uint8_t i = 0; i < 12; ++i) {
    const int16_t y = 37 + i * 17;
    char periodLabel[12];
    snprintf(periodLabel, sizeof(periodLabel), "%u節", static_cast<unsigned>(i + 1));
    textZhAt(4, y, periodLabel);
    textZhAt(34, y, lines[i][0] ? lines[i] : "—");
    if (i < 11) display.drawFastHLine(4, y + 3, 120, uiBlack());
  }
  if (events[0]) {
    textZhAt(4, 252, "行事");
    textZhAt(34, 252, events);
  }
  textZhAt(4, 272, "上下選星期　左右鍵選日期");
}

void drawWrappedUtf8At(int16_t x, int16_t y, const char* text,
                       uint8_t charsPerLine, uint8_t maxLines) {
  const char* cursor = text;
  for (uint8_t lineIndex = 0; lineIndex < maxLines && *cursor; ++lineIndex) {
    char line[64] = {};
    size_t used = 0;
    uint8_t chars = 0;
    while (*cursor && chars < charsPerLine) {
      uint8_t lead = static_cast<uint8_t>(*cursor);
      size_t width = (lead & 0x80) == 0 ? 1 : (lead & 0xE0) == 0xC0 ? 2 :
                     (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
      if (used + width >= sizeof(line)) break;
      memcpy(line + used, cursor, width);
      used += width;
      cursor += width;
      ++chars;
    }
    line[used] = '\0';
    textZhAt(x, y + lineIndex * 22, line);
  }
}

void drawNotesPage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  textZhAt(4, 13, "便利貼");
  display.drawFastHLine(4, 17, 120, uiBlack());
  if (stickyNoteDetailOpen && stickyNoteCount) {
    char note[STICKY_NOTE_BYTES];
    snprintf(note, sizeof(note), "%s", stickyNotes[stickyNoteSelection]);
    char* body = strchr(note, 0x1e);
    if (body) { *body++ = '\0'; for (char* c = body; *c; ++c) if (*c == 0x1e) *c = ' '; }
    drawWrappedUtf8At(6, 44, note, 9, 2);
    if (body) drawWrappedUtf8At(6, 98, body, 9, 6);
  } else {
    if (!stickyNoteCount) textZhAt(8, 70, "手機新增便利貼後同步");
    for (uint8_t i = 0; i < stickyNoteCount; ++i) {
      const int16_t y = 48 + i * 37;
      if (i == stickyNoteSelection) display.drawFastVLine(4, y - 10, 15, uiBlack());
      char title[STICKY_NOTE_BYTES]; snprintf(title, sizeof(title), "%s", stickyNotes[i]);
      char* divider = strchr(title, 0x1e); if (divider) *divider = '\0';
      drawWrappedUtf8At(9, y, title, 9, 1);
    }
  }
}

void drawTagSetupPage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  textZhAt(4, 13, "標籤配對");
  display.drawFastHLine(4, 17, 120, uiBlack());
  textZhAt(4, 40, "選擇物品");
  textZhAt(4, 61, CHECK_ITEMS[checklistSelection]);
  textZhAt(4, 92, "已綁定 UID:");
  display.setCursor(4, 110);
  if (linkedTagUid[checklistSelection][0]) display.print(linkedTagUid[checklistSelection]);
  else textZhAt(4, 110, "尚無標籤");
  display.drawFastHLine(4, 158, 120, uiBlack());
  textZhAt(4, 178, pairingArmed ? "請刷卡完成配對" : "按OK開始配對");
  textZhAt(4, 198, pairingArmed ? "將標籤靠近讀卡器" : "配對後刷卡自動勾選");
  textZhAt(4, 218, lastTagMessage);
  textZhAt(4, 240, itemChecked[checklistSelection] ? "物品已勾選" : "物品尚未勾選");
  textZhAt(4, 258, "已配對數");
  display.setCursor(64, 251);
  display.print(static_cast<unsigned>(std::count_if(linkedTagUid, linkedTagUid + CHECK_ITEM_COUNT, [](const char* uid) { return uid[0] != '\0'; })));
}

const char* gpsStatusChinese(gps_support::Status status) {
  switch (status) {
    case gps_support::Status::WaitingData: return "等待資料";
    case gps_support::Status::NoFix: return "尚未定位";
    case gps_support::Status::Fix: return "已定位";
    case gps_support::Status::Stale: return "訊號中斷";
    case gps_support::Status::UartError: return "接收失敗";
  }
  return "狀態未知";
}

void drawGpsPage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  textZhAt(4, 13, "GPS定位狀態");
  display.drawFastHLine(4, 17, 120, uiBlack());
  textZhAt(4, 42, gpsStatusChinese(displayedGPS.status));
  textZhAt(4, 64, "衛星 使用:");
  display.setCursor(76, 57);
  if (displayedGPS.satellitesKnown) display.print(displayedGPS.satellites);
  else display.print("--");
  textZhAt(4, 82, "可見:");
  display.setCursor(40, 75);
  if (displayedGPS.visibleKnown) display.print(displayedGPS.visible);
  else display.print("--");
  textZhAt(4, 105, "訊號強度");
  display.setCursor(70, 98);
  display.printf("C/N0:%u", displayedGPS.peakCno);
  textZhAt(4, 135, "緯度:");
  if (displayedGPS.status == gps_support::Status::Fix) {
    display.setCursor(40, 128); display.printf("%.6f", displayedGPS.latitude);
  } else textZhAt(40, 135, "尚無資料");
  textZhAt(4, 157, "經度:");
  if (displayedGPS.status == gps_support::Status::Fix) {
    display.setCursor(40, 150); display.printf("%.6f", displayedGPS.longitude);
  } else textZhAt(40, 157, "尚無資料");
  textZhAt(4, 191, "接收器");
  display.setCursor(48, 184);
  display.printf("RX44 %lu", static_cast<unsigned long>(displayedGPS.baud));
  textZhAt(4, 217, "天線朝天空，戶外較佳");
}

void drawSystemInfoPage() {
  drawCardPage();
}

void drawStandbyPage() {
  display.fillScreen(uiWhite());
  textZhAt(8, 78, "關機待機");
  textZhAt(8, 132, "GPS與RFID已暫停");
#if PERIPHERAL_POWER_GATE_ENABLED
  textZhAt(8, 154, "外設電源已關閉");
#else
  textZhAt(8, 154, "外設仍由電源供電");
#endif
  textZhAt(8, 184, "按OK鍵喚醒");
  textZhAt(8, 214, "USB已拔除時可插入喚醒");
  textZhAt(8, 234, "深睡期間USB序列中斷");
  textZhAt(8, 254, "電子紙保留此畫面");
}

void drawCardPage();

void drawButtonTestPage() {
  drawCardPage();
}

void drawCardPage() {
  display.fillScreen(uiWhite());
  display.setTextSize(1);
  textZhAt(4, 13, "按鍵與硬體狀態");
  display.drawFastHLine(4, 14, 120, uiBlack());
  if (hasButtonEvent) {
    const char* const buttonNamesZh[] = {"上鍵", "下鍵", "左鍵", "右鍵", "OK鍵"};
    textZhAt(4, 36, buttonNamesZh[latestButtonIndex]);
    display.setCursor(48, 29); display.printf("GPIO%u", BUTTON_PINS[latestButtonIndex]);
    textZhAndNumberAt(4, 54, "按下次數:", static_cast<int32_t>(buttonPressCounts[latestButtonIndex]));
  } else {
    textZhAt(4, 36, "請按任一按鍵測試");
    textZhAt(4, 54, "上14 下5 左2");
    textZhAt(4, 68, "右1 OK4");
  }
  display.setCursor(4, 83);
  display.printf("UP14:%lu DN5:%lu L2:%lu",
                 static_cast<unsigned long>(buttonPressCounts[0]),
                 static_cast<unsigned long>(buttonPressCounts[1]),
                 static_cast<unsigned long>(buttonPressCounts[2]));
  display.setCursor(4, 96);
  display.printf("R1:%lu OK4:%lu",
                 static_cast<unsigned long>(buttonPressCounts[3]),
                 static_cast<unsigned long>(buttonPressCounts[4]));
  display.drawFastHLine(4, 108, 120, uiBlack());

  textZhAt(4, 120, "讀卡器:");
  textZhAt(52, 120, readerOnline ? "在線" : "離線");
  textZhAt(4, 134, "卡號:");
  display.setCursor(40, 127);
  if (displayedCard.uidSize) display.printf("%.14s", displayedCard.uid);
  else display.print("--");
  if (displayedCard.uidSize > 19) {
    display.setCursor(4, 148);
    display.print("    "); display.print(displayedCard.uid + 19);
  }
  textZhAndNumberAt(4, 162, "刷卡次數:", static_cast<int32_t>(displayedCard.count));

  display.drawFastHLine(4, 176, 120, uiBlack());
  display.setCursor(4, 188);
  textZhAt(4, 188, "GPS狀態:");
  textZhAt(64, 188, gpsStatusChinese(displayedGPS.status));
  display.setCursor(4, 202);
  display.printf("SAT U:%lu V:%lu CNO:%u",
                 static_cast<unsigned long>(displayedGPS.satellites),
                 static_cast<unsigned long>(displayedGPS.visible), displayedGPS.peakCno);
  textZhAt(4, 220, "緯度:");
  if (displayedGPS.status == gps_support::Status::Fix)
    { display.setCursor(40, 213); display.printf("%.6f", displayedGPS.latitude); }
  else textZhAt(40, 220, "尚無資料");
  textZhAt(4, 236, "經度:");
  if (displayedGPS.status == gps_support::Status::Fix)
    { display.setCursor(40, 229); display.printf("%.6f", displayedGPS.longitude); }
  else textZhAt(40, 236, "尚無資料");
  display.setCursor(4, 252);
  display.printf("RX44 %lu baud", static_cast<unsigned long>(displayedGPS.baud));
  display.setCursor(4, 266);
  display.printf("NMEA:%lu CRC:%u", static_cast<unsigned long>(gps_support::totalNmea),
                 static_cast<unsigned>(gps_support::parser.failedChecksum()));
}

void drawReaderPage(bool online) {
  (void)online;
  drawCardPage();
}

void busyCallback(const void*) {
  ++busyPolls;
  if (static_cast<uint32_t>(millis() - operationStartedMs) >=
      OPERATION_TIMEOUT_MS) {
    digitalWrite(EPD_CS, HIGH);
    digitalWrite(EPD_RST, LOW);
    state.store(State::Fault);
    // 只停住電子紙工作，主程式與 USB 仍可回應；不得再送指令。
    // 重新測試前須實體斷電並查核接線與驅動設定。
    vTaskSuspend(nullptr);
  }
  delay(1);  // 回呼取代函式庫原本的 delay，必須讓出 CPU。
}

bool isInteractiveUiFrame(Frame frame) {
  return frame == Frame::Menu || frame == Frame::Checklist ||
         frame == Frame::Schedule || frame == Frame::Notes ||
         frame == Frame::Status || frame == Frame::Settings;
}

void displayTask(void*) {
  operationStartedMs = millis();
  busyPolls = 0;
  bool initialPanelRefresh = !panelPrepared;
  if (!panelPrepared) {
    display.epd2.setBusyCallback(busyCallback);
    display.init(0, true, 2, false, SPI,
                 SPISettings(10000000, MSBFIRST, SPI_MODE0));
    panelPrepared = true;
  }
  display.setRotation(2);  // Portrait with 180-degree correction for this panel mounting.
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(uiBlack());
  display.setFullWindow();
  switch (requestedFrame) {
    case Frame::White: display.fillScreen(uiWhite()); break;
    case Frame::Black: display.fillScreen(uiBlack()); break;
    case Frame::Grey: drawGrey(); break;
    case Frame::Pattern: drawPattern(); break;
    case Frame::Text: drawTextPage(); break;
    case Frame::Card: drawCardPage(); break;
    case Frame::ReaderReady: drawReaderPage(true); break;
    case Frame::ReaderError: drawReaderPage(false); break;
    case Frame::GPS: drawGpsPage(); break;
    case Frame::ButtonTest: drawButtonTestPage(); break;
    case Frame::Menu: drawMenuPage(); break;
    case Frame::Checklist: drawChecklistPage(); break;
    case Frame::Schedule: drawSchedulePage(); break;
    case Frame::Notes: drawNotesPage(); break;
    case Frame::Status: drawStatusPage(); break;
    case Frame::Settings: drawSettingsPage(); break;
    case Frame::TagSetup: drawTagSetupPage(); break;
    case Frame::SystemInfo: drawSystemInfoPage(); break;
    case Frame::Standby: drawStandbyPage(); break;
  }
  if (isInteractiveUiFrame(requestedFrame) || requestedFrame == Frame::Standby) {
    drawVersionBadge();
  }
  bool appScreenFrame = isInteractiveUiFrame(requestedFrame) || isCardFrame(requestedFrame);
  bool forceStandardThisFrame = forceStandardRefreshNext && appScreenFrame;
  bool pageChangeFullRefreshThisFrame = appScreenFrame &&
      (!hasLastAppScreenFrame || requestedFrame != lastAppScreenFrame);
  bool cycleFullRefreshThisFrame = appScreenFrame && uiRefreshSweepsBeforeFull != 0 &&
      partialUpdatesSinceFullRefresh >= uiRefreshSweepsBeforeFull;
  bool canSweepInteractiveScreen =
                                   (isInteractiveUiFrame(requestedFrame) ||
                                    (uiRefreshSweepsBeforeFull == 0 && isCardFrame(requestedFrame))) &&
                                   !forceStandardThisFrame && !initialPanelRefresh &&
                                   !pageChangeFullRefreshThisFrame &&
                                   !cycleFullRefreshThisFrame &&
                                   display.epd2.hasFastPartialUpdate;
  if (canSweepInteractiveScreen) {
    // Submit the complete final canvas once. No directional stripe animation.
    display.displayWindow(0, 0, display.width(), display.height());
    display.powerOff();
    lastFramePartialWindowRefresh = true;
    lastFrameStandardCleanupAfterPartials = false;
    ++partialUpdatesSinceFullRefresh;
  } else {
    if (initialPanelRefresh) {
      Serial.println("[EPD] 面板初次初始化，使用必要的標準全波形刷新。");
    } else if (forceStandardThisFrame) {
      Serial.println("[EPD] 手動要求標準全波形刷新。");
    } else if (requestedFrame == Frame::Standby) {
      Serial.println("[EPD] POWER OFF 待機頁使用標準全波形刷新。");
    } else if (pageChangeFullRefreshThisFrame) {
      Serial.println("[EPD] 進入不同應用畫面，使用一次標準全波形整頁刷新。");
    } else if (cycleFullRefreshThisFrame) {
      Serial.printf("[EPD] 前 %u 次整頁單次局刷已完成；本次直接標準全波形整頁刷新。\n",
                    static_cast<unsigned>(uiRefreshSweepsBeforeFull));
    } else if (appScreenFrame) {
      Serial.println("[EPD] 使用一次標準全波形整頁刷新。");
    }
    display.display(false);
    lastFramePartialWindowRefresh = false;
    lastFrameStandardCleanupAfterPartials = false;
    if (appScreenFrame) partialUpdatesSinceFullRefresh = 0;
    if (forceStandardThisFrame) forceStandardRefreshNext = false;
  }
  if (!appScreenFrame) {
    display.hibernate();
    panelPrepared = false;
  }
  if (appScreenFrame) {
    lastAppScreenFrame = requestedFrame;
    hasLastAppScreenFrame = true;
  }
  completedMs = millis();
  state.store(State::Done);
  vTaskDelete(nullptr);
}

uint32_t cooldownMs() {
  if (!hasCompletedFrame) return 0;
  uint32_t elapsed = millis() - completedMs;
  return elapsed >= REFRESH_INTERVAL_MS ? 0 : REFRESH_INTERVAL_MS - elapsed;
}

uint32_t cardCooldownMs() {
  if (!hasCompletedFrame) return 0;
  uint32_t interval = CARD_REFRESH_INTERVAL_MS;
  uint32_t elapsed = millis() - completedMs;
  return elapsed >= interval ? 0 : interval - elapsed;
}

void initButtons() {
  uint32_t now = millis();
  lastUserActivityMs = now;
  for (unsigned i = 0; i < sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]); ++i) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    bool level = digitalRead(BUTTON_PINS[i]);
    buttons[i].rawLevel = level;
    buttons[i].stableLevel = level;
    buttons[i].changedAtMs = now;
    buttons[i].pressedAtMs = level == LOW ? now : 0;
    buttons[i].pressActive = level == LOW;
  }
  Serial.println("[BUTTON] 測試順序：UP=GPIO14 DOWN=GPIO5 LEFT=GPIO2 RIGHT=GPIO1 OK=GPIO4；INPUT_PULLUP，按下接 GND。");
  Serial.println("主選單：今日要帶、課表與行事、便利貼、GPS狀態、設定；按右鍵或OK進入，左鍵返回。");
  Serial.println("[BUTTON] UP/DOWN 選單、LEFT 返回、RIGHT/OK 開啟；快速連按時合併導航，直接更新最後選中的頁面。");
}

void handleShortButton(uint8_t index) {
  if (uiPage == UiPage::Menu) {
    uint8_t oldSelection = menuSelection;
    if (index == 0) menuSelection = (menuSelection + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
    else if (index == 1) menuSelection = (menuSelection + 1) % MENU_ITEM_COUNT;
    else if (index == 3 || index == 4) {
      uiPage = UiPage::Detail;
      if (menuSelection == 0) { checklistDayPickerOpen = true; weekdayPickerSelection = selectedWeekday; }
      if (menuSelection == 2) stickyNoteDetailOpen = false; forceStandardRefreshNext = true;
      forceStandardRefreshNext = true;
      markUiFullRefresh();
      return;
    }
    else return;
    markMenuRowChange(oldSelection, menuSelection);
    return;
  }
  if (menuSelection == 0 && checklistDayPickerOpen) {
    if (index == 2) { uiPage = UiPage::Menu; forceStandardRefreshNext = true; markUiFullRefresh(); return; }
    if (index == 0 || index == 1) {
      weekdayPickerSelection = (weekdayPickerSelection + (index == 1 ? 0 : 5)) % 7 + 1;
      markUiFullRefresh(); return;
    }
    if (index == 3 || index == 4) {
      activateWeekday(weekdayPickerSelection);
      manualWeekdayOverride = true;
      if (preferencesReady) { nvs_set_u8(appNvsHandle, "selWd", selectedWeekday); nvs_set_u8(appNvsHandle, "manWd", 1); nvs_commit(appNvsHandle); }
      checklistDayPickerOpen = false;
      forceStandardRefreshNext = true; markUiFullRefresh(); return;
    }
    return;
  }
  if (menuSelection == 4) {
    if (index == 0 || index == 1) {
      settingsMenuSelection = static_cast<uint8_t>(
          (settingsMenuSelection + (index == 1 ? 1 : 5)) % 6);
      markUiFullRefresh();
      return;
    }
    if (index == 2) {
      uiPage = UiPage::Menu;
      markUiFullRefresh();
      return;
    }
    if (index == 3 || index == 4) {
      if (settingsMenuSelection == 4) {
        toggleBlackWhiteReflection();
      } else if (settingsMenuSelection < 4) {
        settingsRefreshSelection = settingsMenuSelection;
        applyRefreshSetting();
      } else {
        static const uint8_t standbyOptions[] = {0, 5, 10, 15, 30, 60};
        uint8_t option = 0;
        while (option < sizeof(standbyOptions) &&
               standbyOptions[option] != autoStandbyMinutes) ++option;
        option = static_cast<uint8_t>((option + 1) % (sizeof(standbyOptions) / sizeof(standbyOptions[0])));
        autoStandbyMinutes = standbyOptions[option];
        if (preferencesReady) {
          esp_err_t result = nvs_set_u8(appNvsHandle, "idleM", autoStandbyMinutes);
          if (result == ESP_OK) result = nvs_commit(appNvsHandle);
          if (result != ESP_OK) Serial.printf("[POWER] 待機計時設定保存失敗：%s\n", esp_err_to_name(result));
        }
        lastUserActivityMs = millis();
        markUiFullRefresh();
      }
      return;
    }
  }
  if (menuSelection == 1 && (index == 0 || index == 1)) {
    const int next = static_cast<int>(selectedWeekday) + (index == 1 ? 1 : -1);
    selectedWeekday = static_cast<uint8_t>(next < 1 ? 7 : next > 7 ? 1 : next);
    manualWeekdayOverride = true;
    activateWeekday(selectedWeekday);
    if (preferencesReady) {
      nvs_set_u8(appNvsHandle, "selWd", selectedWeekday);
      nvs_set_u8(appNvsHandle, "manWd", 1);
      nvs_commit(appNvsHandle);
    }
    markUiFullRefresh();
    Serial.printf("[WEEK] 離線選擇星期 %u；今日清單同步切換。\n", selectedWeekday);
    return;
  }
  if (menuSelection == 2) {
    if (index == 2) {
      if (stickyNoteDetailOpen) {
        stickyNoteDetailOpen = false; forceStandardRefreshNext = true;
        markUiFullRefresh();
      } else {
        uiPage = UiPage::Menu;
        markUiFullRefresh();
      }
      return;
    }
    if (!stickyNoteDetailOpen && stickyNoteCount && (index == 0 || index == 1)) {
      const uint8_t oldSelection = stickyNoteSelection;
      stickyNoteSelection = static_cast<uint8_t>(
          (stickyNoteSelection + (index == 1 ? 1 : MAX_STICKY_NOTES - 1)) % stickyNoteCount);
      markUiWindow(4, 33 + oldSelection * 30, 120, 25);
      markUiWindow(4, 33 + stickyNoteSelection * 30, 120, 25);
      return;
    }
    if (!stickyNoteDetailOpen && stickyNoteCount && (index == 3 || index == 4)) {
      stickyNoteDetailOpen = true; forceStandardRefreshNext = true;
      markUiFullRefresh();
      return;
    }
  }
  if (index == 2) {
    if (menuSelection == 0 && !checklistDayPickerOpen) {
      checklistDayPickerOpen = true;
      weekdayPickerSelection = selectedWeekday;
      pairingArmed = false;
      forceStandardRefreshNext = true; markUiFullRefresh(); return;
    }
    pairingArmed = false;
    uiPage = UiPage::Menu;
    markUiFullRefresh();
    return;
  }
  if (menuSelection == 0) {
    if (!activeChecklistCount) return;
    uint8_t oldSelection = checklistSelection;
    if (index == 0) { checklistSelection = moveChecklistSelection(checklistSelection, -1); markChecklistRowChange(oldSelection, checklistSelection); return; }
    else if (index == 1) { checklistSelection = moveChecklistSelection(checklistSelection, 1); markChecklistRowChange(oldSelection, checklistSelection); return; }
    else if (index == 3) { jumpChecklistPage(1); return; }
    else if (index == 4) {
      itemChecked[checklistSelection] = !itemChecked[checklistSelection];
      if (!uiDiagnosticActive) {
        esp_err_t saveResult = saveChecklistItem(checklistSelection);
        if (saveResult != ESP_OK) {
          itemChecked[checklistSelection] = !itemChecked[checklistSelection];
          rollbackChecklistItemNvs(checklistSelection);
          notifyNvsFailure(saveResult);
        }
        notifyChecklistItem(checklistSelection);
      }
      int8_t row = checklistVisibleRow(checklistSelection);
      if (row >= 0) markUiWindow(3, 42 + row * 38 - 16, 122, 29);
      markUiWindow(3, 264, 122, 18);
      return;
    }
    else return;
  }
}

void dispatchButtonAction(uint8_t index) {
  if (index >= sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0])) return;
  if (state.load() == State::Idle) {
    handleShortButton(index);
    return;
  }
  if (queuedButtonCount >= BUTTON_ACTION_QUEUE_CAPACITY) {
    // At capacity, discard the oldest stale navigation action so the newest
    // physical input is still honored when the display becomes ready.
    queuedButtonHead = (queuedButtonHead + 1) % BUTTON_ACTION_QUEUE_CAPACITY;
    --queuedButtonCount;
    Serial.println("[BUTTON] 操作佇列已滿，捨棄最舊操作並保留新按鍵。");
  }
  uint8_t tail = (queuedButtonHead + queuedButtonCount) % BUTTON_ACTION_QUEUE_CAPACITY;
  queuedButtonActions[tail] = index;
  ++queuedButtonCount;
  Serial.printf("[BUTTON] GPIO%u 已排入佇列（%u 筆），刷新完成後執行。\n",
                BUTTON_PINS[index], queuedButtonCount);
}

bool dispatchQueuedButtonAction() {
  if (!queuedButtonCount || state.load() != State::Idle) return false;
  uint8_t index = queuedButtonActions[queuedButtonHead];
  queuedButtonHead = (queuedButtonHead + 1) % BUTTON_ACTION_QUEUE_CAPACITY;
  --queuedButtonCount;
  Serial.printf("[BUTTON] 執行待處理操作 GPIO%u（剩餘 %u 筆）。\n",
                BUTTON_PINS[index], queuedButtonCount);
  handleShortButton(index);
  return true;
}

void runUiDiagnostic() {
  if (!uiDiagnosticActive || state.load() != State::Idle || uiDirty ||
      millis() - lastButtonRefreshMs < BUTTON_REFRESH_GAP_MS) return;
  switch (uiDiagnosticStep) {
    case 0: Serial.println("[UI-TEST] 1/13：主選單選取列變動區局刷"); handleShortButton(1); break;
    case 1: Serial.println("[UI-TEST] 2/13：切換 RFID 標籤配對頁"); handleShortButton(4); break;
    case 2: Serial.println("[UI-TEST] 3/13：RFID 配對提示區局刷"); handleShortButton(1); break;
    case 3: Serial.println("[UI-TEST] 4/13：返回主選單"); handleShortButton(2); break;
    case 4: Serial.println("[UI-TEST] 5/13：主選單選取列變動區局刷"); handleShortButton(0); break;
    case 5: Serial.println("[UI-TEST] 6/13：進入今日清單"); handleShortButton(4); break;
    case 6: case 7: case 8: case 9: case 10:
      Serial.printf("[UI-TEST] %u/13：今日清單核取狀態更新（局刷或累積）\n",
                    static_cast<unsigned>(uiDiagnosticStep + 1));
      handleShortButton(4);
      break;
    case 11: {
      Serial.println("[UI-TEST] 12/13：還原原勾選狀態，局刷該項目與完成數");
      itemChecked[0] = uiDiagnosticOriginalChecked;
      {
        int8_t row = checklistVisibleRow(0);
        if (row >= 0) markUiWindow(3, 42 + row * 38 - 16, 122, 29);
      }
      markUiWindow(3, 264, 122, 18);
      break;
    }
    case 12: Serial.println("[UI-TEST] 13/13：返回主選單"); handleShortButton(2); break;
    default:
      uiDiagnosticActive = false;
      Serial.println("[UI-TEST] 流程完成；這只驗證序列觸發的畫面刷新，不測實體按鍵 GPIO。");
      return;
  }
  ++uiDiagnosticStep;
}

void pollButtons() {
  if (standbyRequested || uiDiagnosticActive) return;
  uint32_t now = millis();
  bool displayBusy = state.load() != State::Idle;
  for (unsigned i = 0; i < sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]); ++i) {
    bool level = digitalRead(BUTTON_PINS[i]);
    if (i == 4 && suppressOkUntilRelease) {
      if (level == HIGH) {
        suppressOkUntilRelease = false;
        buttons[i].rawLevel = buttons[i].stableLevel = HIGH;
        buttons[i].pressActive = buttons[i].actionHandled = false;
      }
      continue;
    }
    if (i == 4 && buttons[i].pressActive && !buttons[i].actionHandled &&
        level == LOW && now - buttons[i].pressedAtMs >= BUTTON_LONG_PRESS_MS) {
      buttons[i].actionHandled = true;
      if (displayBusy) queuedStandbyRequest = true;
      else standbyRequested = true;
      sequenceActive = false;
      Serial.println("[POWER] OK held 1500ms; standby requested before release.");
      return;
    }
    if (level != buttons[i].rawLevel) {
      buttons[i].rawLevel = level;
      buttons[i].changedAtMs = now;
    }
    if (buttons[i].stableLevel != buttons[i].rawLevel &&
        now - buttons[i].changedAtMs >= BUTTON_DEBOUNCE_MS) {
      buttons[i].stableLevel = buttons[i].rawLevel;
      if (buttons[i].stableLevel == LOW) {
        buttons[i].pressedAtMs = now;
        lastUserActivityMs = now;
        buttons[i].pressActive = true;
        latestButtonIndex = static_cast<uint8_t>(i);
        hasButtonEvent = true;
        ++buttonPressCounts[i];
        ++buttonTestGeneration;
        ++buttonEventSequence;
        if (i < 5 && state.load() == State::Running &&
            isInteractiveUiFrame(requestedFrame)) {
          uiNavigationInterrupt.store(true);
        }
        Serial.printf("[BUTTON] SEQ=%lu t=%lu ms %s GPIO%u PRESSED count=%lu\n",
                      static_cast<unsigned long>(buttonEventSequence), static_cast<unsigned long>(now),
                      BUTTON_NAMES[i], BUTTON_PINS[i],
                      static_cast<unsigned long>(buttonPressCounts[i]));
        // Direction keys act as soon as the press is debounced. While BUSY,
        // preserve the action for replay after the current frame completes.
        if (i != 4) {
          dispatchButtonAction(static_cast<uint8_t>(i));
          buttons[i].actionHandled = true;
        }
      } else if (buttons[i].pressActive) {
        uint32_t heldMs = now - buttons[i].pressedAtMs;
        buttons[i].pressedAtMs = 0;
        buttons[i].pressActive = false;
        Serial.printf("[BUTTON] SEQ=%lu t=%lu ms %s GPIO%u RELEASED held=%lu ms count=%lu\n",
                      static_cast<unsigned long>(buttonEventSequence), static_cast<unsigned long>(now),
                      BUTTON_NAMES[i], BUTTON_PINS[i],
                      static_cast<unsigned long>(heldMs),
                      static_cast<unsigned long>(buttonPressCounts[i]));
        if (i == 4 && !buttons[i].actionHandled && heldMs >= BUTTON_LONG_PRESS_MS) {
          if (displayBusy) queuedStandbyRequest = true;
          else standbyRequested = true;
          sequenceActive = false;
          Serial.printf("[BUTTON] GPIO%u long press (%lu ms); standby queued\n",
                        BUTTON_PINS[i], static_cast<unsigned long>(heldMs));
        } else if (!buttons[i].actionHandled) {
          // OK short press is resolved on release so a long press can request standby.
          dispatchButtonAction(static_cast<uint8_t>(i));
        }
        buttons[i].actionHandled = false;
      }
    }
  }
}

bool startFrame(Frame frame, bool buttonRequest = false) {
  if (state.load() != State::Idle) {
    Serial.println("目前正在刷新，或已發生故障；未啟動新測試。");
    return false;
  }
  uint32_t waitMs = isCardFrame(frame) ? cardCooldownMs() : cooldownMs();
  if (!buttonRequest && waitMs) {
    Serial.printf("需再等待 %lu 秒才能再次全刷新。\n",
                  static_cast<unsigned long>((waitMs + 999) / 1000));
    return false;
  }
  if (buttonRequest && frame != Frame::Standby && lastButtonRefreshMs &&
      millis() - lastButtonRefreshMs < BUTTON_REFRESH_GAP_MS) return false;
  if (isCardFrame(frame)) {
    displayedCard = latestCard;
    displayedGPS = gps_support::view;
    gps_support::dirty = false;
    gps_support::urgent = false;
    lastGpsScreenMs = millis();
  }
  if (frame == Frame::Status) {
    displayedGPS = gps_support::view;
    gps_support::dirty = false;
    gps_support::urgent = false;
    lastStatusRefreshMs = millis();
  }
  requestedFrame = frame;
  uiNavigationInterrupt.store(false);
  state.store(State::Running);
  Serial.printf("開始測試，設定 %d：%s；等待實際畫面，通常需數秒至數十秒。\n",
                EPAPER_PROFILE, PROFILE_NAME);
  BaseType_t result = xTaskCreatePinnedToCore(displayTask, "epaper-test", 8192,
                                            nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);
  if (result != pdPASS) {
    state.store(State::Idle);
    Serial.println("無法建立刷新工作；未送出面板指令。");
    return false;
  }
  if (buttonRequest) lastButtonRefreshMs = millis();
  return true;
}

void menu() {
  Serial.printf("\n設定 %d：%s\n", EPAPER_PROFILE, PROFILE_NAME);
  Serial.println("此設定由 PanelConfig.h 手動指定，並非自動辨識結果。");
  Serial.println("t：黑白／紅色色塊與點陣灰階測試圖");
  Serial.println("n：繁體中文與英文文字頁");
  Serial.println("a：開啟刷卡自動顯示；p：暫停自動顯示；c：顯示最近一次讀卡");
  Serial.println("r：重新初始化 RC522；f：下一張刷卡畫面改為全刷新");
  Serial.println("j：完整 GPS 診斷報告；b：重新搜尋 GPS 鮑率（僅接收）");
  Serial.println("o：暫停 RC522 射頻以測試 GPS；i：恢復 RC522 射頻");
  Serial.println("u：閒置時進入 GPS 原始位元組鏡像，暫停其他工作；v：離開鏡像");
  Serial.println("z：待機診斷，走 POWER OFF 全刷與深度睡眠流程；USB 已接時需按 RESET 復原，不驗證實體 OK 長按喚醒。");
  Serial.println("實體選單：UP/DOWN 選擇、RIGHT/OK 開啟、LEFT 返回；OK 長按 1.5 秒全刷 POWER OFF 頁後進深度睡眠，OK 長按喚醒。");
  Serial.println("USB CDC/JTAG 深睡中會中斷；Nologo GPIO3 VBUS 在睡前拔除時可作 USB 插入喚醒，USB 已接則只用 OK 喚醒。外設實體斷電需負載開關。");
  Serial.println("按鈕接法：各 GPIO 與 GND 間接一顆常開按鈕，使用內部上拉；不可接 3.3V 或 5V 到 GPIO。");
  Serial.println("w：全白；k：全黑；g：灰階圖");
  if (uiRefreshSweepsBeforeFull == 0) {
    Serial.println("x：13 步畫面診斷；同頁永遠單次局刷，切換頁面才全刷；q 可取消。");
  } else {
    Serial.printf("x：13 步畫面診斷；目前前 %u 次整頁整頁單次局刷，之後直接整頁全刷；q 可取消。\n",
                  static_cast<unsigned>(uiRefreshSweepsBeforeFull));
  }
  Serial.println("s：白→黑→測試圖→灰階，僅一輪");
  Serial.println("q：取消後續排程；目前刷新會完成後休眠");
  Serial.println("h：顯示選單；換行與空白不影響指令");
  if (NATIVE_GREY) {
    Serial.println("g 使用 GDEM029T94 專用四階灰階波形。");
  } else {
    Serial.println("g 為黑白點陣模擬灰階，不代表支援硬體四階灰階。");
  }
  if (!HAS_RED) {
    Serial.println("目前是黑白模式，沒有送紅色資料，不能據此排除三色面板。");
  }
  Serial.printf("執行 %lu 秒，重置原因代碼 %d，工作狀態 %d。\n",
                static_cast<unsigned long>(millis() / 1000),
                static_cast<int>(esp_reset_reason()), static_cast<int>(state.load()));
  Serial.printf("[USB] VBUS sense GPIO3=%d (0=disconnected, 1=VBUS present; Nologo board divider only)\n",
                digitalRead(USB_VBUS_SENSE_PIN));
  Serial.printf("RC522=%s / FW=0x%02X，讀卡=%lu，自動顯示=%s，區域局刷=ON\n",
                readerOnline ? "ONLINE" : "OFFLINE", readerVersion,
                static_cast<unsigned long>(latestCard.count), cardDisplayEnabled ? "ON" : "OFF");
  if (latestCard.uidSize) Serial.printf("最近 UID：%s，%s\n", latestCard.uid, shortCardType(latestCard.type));
  gps_support::printStatus();
}

esp_err_t configureOkWakeRtcPin() {
  gpio_num_t wakePin = static_cast<gpio_num_t>(BUTTON_PINS[4]);
  esp_err_t result = rtc_gpio_init(wakePin);
  if (result != ESP_OK) return result;
  result = rtc_gpio_set_direction(wakePin, RTC_GPIO_MODE_INPUT_ONLY);
  if (result != ESP_OK) return result;
  result = rtc_gpio_pullup_en(wakePin);
  if (result != ESP_OK) return result;
  return rtc_gpio_pulldown_dis(wakePin);
}

esp_err_t configureUsbWakeRtcPin() {
  gpio_num_t wakePin = static_cast<gpio_num_t>(USB_VBUS_SENSE_PIN);
  esp_err_t result = rtc_gpio_init(wakePin);
  if (result != ESP_OK) return result;
  result = rtc_gpio_set_direction(wakePin, RTC_GPIO_MODE_INPUT_ONLY);
  if (result != ESP_OK) return result;
  // GPIO3 已由板載 VBUS 分壓器偏壓；不再加內部上拉／下拉。
  result = rtc_gpio_pullup_dis(wakePin);
  if (result != ESP_OK) return result;
  return rtc_gpio_pulldown_dis(wakePin);
}

void enterStandby() {
  if (readerOnline) {
    reader.PCD_AntennaOff();
    reader.PCD_SoftPowerDown();
  }
  readerRFEnabled = false;
  gps_support::end();
  // Stop driving external signal lines before deep sleep to reduce back-power paths.
  const uint8_t externalPins[] = {EPD_CS, EPD_DC, EPD_RST, EPD_BUSY,
                                  RFID_CS_PIN, RFID_RST_PIN,
                                  SPI_MOSI_PIN, SPI_SCK_PIN, SPI_MISO_PIN,
                                  GPS_RX_PIN};
  for (uint8_t pin : externalPins) pinMode(pin, INPUT);
#if PERIPHERAL_POWER_GATE_ENABLED
  digitalWrite(PERIPHERAL_POWER_GATE_PIN, LOW);
#endif
  Serial.println("[POWER] 電子紙保留 POWER OFF 頁；GPS UART 關閉、RC522 射頻與晶片進入省電，ESP32 即將深度睡眠。外設電源軌仍需外接負載開關才能物理斷電。");
  Serial.flush();
  standbyAwaitRelease = digitalRead(BUTTON_PINS[4]) == LOW;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_num_t wakePin = static_cast<gpio_num_t>(BUTTON_PINS[4]);
  esp_err_t result = configureOkWakeRtcPin();
  if (result == ESP_OK) result = esp_sleep_enable_ext0_wakeup(wakePin, standbyAwaitRelease ? 1 : 0);
  // USB Serial/JTAG 本身不能喚醒深睡；板載 GPIO3 VBUS 分壓可作 RTC EXT1 高電位喚醒。
  // USB 已插著時不啟用 EXT1，避免進睡眠後因 VBUS 維持高電位而立刻喚醒。
  bool armUsbWake = digitalRead(USB_VBUS_SENSE_PIN) == LOW;
  if (result == ESP_OK && armUsbWake) result = configureUsbWakeRtcPin();
  if (result == ESP_OK && armUsbWake) {
    result = esp_sleep_enable_ext1_wakeup(1ULL << USB_VBUS_SENSE_PIN,
                                          ESP_EXT1_WAKEUP_ANY_HIGH);
  }
  if (result != ESP_OK) {
    Serial.printf("[POWER] GPIO4 喚醒設定失敗 (%s)，維持低活動等待。\n", esp_err_to_name(result));
    while (true) delay(1000);
  }
  Serial.printf("[POWER] 深度睡眠；OK(GPIO4) 可喚醒；USB VBUS(GPIO3) 喚醒%s。USB CDC/JTAG 在深睡期間仍會中斷。\n",
                armUsbWake ? "已啟用（僅睡眠後插入 USB）" : "未啟用（睡前 USB 已連接）");
  Serial.flush();
  esp_deep_sleep_start();
}

void validateDeepSleepWake() {
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause != ESP_SLEEP_WAKEUP_EXT0 && wakeCause != ESP_SLEEP_WAKEUP_EXT1) return;
  rtc_gpio_deinit(static_cast<gpio_num_t>(USB_VBUS_SENSE_PIN));
  rtc_gpio_deinit(static_cast<gpio_num_t>(BUTTON_PINS[4]));
  pinMode(USB_VBUS_SENSE_PIN, INPUT);
  pinMode(BUTTON_PINS[4], INPUT_PULLUP);
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0) {
    if (!standbyAwaitRelease) {
      const uint32_t holdStart = millis();
      while (digitalRead(BUTTON_PINS[4]) == LOW &&
             millis() - holdStart < BUTTON_LONG_PRESS_MS) delay(10);
      if (digitalRead(BUTTON_PINS[4]) == LOW) {
        suppressOkUntilRelease = true;
        return;
      }
    }
    // The release after shutdown only arms the next press; it never boots the UI.
    standbyAwaitRelease = false;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    configureOkWakeRtcPin();
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(BUTTON_PINS[4]), 0);
    if (digitalRead(USB_VBUS_SENSE_PIN) == LOW) {
      configureUsbWakeRtcPin();
      esp_sleep_enable_ext1_wakeup(1ULL << USB_VBUS_SENSE_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    }
    esp_deep_sleep_start();
  }
  // EXT0/EXT1 is already a wake event. Returning into setup avoids re-sleeping
  // on a short press because boot time made the post-boot hold timer inaccurate.
}

void begin() {
  pinMode(USB_VBUS_SENSE_PIN, INPUT);
  validateDeepSleepWake();
  esp_log_level_set("*", ESP_LOG_NONE);
  Serial0.end();  // GPIO43 必須留給 DC，不能繼續輸出 UART0 日誌。
#if PERIPHERAL_POWER_GATE_ENABLED
  gpio_set_level(static_cast<gpio_num_t>(PERIPHERAL_POWER_GATE_PIN), 1);
  pinMode(PERIPHERAL_POWER_GATE_PIN, OUTPUT);
  delay(50);  // Allow the switched rails to settle before initializing peripherals.
#endif
  // Arduino core 3.x 的 digitalWrite 在 pinMode 前可能不設定輸出鎖存器；
  // 先用 GPIO 驅動器預載 HIGH，避免切換成輸出時短暫選取周邊。
  gpio_set_level(static_cast<gpio_num_t>(EPD_CS), 1);
  pinMode(EPD_CS, OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(RFID_CS_PIN), 1);
  pinMode(RFID_CS_PIN, OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(RFID_RST_PIN), 1);
  pinMode(RFID_RST_PIN, OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(EPD_RST), 1);
  pinMode(EPD_RST, OUTPUT);
  pinMode(EPD_BUSY, INPUT);
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 2500) delay(10);
  initButtons();
  initReader();
  gps_support::begin();
  loadSchoolbagTags();
  initBle();
  initWifiControl();
  menu();
  uiPage = UiPage::Menu;
  menuSelection = 0;
  markUiFullRefresh();
  Serial.println("準備顯示功能清單；UP/DOWN 選擇、RIGHT/OK 開啟、LEFT 返回。刷卡資料仍會在背景接收。");
}

void poll() {
  wifiHttpServer.handleClient();
  pollBle();
  gps_support::poll();
  static uint32_t lastWeekdayCheckMs = 0;
  if (!manualWeekdayOverride && rtcTimeValid() &&
      millis() - lastWeekdayCheckMs >= 10000UL) {
    lastWeekdayCheckMs = millis();
    const uint8_t rtcWeekday = weekdayFromRtc();
    if (rtcWeekday != selectedWeekday) {
      activateWeekday(rtcWeekday);
      if (preferencesReady) {
        nvs_set_u8(appNvsHandle, "selWd", selectedWeekday);
        nvs_commit(appNvsHandle);
      }
      if (uiPage == UiPage::Detail && (menuSelection == 0 || menuSelection == 1))
        markUiFullRefresh();
      Serial.printf("[TIME] RTC 換日；已切換至星期 %u 的課表與清單。\n", selectedWeekday);
    }
  }
  if (bleEventCharacteristic && millis() - lastBleGpsNotifyMs >= 30000UL) {
    lastBleGpsNotifyMs = millis();
    const gps_support::View& g = gps_support::view;
    char gpsMessage[120];
    if (g.status == gps_support::Status::Fix)
      snprintf(gpsMessage, sizeof(gpsMessage), "GPS\tFIX\t%u\t%.6f\t%.6f", g.satellites, g.latitude, g.longitude);
    else snprintf(gpsMessage, sizeof(gpsMessage), "GPS\tNOFIX\t%u", g.satellites);
    bleNotify(gpsMessage);
  }
  if (gps_support::rawOutput) {
    while (Serial.available()) {
      char command = Serial.read();
      if (command == 'v' || command == 'V') {
        gps_support::rawOutput = false;
        Serial.println("\n[GPS RAW OFF] 已恢復一般工作。");
      }
    }
    delay(2);
    return;
  }
  pollButtons();
  if (state.load() == State::Done) {
    hasCompletedFrame = true;
    state.store(State::Idle);
    if (requestedFrame == Frame::Standby) standbyFrameComplete = true;
    const char* refreshDescription = lastFramePartialWindowRefresh
                                         ? "整頁單次局刷"
                                         : "單次標準全波形整頁刷新";
    Serial.printf("[EPD] %s完成，已關閉面板高壓；耗時 %lu ms，BUSY 輪詢 %lu 次。\n",
                  refreshDescription,
                  static_cast<unsigned long>(completedMs - operationStartedMs),
                  static_cast<unsigned long>(busyPolls));
    Serial.println("這只代表流程返回，顯示是否正確仍須目視確認。");
    if (!busyPolls) {
      Serial.println("未觀察到 BUSY 等待：請核對 BUSY 接線、供電及面板型號。");
    }
  }
  if (state.load() == State::Idle && queuedStandbyRequest) {
    queuedStandbyRequest = false;
    standbyRequested = true;
  } else if (state.load() == State::Idle && queuedButtonCount) {
    // Apply all actions accumulated during BUSY before scheduling one redraw.
    // This keeps navigation responsive without triggering an extra slow refresh per tap.
    while (state.load() == State::Idle && queuedButtonCount) {
      dispatchQueuedButtonAction();
    }
  }
  if (state.load() == State::Fault && !faultReported) {
    faultReported = true;
    sequenceActive = false;
    uiDiagnosticActive = false;
    Serial.println("故障：BUSY 等待期間，整次操作已超過 60 秒。");
    Serial.println("已停止電子紙工作、CS 拉高並將 RST 拉低；不會自動重試或重開機。");
    Serial.println("請斷電後核對供電、BUSY、RST、CS、DC 和面板設定，再重新上電。");
  }
  if (!standbyRequested && autoStandbyMinutes && lastUserActivityMs &&
      !sequenceActive && !uiDiagnosticActive && state.load() == State::Idle &&
      !uiDirty && millis() - lastUserActivityMs >=
          static_cast<uint32_t>(autoStandbyMinutes) * 60UL * 1000UL) {
    standbyRequested = true;
    standbyFrameStarted = false;
    standbyFrameComplete = false;
    Serial.printf("[POWER] 閒置 %u 分鐘，自動刷新 POWER OFF 頁並進入待機。\n",
                  autoStandbyMinutes);
  }
  if (standbyRequested && state.load() == State::Idle && !standbyFrameStarted) {
    sequenceActive = false;
    if (startFrame(Frame::Standby, true)) {
      standbyFrameStarted = true;
      Serial.println("[POWER] 待機畫面刷新中。");
    }
  }
  if (standbyRequested && standbyFrameComplete && state.load() == State::Idle) {
    enterStandby();
    return;
  }
  if (!standbyRequested && uiDirty && state.load() == State::Idle &&
      (!lastButtonRefreshMs || millis() - lastButtonRefreshMs >= BUTTON_REFRESH_GAP_MS)) {
    Frame uiFrame = Frame::Menu;
    if (uiPage == UiPage::Detail) {
      switch (menuSelection) {
        case 0: uiFrame = Frame::Checklist; break;
        case 1: uiFrame = Frame::Schedule; break;
        case 2: uiFrame = Frame::Notes; break;
        case 3: uiFrame = Frame::Status; break;
        default: uiFrame = Frame::Settings; break;
      }
    }
    activeUiWindowCount = pendingUiWindowCount;
    for (uint8_t i = 0; i < activeUiWindowCount; ++i) activeUiWindows[i] = pendingUiWindows[i];
    bool pageChangeFullRefresh = !hasLastAppScreenFrame || uiFrame != lastAppScreenFrame;
    bool scheduledFullRefresh = uiRefreshSweepsBeforeFull != 0 &&
        partialUpdatesSinceFullRefresh >= uiRefreshSweepsBeforeFull;
    activeUiFullRefresh = pageChangeFullRefresh || scheduledFullRefresh ||
                          !panelPrepared || !display.epd2.hasFastPartialUpdate;
    if (startFrame(uiFrame, true)) {
      uiDirty = false;
      uiFullRefresh = false;
      pendingUiWindowCount = 0;
      if (activeUiFullRefresh) {
        Serial.printf("[UI] 全畫面重繪排程：%s；本次直接整頁標準刷新（%s）\n",
                      uiPage == UiPage::Menu ? "主選單" : MENU_ITEMS[menuSelection],
                      pageChangeFullRefresh ? "切換頁面" :
                      scheduledFullRefresh ? "已達設定局刷次數" :
                      !panelPrepared ? "面板初次初始化" : "面板不支援局刷或指定全刷");
      } else {
        Serial.printf("[UI] 全畫面重繪排程：%s；整頁整頁單次局刷（%s）\n",
                      uiPage == UiPage::Menu ? "主選單" : MENU_ITEMS[menuSelection],
                      uiRefreshSweepsBeforeFull == 0
                          ? "永不按次數全刷，切換頁面時才全刷"
                          : "依設定次數整頁單次局刷，到期後直接整頁全刷");
      }
    }
  }
  runUiDiagnostic();
  while (Serial.available()) {
    char cmd = Serial.read();
    if (cmd >= 'A' && cmd <= 'Z') cmd += 'a' - 'A';
    if (cmd == '\r' || cmd == '\n' || cmd == ' ' || cmd == '\t') continue;
    if (cmd == 'h' || cmd == '?') { menu(); continue; }
    if (cmd == 'z') {
      if (state.load() != State::Idle || uiDirty || uiDiagnosticActive || sequenceActive || standbyRequested) {
        Serial.println("[POWER-TEST] 請等待畫面完成並回到閒置狀態，再送 z。");
      } else {
        standbyRequested = true;
        standbyFrameStarted = false;
        standbyFrameComplete = false;
        Serial.println("[POWER-TEST] 以相同路徑刷新 POWER OFF 頁並進入深度睡眠；不代表實體按鍵喚醒已驗證。");
      }
      continue;
    }
    if (cmd == 'q') {
      sequenceActive = false;
      if (uiDiagnosticActive) {
        uiDiagnosticActive = false;
        itemChecked[0] = uiDiagnosticOriginalChecked;
        uiPage = UiPage::Menu;
        menuSelection = 0;
        markUiFullRefresh();
        Serial.println("[UI-TEST] 已取消；還原勾選狀態並返回主選單。");
        continue;
      }
      Serial.println("已取消後續刷屏；若有刷新正在進行，會讓它完成。");
      continue;
    }
    if (cmd == 'x') {
      if (state.load() != State::Idle || uiDirty || uiDiagnosticActive ||
          sequenceActive || standbyRequested || uiPage != UiPage::Menu) {
        Serial.println("[UI-TEST] 請先回到主選單並等待刷新完成，再送 x。");
      } else {
        uiPage = UiPage::Menu;
        menuSelection = 0;
        checklistSelection = 0;
        pairingArmed = false;
        uiDiagnosticOriginalChecked = itemChecked[0];
        uiDiagnosticStep = 0;
        uiDiagnosticActive = true;
        markUiFullRefresh();
        Serial.println("[UI-TEST] 開始；依設定次數整頁整頁單次局刷，達次數後直接整頁全刷，不追加第二輪刷新。勾選只在 RAM 暫存，不寫入 NVS；最後還原並返回主選單，實體按鍵暫停讀取。");
      }
      continue;
    }
    if (state.load() == State::Fault) {
      Serial.println("電子紙故障鎖定中；請先斷電檢查。");
      continue;
    }
    if (cmd == 'p') {
      cardDisplayEnabled = false;
      Serial.println("已暫停卡片與 GPS 自動畫面；仍持續接收並輸出 USB 紀錄。");
      continue;
    }
    if (cmd == 'a') {
      cardDisplayEnabled = true;
      if (latestCard.uidSize) cardPending = true;
      else readerStatusPending = true;
      Serial.println("已開啟刷卡自動顯示。");
      continue;
    }
    if (cmd == 'j') {
      gps_support::printDiagnosticReport();
      continue;
    }
    if (cmd == 'u') {
      if (state.load() != State::Idle || !gps_support::ready) {
        Serial.println("[GPS RAW WAIT] 請等畫面更新完成且 UART 啟用後再試。");
      } else {
        gps_support::rawOutput = true;
        Serial.println("[GPS RAW ON] UART 位元組原樣鏡像；送 v 離開。");
        return;
      }
      continue;
    }
    if (cmd == 'b') {
      if (gps_support::ready) gps_support::selectBaud(0);
      else Serial.println("GPS UART 尚未啟用，請先檢查開機紀錄。");
      continue;
    }
    if (cmd == 'o' || cmd == 'i') {
      if (state.load() != State::Idle) {
        Serial.println("請等畫面刷新完成後再切換 RC522 射頻。");
      } else if (!readerOnline) {
        Serial.println("RC522 尚未連線。");
      } else {
        readerRFEnabled = cmd == 'i';
        if (readerRFEnabled) reader.PCD_AntennaOn();
        else reader.PCD_AntennaOff();
        Serial.printf("[RC522 RF] %s；%s\n", readerRFEnabled ? "ON" : "OFF",
                      readerRFEnabled ? "已恢復感應卡片" : "暫停感應卡片，用於排查 GPS 供電／干擾");
      }
      continue;
    }
    if (cmd == 'c') {
      if (latestCard.uidSize) { cardPending = true; cardDisplayEnabled = true; }
      else Serial.println("尚未成功讀取卡片。");
      continue;
    }
    if (cmd == 'r' || cmd == 'f') {
      if (state.load() != State::Idle) {
        Serial.println("請等目前刷新完成後再送出此指令。");
      } else if (cmd == 'r') {
        initReader();
      } else {
        if (latestCard.uidSize) cardPending = true;
        else readerStatusPending = true;
        forceStandardRefreshNext = true;
        Serial.println("互動同頁只更新變動區域，切頁與卡片畫面使用全波形；下一張卡片畫面會完整重繪。");
      }
      continue;
    }
    if (cmd == 's') {
      if (state.load() != State::Idle || sequenceActive) {
        Serial.println("已有測試進行中，未重複排程。");
      } else {
        sequenceActive = true;
        sequenceStep = 0;
        lastCountdownMs = millis() - 10000;
        Serial.println("已排程一輪四個畫面；q 可取消尚未開始的畫面。");
      }
      continue;
    }
    if (sequenceActive) {
      Serial.println("排程進行中；先送 q 取消後續畫面，再進行單張測試。");
      continue;
    }
    switch (cmd) {
      case 't': startFrame(Frame::Pattern); break;
      case 'w': startFrame(Frame::White); break;
      case 'k': startFrame(Frame::Black); break;
      case 'g': startFrame(Frame::Grey); break;
      case 'n': startFrame(Frame::Text); break;
      default: Serial.println("未知指令；送 h 查看選單。"); break;
    }
  }
  if (!standbyRequested) pollReader();
  if (sequenceActive && state.load() == State::Idle) {
    if (sequenceStep == sizeof(sequenceFrames) / sizeof(sequenceFrames[0])) {
      sequenceActive = false;
      Serial.println("四個畫面的刷屏測試已完成，停止自動刷新。");
    } else if (!cooldownMs()) {
      if (startFrame(sequenceFrames[sequenceStep])) ++sequenceStep;
      else sequenceActive = false;
    } else if (millis() - lastCountdownMs >= 10000) {
      lastCountdownMs = millis();
      Serial.printf("距離下一張還有 %lu 秒；q 可取消。\n",
                    static_cast<unsigned long>((cooldownMs() + 999) / 1000));
    }
  }
  if (!standbyRequested && uiPage == UiPage::Detail && menuSelection == 3 &&
      state.load() == State::Idle && !uiDirty && gps_support::dirty &&
      millis() - lastStatusRefreshMs >= 15000UL) {
    markUiWindow(0, 28, display.width(), 240);
  }
  if (!standbyRequested && !sequenceActive && cardDisplayEnabled && state.load() == State::Idle &&
      !cardCooldownMs()) {
    if (cardPending && latestCard.uidSize) {
      if (startFrame(Frame::Card)) cardPending = false;
    } else if (readerStatusPending) {
      if (startFrame(readerOnline ? Frame::ReaderReady : Frame::ReaderError)) {
        readerStatusPending = false;
      }
    } else if ((uiPage != UiPage::Detail || (menuSelection != 3 && menuSelection != 4)) &&
               gps_support::dirty && (gps_support::urgent ||
               millis() - lastGpsScreenMs >= GPS_SCREEN_INTERVAL_MS)) {
      startFrame(Frame::GPS);
    }
  }
  if (millis() - lastHeartbeatMs >= 5000) {
    lastHeartbeatMs = millis();
    Serial.printf("[alive] 秒=%lu，狀態=%d，開機原因=%d，RC522=%s，RF=%s，讀卡=%lu\n",
                  static_cast<unsigned long>(millis() / 1000),
                  static_cast<int>(state.load()), static_cast<int>(esp_reset_reason()),
                  readerOnline ? "ONLINE" : "OFFLINE", readerRFEnabled ? "ON" : "OFF",
                  static_cast<unsigned long>(latestCard.count));
    gps_support::printStatus();
  }
  delay(10);
}

}  // namespace epaper_test



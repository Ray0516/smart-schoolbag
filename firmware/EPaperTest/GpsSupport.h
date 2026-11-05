#pragma once

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include "PanelConfig.h"
#include "src/TinyGPSPlus/TinyGPS++.h"

namespace gps_support {

enum class Status : uint8_t { WaitingData, NoFix, Fix, Stale, UartError };
struct View {
  Status status = Status::WaitingData;
  double latitude = 0;
  double longitude = 0;
  uint32_t satellites = 0;
  bool satellitesKnown = false;
  uint32_t visible = 0;
  uint8_t peakCno = 0;
  bool visibleKnown = false;
  uint32_t baud = 9600;
  uint32_t nmeaCount = 0;
};
constexpr uart_port_t PORT = UART_NUM_1;
constexpr uint32_t BAUD_RATES[] = {9600, 38400, 115200, 4800, 19200, 57600};
TinyGPSPlus parser;
View view;
bool dirty = true;
bool urgent = true;
bool ready = false;
bool rawOutput = false;
bool baudLocked = false;
bool fixFlagSeen = false;
bool fixFlag = false;
uint32_t fixFlagMs = 0;
uint32_t totalRxBytes = 0;
uint32_t totalNmea = 0;
uint32_t validRmcCount = 0;
uint32_t validGgaCount = 0;
uint32_t validGsvCount = 0;
uint32_t validGsaCount = 0;
uint32_t validVtgCount = 0;
uint32_t baudSwitchCount = 0;
uint32_t lastValidMs = 0;
uint32_t slotStartedMs = 0;
uint32_t lastViewUpdateMs = 0;
uint32_t slotNmea = 0;
size_t baudIndex = 0;
char line[128] = {};
size_t lineLength = 0;
char lastNmea[128] = {};
char lastGsv[128] = {};
struct GsvInfo { uint32_t visible = 0; uint8_t peakCno = 0; uint32_t atMs = 0; };
GsvInfo gsv[5];  // GP、GL、GA、GB/BD、GN。

const char* statusText(Status status) {
  switch (status) {
    case Status::WaitingData: return "WAIT DATA";
    case Status::NoFix: return "NO FIX";
    case Status::Fix: return "FIX";
    case Status::Stale: return "STALE";
    case Status::UartError: return "UART ERROR";
  }
  return "UNKNOWN";
}

char firstFieldChar(const char* sentence, unsigned target) {
  unsigned field = 0;
  for (const char* p = sentence; *p; ++p) {
    if (*p == ',' && ++field == target) return p[1];
    if (*p == '*') break;
  }
  return '\0';
}

bool unsignedField(const char* sentence, unsigned target, uint32_t& value) {
  unsigned field = 0;
  for (const char* p = sentence; *p; ++p) {
    if (*p == '*') return false;
    if (*p == ',' && ++field == target) {
      ++p;
      if (*p < '0' || *p > '9') return false;
      value = 0;
      for (unsigned digits = 0; *p >= '0' && *p <= '9' && digits < 5; ++p, ++digits) {
        value = value * 10 + static_cast<uint32_t>(*p - '0');
      }
      return true;
    }
  }
  return false;
}

void refreshView() {
  View next;
  uint32_t now = millis();
  next.baud = BAUD_RATES[baudIndex];
  next.nmeaCount = totalNmea;
  if (!ready) next.status = Status::UartError;
  else if (!lastValidMs) next.status = Status::WaitingData;
  else if (now - lastValidMs > GPS_STREAM_MAX_AGE_MS) next.status = Status::Stale;
  else {
    bool recentFix = fixFlagSeen && fixFlag &&
        now - fixFlagMs <= GPS_FIX_MAX_AGE_MS &&
        parser.location.isValid() && parser.location.age() <= GPS_FIX_MAX_AGE_MS;
    next.status = recentFix ? Status::Fix : Status::NoFix;
    if (recentFix) {
      next.latitude = parser.location.lat();
      next.longitude = parser.location.lng();
    }
    next.satellitesKnown = parser.satellites.isValid() &&
                          parser.satellites.age() <= GPS_STREAM_MAX_AGE_MS;
    if (next.satellitesKnown) next.satellites = parser.satellites.value();
    uint32_t totalVisible = 0;
    for (unsigned i = 0; i < 5; ++i) {
      if (gsv[i].atMs && now - gsv[i].atMs <= GPS_STREAM_MAX_AGE_MS) {
        next.visibleKnown = true;
        if (i < 4) totalVisible += gsv[i].visible;
        next.peakCno = max(next.peakCno, gsv[i].peakCno);
      }
    }
    next.visible = totalVisible;
    if (gsv[4].atMs && now - gsv[4].atMs <= GPS_STREAM_MAX_AGE_MS) {
      next.visible = max(next.visible, gsv[4].visible);
    }
  }
  bool changed = next.status != view.status || next.baud != view.baud ||
      next.satellitesKnown != view.satellitesKnown ||
      next.satellites != view.satellites || next.latitude != view.latitude ||
      next.longitude != view.longitude || next.visibleKnown != view.visibleKnown ||
      next.visible != view.visible;
  if (next.status != view.status) {
    urgent = true;
    if (!rawOutput) Serial.printf("[GPS] 狀態=%s，RX=%lu bytes，有效NMEA=%lu\n", statusText(next.status),
                                 static_cast<unsigned long>(totalRxBytes), static_cast<unsigned long>(totalNmea));
  }
  view = next;
  if (changed) dirty = true;
}

void selectBaud(size_t index) {
  baudIndex = index;
  ++baudSwitchCount;
  uart_set_baudrate(PORT, BAUD_RATES[index]);
  uart_flush_input(PORT);
  parser = TinyGPSPlus();
  baudLocked = false;
  slotNmea = 0;
  lineLength = 0;
  for (auto& entry : gsv) entry = GsvInfo{};
  lastGsv[0] = '\0';
  lastValidMs = 0;
  fixFlagSeen = false;
  fixFlag = false;
  slotStartedMs = millis();
  dirty = true;
  Serial.printf("[GPS] RX44，僅接收，嘗試 %lu baud。\n",
                static_cast<unsigned long>(BAUD_RATES[index]));
  refreshView();
}

void begin() {
  if (uart_is_driver_installed(PORT)) {
    Serial.println("[GPS] UART1 已被占用，未覆寫既有驅動。");
    refreshView();
    return;
  }
  uart_config_t config = {};
  config.baud_rate = BAUD_RATES[0];
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  esp_err_t result = uart_param_config(PORT, &config);
  if (result == ESP_OK) {
    // TX、RTS、CTS 均不配置，只把 UART1 RX 映射至 GPIO44。
    result = uart_set_pin(PORT, UART_PIN_NO_CHANGE, GPS_RX_PIN,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  if (result == ESP_OK) result = uart_driver_install(PORT, 8192, 0, 0, nullptr, 0);
  ready = result == ESP_OK;
  if (!ready) {
    Serial.printf("[GPS] UART 初始化失敗：%s。\n", esp_err_to_name(result));
    refreshView();
    return;
  }
  gpio_pullup_en(static_cast<gpio_num_t>(GPS_RX_PIN));
  selectBaud(0);
}

void end() {
  if (uart_is_driver_installed(PORT)) {
    esp_err_t result = uart_driver_delete(PORT);
    if (result != ESP_OK) {
      Serial.printf("[GPS] UART 停止失敗：%s。\n", esp_err_to_name(result));
    }
  }
  ready = false;
  rawOutput = false;
  baudLocked = false;
  lineLength = 0;
  lastValidMs = 0;
  fixFlagSeen = false;
  fixFlag = false;
  parser = TinyGPSPlus();
  view = View{};
  dirty = true;
  urgent = true;
  Serial.println("[GPS] UART1 已暫停待機。");
}

void acceptSentence() {
  // 只有 TinyGPSPlus 已通過 checksum 的句子才會到這裡。
  if (lineLength < 9 || line[0] != '$' ||
      (line[1] != 'G' && strncmp(line + 1, "BD", 2) != 0)) return;
  ++totalNmea;
  ++slotNmea;
  if (strncmp(line + 3, "RMC", 3) == 0) ++validRmcCount;
  else if (strncmp(line + 3, "GGA", 3) == 0) ++validGgaCount;
  else if (strncmp(line + 3, "GSV", 3) == 0) ++validGsvCount;
  else if (strncmp(line + 3, "GSA", 3) == 0) ++validGsaCount;
  else if (strncmp(line + 3, "VTG", 3) == 0) ++validVtgCount;
  lastValidMs = millis();
  snprintf(lastNmea, sizeof(lastNmea), "%s", line);
  if (strncmp(line + 3, "RMC", 3) == 0) {
    fixFlagSeen = true;
    fixFlag = firstFieldChar(line, 2) == 'A';
    fixFlagMs = millis();
  } else if (strncmp(line + 3, "GGA", 3) == 0) {
    char quality = firstFieldChar(line, 6);
    fixFlagSeen = true;
    fixFlag = quality >= '1' && quality <= '8';
    fixFlagMs = millis();
  } else if (strncmp(line + 3, "GSV", 3) == 0) {
    int group = line[2] == 'P' ? 0 : line[2] == 'L' ? 1 :
                line[2] == 'A' ? 2 : line[2] == 'B' ? 3 :
                line[2] == 'N' ? 4 : strncmp(line + 1, "BD", 2) == 0 ? 3 : -1;
    uint32_t visible = 0, message = 0;
    if (group >= 0 && unsignedField(line, 3, visible)) {
      unsignedField(line, 2, message);
      if (message == 1) gsv[group].peakCno = 0;
      gsv[group].visible = visible;
      gsv[group].atMs = millis();
      for (unsigned field : {7u, 11u, 15u, 19u}) {
        uint32_t cno = 0;
        if (unsignedField(line, field, cno) && cno <= 99) {
          gsv[group].peakCno = max(gsv[group].peakCno, static_cast<uint8_t>(cno));
        }
      }
      snprintf(lastGsv, sizeof(lastGsv), "%s", line);
    }
  } else if (strncmp(line + 3, "TXT", 3) == 0) {
    if (!rawOutput) Serial.printf("[GPS TXT] %s\n", line);
  }
  if (!baudLocked && slotNmea >= 2) {
    baudLocked = true;
    if (!rawOutput) Serial.printf("[GPS] 已鎖定 %lu baud，收到 checksum 正確的 NMEA。\n",
                                 static_cast<unsigned long>(BAUD_RATES[baudIndex]));
  }
  if (!rawOutput && totalNmea <= 3) Serial.printf("[GPS NMEA] %s\n", lastNmea);
}

void poll() {
  if (!ready) return;
  // UART 接收獨立於 SPI，電子紙刷新期間仍持續解析 GPS。
  uint8_t bytes[256];
  unsigned budget = 1024;
  while (budget) {
    unsigned request = budget < sizeof(bytes) ? budget : sizeof(bytes);
    int count = uart_read_bytes(PORT, bytes, request, 0);
    if (count <= 0) break;
    budget -= static_cast<unsigned>(count);
    totalRxBytes += static_cast<uint32_t>(count);
    // UART 原始位元組先鏡像到 USB，再交給解析器；此路徑不修改內容。
    if (rawOutput) Serial.write(bytes, count);
    for (int i = 0; i < count; ++i) {
      char c = static_cast<char>(bytes[i]);
      if (c == '$') { lineLength = 0; line[lineLength++] = c; }
      else if (lineLength && c != '\r' && c != '\n') {
        if (lineLength + 1 < sizeof(line)) line[lineLength++] = c;
        else lineLength = 0;
      }
      line[lineLength] = '\0';
      if (parser.encode(c)) acceptSentence();
      if (c == '\r' || c == '\n') lineLength = 0;
    }
  }
  uint32_t now = millis();
  if (!rawOutput && !baudLocked && now - slotStartedMs >= GPS_BAUD_SCAN_INTERVAL_MS) {
    selectBaud((baudIndex + 1) % (sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0])));
  } else if (!rawOutput && baudLocked && now - lastValidMs >= 30000UL) {
    Serial.println("[GPS] 有效資料中斷 30 秒，重新尋找鮑率。");
    selectBaud(0);
  }
  if (now - lastViewUpdateMs >= 250) {
    lastViewUpdateMs = now;
    refreshView();
  }
}

void printStatus() {
  Serial.printf("[GPS] %s，baud=%lu，RX=%lu，有效NMEA=%lu，CRC失敗=%lu，衛星=",
                statusText(view.status), static_cast<unsigned long>(view.baud),
                static_cast<unsigned long>(totalRxBytes), static_cast<unsigned long>(totalNmea),
                static_cast<unsigned long>(parser.failedChecksum()));
  if (view.satellitesKnown) Serial.print(view.satellites);
  else Serial.print("--");
  Serial.print("，GSV可見=");
  if (view.visibleKnown) Serial.printf("%lu，最高C/N0=%u dBHz", static_cast<unsigned long>(view.visible), view.peakCno);
  else Serial.print("--");
  if (view.status == Status::Fix) Serial.printf("，lat=%.6f，lon=%.6f", view.latitude, view.longitude);
  Serial.println();
  if (lastGsv[0]) Serial.printf("[GPS GSV] %s\n", lastGsv);
}

void printDiagnosticReport() {
  uint32_t now = millis();
  size_t buffered = 0;
  if (ready) uart_get_buffered_data_len(PORT, &buffered);
  Serial.println("\n========== GPS HARDWARE DIAGNOSTIC ==========");
  Serial.printf("UART1: %s | RX GPIO%u | 8N1 | RX buffer=%u bytes\n",
                ready ? "READY" : "ERROR/PAUSED", GPS_RX_PIN,
                static_cast<unsigned>(buffered));
  Serial.printf("Baud: %lu | lock=%s | baud switches=%lu | candidates=9600,38400,115200,4800,19200,57600\n",
                static_cast<unsigned long>(view.baud), baudLocked ? "YES" : "SCANNING",
                static_cast<unsigned long>(baudSwitchCount));
  Serial.printf("RX bytes=%lu | valid NMEA=%lu | checksum errors=%u | RMC=%lu GGA=%lu GSA=%lu GSV=%lu VTG=%lu\n",
                static_cast<unsigned long>(totalRxBytes), static_cast<unsigned long>(totalNmea),
                static_cast<unsigned>(parser.failedChecksum()),
                static_cast<unsigned long>(validRmcCount), static_cast<unsigned long>(validGgaCount),
                static_cast<unsigned long>(validGsaCount), static_cast<unsigned long>(validGsvCount),
                static_cast<unsigned long>(validVtgCount));
  if (lastValidMs) {
    Serial.printf("Last valid NMEA age=%lu ms\n", static_cast<unsigned long>(now - lastValidMs));
  } else {
    Serial.println("Last valid NMEA: none");
  }
  Serial.printf("Fix status=%s | satellites used=", statusText(view.status));
  if (view.satellitesKnown) Serial.print(view.satellites);
  else Serial.print("--");
  Serial.print(" | satellites visible=");
  if (view.visibleKnown) Serial.print(view.visible);
  else Serial.print("--");
  Serial.printf(" | peak C/N0=%u dB-Hz\n", view.peakCno);
  if (view.status == Status::Fix) {
    Serial.printf("Position: %.6f, %.6f\n", view.latitude, view.longitude);
  } else {
    Serial.println("Position: no current fix");
  }
  if (lastNmea[0]) Serial.printf("Last valid NMEA: %s\n", lastNmea);
  else Serial.println("Last valid NMEA: none");
  if (lastGsv[0]) Serial.printf("Last GSV: %s\n", lastGsv);
  else Serial.println("Last GSV: none");
  Serial.println("RX-only wiring cannot change receiver protocol; raw mirror: send u, leave with v.");
  Serial.println("=============================================\n");
}

}  // namespace gps_support

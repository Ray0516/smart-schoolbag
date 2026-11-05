// 開啟此檔後，先在 PanelConfig.h 選擇面板設定，再編譯或上傳；手機控制走 Wi-Fi。
// 上傳後會自動顯示讀卡狀態；將卡片靠近 RC522，即顯示 UID 與卡片類型。
// USB 序列埠為 115200 baud，送出 h 查看指令。
#include "EPaperTestApp.h"

void setup() { epaper_test::begin(); }
void loop() { epaper_test::poll(); }

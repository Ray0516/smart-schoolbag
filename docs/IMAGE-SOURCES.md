# 實照拼貼與接線來源

這張橫向圖使用真實產品照片，經裁切、旋轉與透視校正，再用程式逐條繪製接線、文字和標示。未使用生成式影像工具；保留真實 PCB 紋理與絲印。SVG 內嵌所有照片，可單獨開啟；PNG 為 3000×1860，PDF 是相同構圖的列印版。

照片為同型／同系參考，不是使用者實物照片，也不表示所有尺寸、排針次序與面板版號均相同。依使用者要求，導線接到模組標示區，未宣稱線端是照片上實際焊點。接線標籤已逐項比對目前韌體 `PanelConfig.h` 與 `BUTTON_PINS`；機讀結果見 `接線核對.json`。

## 實照

- S3 正／背面：[Nologo 官方產品頁](https://www.nologo.tech/product/esp32/esp32s3/esp32s3supermini/esp32S3SuperMini.html)，[原始產品照](https://www.nologo.tech/assets/img/esp32/esp32s3supermini/esp32s3.jpg)。拆成正、背兩張，裁切並透視校正。GPIO14 的位置依[官方正背面腳位圖](https://www.nologo.tech/assets/img/esp32/esp32s3supermini/esp32s3foot1.jpg)標為底面焊盤；原接線文件曾誤稱全部使用外圍排針，本次一併更正。
- 電子紙正面：[Waveshare 模組手冊](https://www.waveshare.com/wiki/2.9inch_e-Paper_Module_Manual)，[原始產品照](https://www.waveshare.com/w/upload/b/bc/2.9inch-e-paper-module-4.jpg)。裁切並拉正。照片中的顯示圖案是原始產品照，不是本專案韌體畫面。
- 電子紙背板：[產品說明頁](https://manuals.plus/asin/B071LGVVL1)，[原始介面產品圖](https://m.media-amazon.com/images/I/61xmPkPX%2BjL._AC_SL1200_.jpg)。只裁出上半部正視背板，保留真實 BUSY/RST/DC/CS/CLK/DIN/GND/VCC 絲印。背板 Rev2.1 不能單獨證明實際顯示面板的驅動型號。
- GPS：[電子工作ステーション GY-GPSV3-NEO6M](https://electronicwork.shop/items/68555fad2351e4b122b8b99f)，[原始照片](https://imagedelivery.net/QondspN4HIUvB_R16-ddAQ/61d869cdfd2e73482d08d4c0/685c7238049b5212d78a.png/fit%3Dcover%2Cw%3D920%2Ch%3D920)。拆成主板與陶瓷天線、透視校正；照片是四腳同系載板，本案只用三線 VCC/GND/TXD。RF 同軸線以手繪線表示連接。
- RC522：[Goga504，Wikimedia Commons](https://commons.wikimedia.org/wiki/File:RFID_module_RC522.jpg)，[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)。照片裁出藍色主板、旋轉與透視校正；該照片改作依 CC BY-SA 4.0 使用，未暗示原作者背書本接線。
- 按鈕：[NZ Electronics 6×6 mm 兩腳常開輕觸開關](https://www.nznelectronics.co.nz/products/tactile-push-button-switch-6-6mm-2-pin-through-hole-momentary-pcb)，[正視照片](https://www.nznelectronics.co.nz/cdn/shop/files/tactile-push-button-6x6mm-2-pin-top-view.png?width=800)。裁切後重複五次。使用者若採四腳按鈕，須使用按下才導通的兩組端子。

產品照片著作權歸各原作者／供應商；非 Commons 素材未另外取得商用授權。接線與文字為本專案繪製。

## 本圖接法

| 模組 | 接到 S3 |
|---|---|
| 電子紙 VCC / GND | 3V3 / GND |
| DIN / CLK / CS / DC / RST / BUSY | 11 / 12 / 10 / 43（TX）/ 6 / 7 |
| RC522 VCC / GND | 3V3 / GND |
| MOSI / SCK / MISO / SDA(SS) / RST | 11 / 12 / 13 / 9 / 8 |
| RC522 IRQ | 不接 |
| GPS TXD | 44（RX） |
| GPS VCC / GND | 本案最後確認為載板 5V / GND；仍須符合實物額定 |
| 上 / 下 / 左 / 右 / OK | 14（底面）/ 5 / 2 / 1 / 4；另一端各接 GND |

全部訊號維持 3.3V 邏輯。GPIO3 留給特定板型的 USB VBUS 偵測，GPIO19/20 留給原生 USB。SPI 為共用 MOSI/SCK、獨立片選及 RST。GPS 裸 NEO-6M 不能直接接 5V；本圖標示的是已確認接法的載板 VCC。


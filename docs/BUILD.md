# 建置、執行與版本

建置不會自動燒錄或安裝。來源版本 v1.5.0／App 1.5.0+15；要做正式發行前先完成 TESTING.md。

## 韌體

先複製 firmware/EPaperTest/LocalWifiConfig.example.h 為 LocalWifiConfig.h，設定自己的密碼（詳細步驟見本頁末尾），再編譯。

已知工具鏈：Arduino ESP32 core **3.3.7**，GxEPD2 **1.6.9**，Adafruit GFX Library **1.12.6**，Adafruit BusIO **1.17.4**。MFRC522 **1.4.12** 與 TinyGPSPlus **1.1.0** 已隨 src 收錄，不要再用別版覆蓋。Arduino CLI 需可從 PATH 找到，或腳本 -ArduinoCli 指定路徑。

```powershell
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.7
arduino-cli lib install "GxEPD2@1.6.9" "Adafruit GFX Library@1.12.6" "Adafruit BusIO@1.17.4"
powershell -ExecutionPolicy Bypass -File scripts/build-firmware.ps1
```

FQBN 固定為：

```text
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=4M,PSRAM=disabled,PartitionScheme=huge_app
```

Arduino IDE 選 ESP32S3 Dev Module、USB Mode Hardware CDC and JTAG、USB CDC On Boot Enabled、Flash 4MB、PSRAM Disabled、Huge APP。開啟 firmware/EPaperTest/EPaperTest.ino。先核對 PanelConfig.h，不要自動試不同面板。預設 profile 1 不需 GxEPD2_4G；profile 6 需另裝且只限已確認面板。

產物在 artifacts/firmware，來源雜湊由 SOURCE-MANIFEST.json 記錄。若 CLI Windows sandbox 啟動 DLL 失敗，應修復執行環境，不改硬體邏輯掩蓋工具鏈問題。

## Flutter Android

原工作環境 Flutter 3.41 系列，pubspec 要求 Dart ^3.11.1；以相容的 Flutter stable 版本及 Dart 3.11 以上建置。保留 pubspec.lock。Android 使用 JDK 17、Gradle wrapper、AGP 8.11.1、Kotlin 2.2.20，SDK/NDK 依 Flutter 配置安裝。

```powershell
flutter doctor -v
cd mobile/SmartSchoolbagApp
flutter pub get
flutter analyze
flutter test
flutter build apk --release
```

APK 在 mobile/SmartSchoolbagApp/build/app/outputs/flutter-apk/app-release.apk。Android Studio 可自動設定 local.properties；該檔含機器路徑，不加入版本控制。原型 release 目前使用 debug signing，沒有提供私人簽章金鑰；正式上架需擁有者配置簽章，換金鑰可能不能覆蓋原手機版本，不要為了安裝直接清資料。

也可在根目錄執行 scripts/build-mobile.ps1，會依序還原套件、分析、測試、建置。iOS 殼層收錄但未驗證；需要 macOS/Xcode、簽章及本地網路／藍牙權限回歸，不能由 Android 成功推論可用。

## 實機（需使用者當輪要求）

安裝手機可用 adb install -r <APK>，保留原資料；需相同簽章及已授權 ADB。燒錄 S3 前確認實際序列埠與版本，只更新 app 時使用正確位址 0x10000，不要 erase_flash，以免清 NVS。首次新板還需要正確 bootloader／partition／boot_app0，不能只靠 app.bin 開機；使用 Arduino CLI／IDE 正常 upload 會依工具鏈處理。COM7 只是舊機器紀錄，不是通用埠名。

USB 序列為 115200。需要進下載模式時按住 BOOT、按放 RESET、再放 BOOT；平常只按 RESET。深睡會讓 USB CDC 斷線，不代表程式一定損毀。

## 公開交接版的 Wi-Fi 設定（編譯前必做）

現有裝置密碼不在公開儲存庫。請先執行：

```powershell
Copy-Item firmware/EPaperTest/LocalWifiConfig.example.h firmware/EPaperTest/LocalWifiConfig.h
```

編輯 LocalWifiConfig.h，將 SCHOOLBAG_WIFI_PASSWORD 設為自己的 8～63 bytes 密碼，再執行建置。此檔被 .gitignore 排除，請私下提供給裝置使用者。手機只提示向擁有者取得密碼，不硬編碼密碼。燒錄新版本後，手機須在系統 Wi-Fi 設定使用新密碼重新加入。單純上傳 GitHub 不會改變現有實體裝置的密碼。

本次交接驗證的 Flutter 精確版本為 3.41.4、Dart 3.11.1。不需要重新產生既有中文字庫；產生工具依賴 Windows 字型和 System.Drawing，公開再散布字形的權利需另外確認。


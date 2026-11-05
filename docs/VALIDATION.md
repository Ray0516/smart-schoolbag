# 本次公開交接驗證

日期：2026-11-05，臺灣。來源基線：v1.5.0；交接整理未燒錄 ESP32，也未安裝手機。

## 手機

- 工具鏈：Flutter 3.41.4、Dart 3.11.1。
- 在重新整理的 mobile/SmartSchoolbagApp 執行 flutter pub get，成功還原鎖定依賴。
- flutter analyze：No issues found。
- flutter test：4 項資料邏輯測試全部通過。
- flutter build apk --release：成功，53.1 MB。
- APK SHA-256：`3FAC4F4149001CD59752CA1C3DE29DCD8CED77C552F41EF99BD4945C0CA25418`。
- APK 屬原型 debug signing 的 release build；不等同正式簽章或新版實機驗收。

## 來源與圖片

- 以不帶 GitHub 認證的 git clone 成功下載公開儲存庫。
- 在下載副本執行 scripts/verify-manifest.ps1，86 個受追蹤基線來源雜湊全部相符。
- Android Gradle wrapper 的腳本與 JAR 有收錄；本機 local.properties、建置快取與私人 Wi-Fi 設定均未追蹤。
- 中文 Markdown 的本機連結檢查：0 個失效連結。
- wiring.png SHA-256：`3A176D6B7F6C666FE4B92DFD8EC05622665DA84EB3CCB2350055C4D0C5CD9682`。
- manufacturing.png SHA-256：`B94200EC9717E3272E360787446876949880F65CD33FAFFBBE3071E00396EC4D`。
- 兩張 PNG 與使用者要求刪除底部說明後的原檔完全相同，沒有再生成圖片。
- 製作 JPG 移除 EXIF/XMP/IPTC/註解等中繼資料，解碼後像素逐一確認不變。
- 受追蹤內容掃描未發現原型 Wi-Fi 密碼、GitHub token、私人金鑰或本機使用者絕對路徑。

## 韌體

固定 ESP32 core 3.3.7、GxEPD2 1.6.9 與 BUILD.md 的 FQBN，使用本機未上傳的 Wi-Fi 設定重新建置，成功（結束碼 0）。

- 程式：1,687,121 / 3,145,728 bytes，53%。
- 全域變數：66,732 / 327,680 bytes，20%；其餘 260,948 bytes。
- 腳本：scripts/build-firmware.ps1。僅編譯，沒有 upload／flash。
- 韌體產物不加入公開來源庫；內含本機設定的密碼，接手者請依 BUILD.md 設定自己的密碼重新編譯。

## 沒有代替實機驗收

本紀錄不代表 GPS 已取得定位、電子紙無殘影、五鍵與長按喚醒已實測、外設已實際斷電，或新版手機與 S3 同步已完成回歸。這些仍依 TESTING.md 逐項驗收。


package tw.smartschoolbag.smart_schoolbag_app

import android.os.Build
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "smart_schoolbag/device")
            .setMethodCallHandler { call, result ->
                if (call.method == "androidSdkInt") {
                    result.success(Build.VERSION.SDK_INT)
                } else if (call.method == "openWifiSettings") {
                    startActivity(Intent(Settings.ACTION_WIFI_SETTINGS))
                    result.success(true)
                } else if (call.method == "openGpsMap") {
                    val latitude = call.argument<Double>("latitude")
                    val longitude = call.argument<Double>("longitude")
                    if (latitude == null || longitude == null ||
                        latitude !in -90.0..90.0 || longitude !in -180.0..180.0) {
                        result.error("INVALID_COORDINATES", "GPS 座標無效", null)
                    } else {
                        val uri = Uri.parse("geo:$latitude,$longitude?q=$latitude,$longitude")
                        val mapIntent = Intent(Intent.ACTION_VIEW, uri)
                        if (mapIntent.resolveActivity(packageManager) != null) {
                            startActivity(mapIntent)
                            result.success(true)
                        } else {
                            result.error("NO_MAP_APP", "手機沒有可開啟位置的地圖 App", null)
                        }
                    }
                } else {
                    result.notImplemented()
                }
            }
    }
}

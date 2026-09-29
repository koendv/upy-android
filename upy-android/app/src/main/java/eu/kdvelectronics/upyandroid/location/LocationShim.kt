package eu.kdvelectronics.upyandroid.location

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper
import androidx.core.content.ContextCompat
import eu.kdvelectronics.upyandroid.EngineService

// android.location: a thin layer over Android's LocationManager, for
// location_jni_bridge.cpp. Lives in :engine. Static (@JvmStatic) so the
// native bridge reaches it with FindClass + GetStaticMethodID, like
// EngineService.requestShare(). No Google Play Services (fused location).
object LocationShim {
    // start() results, mirrored in location_jni_bridge.h.
    const val OK = 0
    const val NO_PERMISSION = 1
    const val NO_PROVIDER = 2

    private lateinit var appContext: Context

    @Volatile
    private var latest: Location? = null

    // Snapshot for readProvider(), set by readFix(): the two calls must
    // describe the same fix.
    @Volatile
    private var lastRead: Location? = null

    private val listener = LocationListener { location -> latest = location }

    // Called from EngineService.onCreate().
    fun init(context: Context) {
        appContext = context.applicationContext
    }

    private fun manager() = appContext.getSystemService(Context.LOCATION_SERVICE) as LocationManager

    private fun hasPermission() =
        ContextCompat.checkSelfPermission(appContext, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED ||
            ContextCompat.checkSelfPermission(appContext, Manifest.permission.ACCESS_COARSE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    // Without permission, asks the UI process to show the permission
    // prompt and returns NO_PERMISSION; the script raises and can be run
    // again once access is granted.
    @SuppressLint("MissingPermission")
    @JvmStatic
    fun start(intervalMs: Long, minDistanceM: Float): Int {
        if (!hasPermission()) {
            EngineService.requestPermissions(
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION),
            )
            return NO_PERMISSION
        }
        val lm = manager()
        val providers = listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)
            .filter { lm.allProviders.contains(it) && lm.isProviderEnabled(it) }
        if (providers.isEmpty()) {
            return NO_PROVIDER
        }
        lm.removeUpdates(listener)
        for (provider in providers) {
            lm.requestLocationUpdates(provider, intervalMs, minDistanceM, listener, Looper.getMainLooper())
        }
        return OK
    }

    @JvmStatic
    fun stop() {
        if (::appContext.isInitialized) {
            manager().removeUpdates(listener)
        }
        latest = null
    }

    // [latitude, longitude, altitude_m, accuracy_m, speed_mps, bearing_deg,
    // time_ms], NaN where Android has no value; null without a fix.
    // lastKnown: Android's last known fix instead of this session's latest.
    @SuppressLint("MissingPermission")
    @JvmStatic
    fun readFix(lastKnown: Boolean): DoubleArray? {
        val location = if (lastKnown) {
            if (!hasPermission()) return null
            val lm = manager()
            lm.allProviders.mapNotNull { lm.getLastKnownLocation(it) }.maxByOrNull { it.time }
        } else {
            latest
        }
        lastRead = location
        location ?: return null
        return doubleArrayOf(
            location.latitude,
            location.longitude,
            if (location.hasAltitude()) location.altitude else Double.NaN,
            if (location.hasAccuracy()) location.accuracy.toDouble() else Double.NaN,
            if (location.hasSpeed()) location.speed.toDouble() else Double.NaN,
            if (location.hasBearing()) location.bearing.toDouble() else Double.NaN,
            location.time.toDouble(),
        )
    }

    @JvmStatic
    fun readProvider(): String? = lastRead?.provider
}

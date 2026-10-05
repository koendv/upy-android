package eu.kdvelectronics.upyandroid.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CaptureRequest
import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.util.Range
import android.util.Size
import android.view.Display
import android.view.OrientationEventListener
import android.view.Surface
import androidx.camera.camera2.interop.Camera2CameraInfo
import androidx.camera.camera2.interop.Camera2Interop
import androidx.camera.core.AspectRatio
import androidx.camera.core.Camera
import androidx.camera.core.CameraInfo
import androidx.camera.core.CameraSelector
import androidx.camera.core.CameraState
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.FocusMeteringAction
import androidx.camera.core.FocusMeteringResult
import androidx.camera.core.ImageProxy
import androidx.camera.core.LowLightBoostState
import androidx.camera.core.SurfaceOrientedMeteringPointFactory
import androidx.camera.core.TorchState
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.Observer
import eu.kdvelectronics.upyandroid.EngineService
import java.io.IOException
import java.nio.ByteBuffer
import com.google.common.util.concurrent.ListenableFuture
import java.util.concurrent.CancellationException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutionException
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

// camera module: a thin layer over CameraX ImageAnalysis, for
// camera_jni_bridge.cpp. Lives in :engine. The only file that imports
// androidx.camera.*. Called from the MicroPython worker thread; CameraX
// binding runs on the main looper, frames arrive on an analyzer thread.
// One camera open at a time.
object CameraShim {
    // open() results, mirrored in camera_jni_bridge.h.
    const val OK = 0
    const val NO_PERMISSION = 1
    const val NO_CAMERA = 2
    const val NO_FRAME_RATE = 3

    // Control and info errors, mirrored in camera_jni_bridge.h.
    const val ERR_VALUE = 1
    const val ERR_UNSUPPORTED = 2
    const val ERR_IO = 3
    const val ERR_LOST = 4

    private const val SLOTS = 3
    private const val ORIENTATION_WAIT_MS = 500L
    private const val MAIN_WAIT_MS = 5000L

    private lateinit var appContext: Context
    private val mainHandler = Handler(Looper.getMainLooper())

    private class Entry(val info: CameraInfo, val id: String, val facing: String, val sizes: List<Size>)

    private var entries: List<Entry> = emptyList()

    // A frame slot. Written by the analyzer only while it is neither
    // ready nor being read.
    private class Slot(val buf: ByteBuffer) {
        var seq = 0L
        var timestamp = 0L
        var width = 0
        var height = 0
        var rotation = 0
    }

    // Guards everything below it.
    private val lock = ReentrantLock()
    private val changed = lock.newCondition()
    private var slots: Array<Slot> = emptyArray()
    private var ready = -1
    private var reading = -1
    private var seq = 0L
    private var woken = false
    // Sticky: set when CameraX loses the camera or delivers a wrong
    // frame; snapshot() raises it until the camera is closed.
    private var error: String? = null
    private var opening = false
    private var wasOpen = false
    private var closing = false
    // Bumped per open, so a late frame from a closed camera is dropped.
    private var session = 0

    private var provider: ProcessCameraProvider? = null
    // Written on the worker thread and, for a lost camera, the main looper.
    @Volatile private var owner: Owner? = null
    @Volatile private var analysis: ImageAnalysis? = null
    @Volatile private var camera: Camera? = null
    private var executor: ExecutorService? = null
    private var stateObserver: Observer<CameraState>? = null
    private var srcSize = Size(0, 0)
    private var outSize = Size(0, 0)
    private var rotationDegrees = 0
    private var rgb = false

    // CameraX needs a lifecycle; this one is RESUMED while the camera is
    // open and DESTROYED on close. A new one per open.
    private class Owner : LifecycleOwner {
        val registry = LifecycleRegistry(this)
        override val lifecycle: Lifecycle get() = registry
    }

    // Called from EngineService.onCreate().
    fun init(context: Context) {
        appContext = context.applicationContext
    }

    private fun <T> onMain(block: () -> T): T {
        if (Looper.myLooper() == Looper.getMainLooper()) return block()
        val latch = CountDownLatch(1)
        var result: Result<T>? = null
        mainHandler.post {
            result = runCatching(block)
            latch.countDown()
        }
        if (!latch.await(MAIN_WAIT_MS, TimeUnit.MILLISECONDS)) {
            throw IOException("camera: main thread did not respond")
        }
        return result!!.getOrThrow()
    }

    private fun provider(): ProcessCameraProvider =
        provider ?: ProcessCameraProvider.getInstance(appContext).get(MAIN_WAIT_MS, TimeUnit.MILLISECONDS)
            .also { provider = it }

    private fun facingName(facing: Int) = when (facing) {
        CameraSelector.LENS_FACING_BACK -> "back"
        CameraSelector.LENS_FACING_FRONT -> "front"
        CameraSelector.LENS_FACING_EXTERNAL -> "external"
        else -> "unknown"
    }

    private fun loadEntries(): List<Entry> {
        entries = provider().availableCameraInfos.map { info ->
            val c2 = Camera2CameraInfo.from(info)
            val map = c2.getCameraCharacteristic(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            val sizes = map?.getOutputSizes(ImageFormat.YUV_420_888)?.toList().orEmpty()
                .distinct()
                .sortedByDescending { it.width.toLong() * it.height }
            Entry(info, c2.cameraId, facingName(info.lensFacing), sizes)
        }
        return entries
    }

    // camera.list(): count() refreshes, the others index into that result.
    @JvmStatic
    fun count(): Int = loadEntries().size

    @JvmStatic
    fun id(index: Int): String = entries[index].id

    @JvmStatic
    fun facing(index: Int): String = entries[index].facing

    // [w0, h0, w1, h1, ...], sensor orientation, largest first.
    @JvmStatic
    fun sizes(index: Int): IntArray =
        entries[index].sizes.flatMap { listOf(it.width, it.height) }.toIntArray()

    // Nearest pixel count; a tie goes to the closer aspect ratio. The
    // request's orientation does not matter.
    private fun chooseSize(sizes: List<Size>, width: Int, height: Int): Size {
        val want = width.toLong() * height
        val wantRatio = max(width, height).toDouble() / min(width, height)
        return sizes.minWith(
            compareBy<Size> { abs(it.width.toLong() * it.height - want) }
                .thenBy { abs(max(it.width, it.height).toDouble() / min(it.width, it.height) - wantRatio) },
        )
    }

    // Surface.ROTATION_* for how the phone is held right now, from one
    // orientation sensor reading. Flat on a table there is no reading;
    // then the screen's current rotation.
    private fun heldRotation(): Int {
        val latch = CountDownLatch(1)
        var degrees = OrientationEventListener.ORIENTATION_UNKNOWN
        val listener = object : OrientationEventListener(appContext) {
            override fun onOrientationChanged(orientation: Int) {
                if (orientation != ORIENTATION_UNKNOWN && latch.count > 0) {
                    degrees = orientation
                    latch.countDown()
                }
            }
        }
        if (listener.canDetectOrientation()) {
            listener.enable()
            latch.await(ORIENTATION_WAIT_MS, TimeUnit.MILLISECONDS)
            listener.disable()
        }
        if (degrees == OrientationEventListener.ORIENTATION_UNKNOWN) {
            val dm = appContext.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
            return dm.getDisplay(Display.DEFAULT_DISPLAY)?.rotation ?: Surface.ROTATION_0
        }
        return when (degrees) {
            in 45 until 135 -> Surface.ROTATION_270
            in 135 until 225 -> Surface.ROTATION_180
            in 225 until 315 -> Surface.ROTATION_90
            else -> Surface.ROTATION_0
        }
    }

    // Copies the frame, row by row, into a free slot and makes it the
    // ready one: the Y plane for grayscale, CameraX's RGBA plane for
    // RGB565. The ImageProxy is closed at once.
    private fun store(image: ImageProxy, from: Int) {
        image.use {
            val slot: Slot
            lock.withLock {
                if (closing || from != session || slots.isEmpty()) return
                if (image.width != srcSize.width || image.height != srcSize.height) {
                    fail("camera: got ${image.width}x${image.height} frames, not ${srcSize.width}x${srcSize.height}")
                    return
                }
                slot = slots[(0 until SLOTS).first { it != ready && it != reading }]
            }
            val dst = slot.buf.duplicate()
            dst.clear()
            copyRows(image.planes[0], image.width * (if (rgb) 4 else 1), image.height, dst)
            lock.withLock {
                if (from != session) return
                slot.seq = ++seq
                slot.timestamp = image.imageInfo.timestamp
                slot.width = image.width
                slot.height = image.height
                slot.rotation = image.imageInfo.rotationDegrees
                ready = slots.indexOf(slot)
                changed.signalAll()
            }
        }
    }

    private fun copyRows(plane: ImageProxy.PlaneProxy, rowBytes: Int, rows: Int, dst: ByteBuffer) {
        val src = plane.buffer.duplicate()
        val rowStride = plane.rowStride
        for (row in 0 until rows) {
            src.limit(row * rowStride + rowBytes)
            src.position(row * rowStride)
            dst.put(src)
        }
    }

    // Caller holds lock. A lost camera is released at once, screen lock
    // included; snapshot() keeps raising until close().
    private fun fail(message: String) {
        if (error == null) {
            error = message
            val lost = session
            mainHandler.post { if (lost == session) dropCamera() }
        }
        changed.signalAll()
    }

    private fun dropCamera() {
        val useCase = analysis ?: return
        unbind(useCase, owner)
        analysis = null
        owner = null
        camera = null
        EngineService.setCameraRotation(-1)
    }

    // The state LiveData belongs to the camera, not to this open, and
    // first replays the previous session's last state: skip states
    // until this session's own OPENING.
    private fun onState(state: CameraState) {
        lock.withLock {
            if (closing) return
            if (state.type == CameraState.Type.OPENING) opening = true
            if (!opening) return
            val err = state.error
            // While opening, CameraX retries recoverable errors itself;
            // the first-frame timeout limits the wait.
            when {
                err != null && (wasOpen || err.type == CameraState.ErrorType.CRITICAL) -> fail(stateErrorText(err.code))
                err != null -> {}
                state.type == CameraState.Type.OPEN -> wasOpen = true
                wasOpen -> fail("camera: closed by Android (screen off or app not in foreground?)")
            }
        }
    }

    private fun stateErrorText(code: Int) = when (code) {
        CameraState.ERROR_CAMERA_IN_USE -> "camera: in use by another app"
        CameraState.ERROR_MAX_CAMERAS_IN_USE -> "camera: too many cameras in use"
        CameraState.ERROR_DO_NOT_DISTURB_MODE_ENABLED -> "camera: blocked by Do Not Disturb"
        CameraState.ERROR_STREAM_CONFIG -> "camera: stream configuration failed"
        else -> "camera: lost, error $code (screen off or app not in foreground?)"
    }

    // id null: the first back camera, else the first camera.
    // fpsMin 0: automatic frame rate, else one of
    // getSupportedFrameRateRanges(). rgbFrames: RGBA frames from CameraX
    // (it converts from YUV, whatever the camera's layout), else the Y
    // plane only.
    // Without permission, asks the UI process to show the prompt.
    @JvmStatic
    fun open(id: String?, width: Int, height: Int, rgbFrames: Boolean, fpsMin: Int, fpsMax: Int): Int {
        if (ContextCompat.checkSelfPermission(appContext, Manifest.permission.CAMERA) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            EngineService.requestPermissions(arrayOf(Manifest.permission.CAMERA))
            return NO_PERMISSION
        }
        close()
        val all = loadEntries()
        val entry = if (id == null) {
            all.firstOrNull { it.facing == "back" } ?: all.firstOrNull()
        } else {
            all.firstOrNull { it.id == id }
        } ?: return NO_CAMERA
        if (entry.sizes.isEmpty()) throw IOException("camera: ${entry.id} lists no sizes")
        val fps = if (fpsMin > 0) Range(fpsMin, fpsMax) else null
        if (fps != null && fps !in entry.info.supportedFrameRateRanges) return NO_FRAME_RATE

        val size = chooseSize(entry.sizes, width, height)
        val rotation = heldRotation()
        val degrees = entry.info.getSensorRotationDegrees(rotation)
        val ratio = if (abs(size.width.toDouble() / size.height - 16.0 / 9) <
            abs(size.width.toDouble() / size.height - 4.0 / 3)
        ) AspectRatio.RATIO_16_9 else AspectRatio.RATIO_4_3

        lock.withLock {
            slots = Array(SLOTS) { Slot(ByteBuffer.allocateDirect(size.width * size.height * (if (rgbFrames) 4 else 1))) }
            ready = -1
            reading = -1
            seq = 0L
            woken = false
            error = null
            opening = false
            wasOpen = false
            closing = false
            session++
        }
        val thisSession = session
        srcSize = size
        rotationDegrees = degrees
        rgb = rgbFrames
        outSize = if (degrees % 180 == 0) size else Size(size.height, size.width)

        val selector = ResolutionSelector.Builder()
            .setAspectRatioStrategy(AspectRatioStrategy(ratio, AspectRatioStrategy.FALLBACK_RULE_AUTO))
            .setResolutionStrategy(ResolutionStrategy(size, ResolutionStrategy.FALLBACK_RULE_NONE))
            .setResolutionFilter { sizes, _ -> sizes.filter { it == size } }
            .build()
        val builder = ImageAnalysis.Builder()
        if (fps != null) {
            Camera2Interop.Extender(builder).setCaptureRequestOption(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, fps)
        }
        val useCase = builder
            .setResolutionSelector(selector)
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .setOutputImageFormat(
                if (rgbFrames) ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888 else ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888,
            )
            .setTargetRotation(rotation)
            .build()
        val exec = Executors.newSingleThreadExecutor()
        useCase.setAnalyzer(exec) { store(it, thisSession) }
        val cameraSelector = CameraSelector.Builder()
            .addCameraFilter { infos -> infos.filter { Camera2CameraInfo.from(it).cameraId == entry.id } }
            .build()
        val lifecycleOwner = Owner()
        val observer = Observer<CameraState> { onState(it) }
        executor = exec
        analysis = useCase
        owner = lifecycleOwner
        stateObserver = observer
        try {
            camera = onMain {
                lifecycleOwner.registry.currentState = Lifecycle.State.RESUMED
                val cam = provider().bindToLifecycle(lifecycleOwner, cameraSelector, useCase)
                cam.cameraInfo.cameraState.observe(lifecycleOwner, observer)
                cam
            }
        } catch (e: Exception) {
            close()
            throw IOException("camera: cannot open ${entry.id} at ${size.width}x${size.height}: ${e.message}", e)
        }
        EngineService.setCameraRotation(rotation)
        return OK
    }

    // [out_w, out_h, src_w, src_h, rotation_degrees]. out_* is the
    // upright size snapshot() returns.
    @JvmStatic
    fun openInfo(): IntArray =
        intArrayOf(outSize.width, outSize.height, srcSize.width, srcSize.height, rotationDegrees)

    @JvmStatic
    fun buffer(index: Int): ByteBuffer = slots[index].buf

    // Waits up to timeoutMs for a frame newer than lastSeq and marks it
    // being read until release(). Returns [slot, seq, timestamp_ns,
    // width, height, rotation_degrees], or null on timeout or interrupt().
    // Throws once the camera is lost.
    @JvmStatic
    fun acquire(lastSeq: Long, timeoutMs: Long): LongArray? {
        lock.withLock {
            val deadline = System.nanoTime() + timeoutMs * 1_000_000
            while (true) {
                error?.let { throw IOException(it) }
                if (woken) {
                    woken = false
                    return null
                }
                if (ready >= 0 && slots[ready].seq > lastSeq) break
                val left = (deadline - System.nanoTime()) / 1_000_000
                if (left <= 0) return null
                changed.await(left, TimeUnit.MILLISECONDS)
            }
            reading = ready
            val s = slots[reading]
            return longArrayOf(reading.toLong(), s.seq, s.timestamp, s.width.toLong(), s.height.toLong(), s.rotation.toLong())
        }
    }

    @JvmStatic
    fun release() {
        lock.withLock { reading = -1 }
    }

    // Wakes a waiting acquire(), so Interrupt is seen at once.
    @JvmStatic
    fun interrupt() {
        lock.withLock {
            woken = true
            changed.signalAll()
        }
    }

    // CameraControl. One operation at a time (the worker thread waits for
    // it): controlStart() begins it, controlWait() polls it, so the
    // native side can check for Interrupt between polls. Arguments by op:
    // TORCH a=on, TORCH_STRENGTH n, LOW_LIGHT a=on, ZOOM_RATIO a,
    // LINEAR_ZOOM a, EXPOSURE n, FOCUS a=x b=y (snapshot coordinates)
    // n=auto cancel ms (0: none) flags=AF|AE|AWB, CANCEL_FOCUS.
    const val OP_TORCH = 0
    const val OP_TORCH_STRENGTH = 1
    const val OP_LOW_LIGHT = 2
    const val OP_ZOOM_RATIO = 3
    const val OP_LINEAR_ZOOM = 4
    const val OP_EXPOSURE = 5
    const val OP_FOCUS = 6
    const val OP_CANCEL_FOCUS = 7

    private var pending: ListenableFuture<*>? = null
    private var controlMessage = ""

    @JvmStatic
    fun controlError(): String = controlMessage

    private fun refuse(kind: Int, message: String): Int {
        controlMessage = message
        return kind
    }

    // 0 started, else ERR_* with controlError().
    @JvmStatic
    fun controlStart(op: Int, a: Float, b: Float, n: Int, flags: Int): Int {
        val cam = camera ?: return refuse(ERR_LOST, "camera: lost -- open a new camera.Camera()")
        val control = cam.cameraControl
        val info = cam.cameraInfo
        pending = when (op) {
            OP_TORCH -> {
                if (!info.hasFlashUnit()) return refuse(ERR_UNSUPPORTED, "camera: no flash unit")
                control.enableTorch(a != 0f)
            }
            OP_TORCH_STRENGTH -> {
                if (!info.isTorchStrengthSupported) return refuse(ERR_UNSUPPORTED, "camera: torch strength not supported")
                if (n !in 1..info.maxTorchStrengthLevel) {
                    return refuse(ERR_VALUE, "camera: torch strength must be 1..${info.maxTorchStrengthLevel}")
                }
                control.setTorchStrengthLevel(n)
            }
            OP_LOW_LIGHT -> {
                if (!info.isLowLightBoostSupported) return refuse(ERR_UNSUPPORTED, "camera: low-light boost not supported")
                control.enableLowLightBoostAsync(a != 0f)
            }
            OP_ZOOM_RATIO -> {
                val z = info.zoomState.value
                if (z != null && (a < z.minZoomRatio || a > z.maxZoomRatio)) {
                    return refuse(ERR_VALUE, "camera: zoom ratio must be ${z.minZoomRatio}..${z.maxZoomRatio}")
                }
                control.setZoomRatio(a)
            }
            OP_LINEAR_ZOOM -> {
                if (a < 0f || a > 1f) return refuse(ERR_VALUE, "camera: linear zoom must be 0.0..1.0")
                control.setLinearZoom(a)
            }
            OP_EXPOSURE -> {
                val e = info.exposureState
                if (!e.isExposureCompensationSupported) return refuse(ERR_UNSUPPORTED, "camera: exposure compensation not supported")
                if (n !in e.exposureCompensationRange) {
                    return refuse(ERR_VALUE, "camera: exposure index must be ${e.exposureCompensationRange.lower}..${e.exposureCompensationRange.upper}")
                }
                control.setExposureCompensationIndex(n)
            }
            OP_FOCUS -> {
                val action = focusAction(a, b, flags, n) ?: return refuse(ERR_VALUE, "camera: point outside the image")
                if (!info.isFocusMeteringSupported(action)) return refuse(ERR_UNSUPPORTED, "camera: focus and metering not supported here")
                control.startFocusAndMetering(action)
            }
            OP_CANCEL_FOCUS -> control.cancelFocusAndMetering()
            else -> return refuse(ERR_VALUE, "camera: unknown control $op")
        }
        return 0
    }

    // A point in snapshot coordinates (upright, out size), turned back
    // into the analysis buffer's sensor orientation for CameraX.
    private fun focusAction(x: Float, y: Float, flags: Int, autoCancelMs: Int): FocusMeteringAction? {
        val useCase = analysis ?: return null
        val ow = outSize.width.toFloat()
        val oh = outSize.height.toFloat()
        if (x < 0f || y < 0f || x > ow || y > oh) return null
        val sw = srcSize.width.toFloat()
        val sh = srcSize.height.toFloat()
        val (sx, sy) = when (rotationDegrees) {
            90 -> Pair(y, sh - x)
            180 -> Pair(sw - x, sh - y)
            270 -> Pair(sw - y, x)
            else -> Pair(x, y)
        }
        val point = SurfaceOrientedMeteringPointFactory(sw, sh, useCase).createPoint(sx, sy)
        val builder = FocusMeteringAction.Builder(point, if (flags == 0) FOCUS_ALL else flags)
        if (autoCancelMs > 0) builder.setAutoCancelDuration(autoCancelMs.toLong(), TimeUnit.MILLISECONDS) else builder.disableAutoCancel()
        return builder.build()
    }

    private const val FOCUS_ALL = FocusMeteringAction.FLAG_AF or FocusMeteringAction.FLAG_AE or FocusMeteringAction.FLAG_AWB

    // null while running; else [0, value] (exposure index, focus success
    // 1/0, else 0) or [ERR_*] with controlError().
    @JvmStatic
    fun controlWait(timeoutMs: Long): DoubleArray? {
        val future = pending ?: return doubleArrayOf(0.0, 0.0)
        val result = try {
            future.get(timeoutMs, TimeUnit.MILLISECONDS)
        } catch (e: TimeoutException) {
            return null
        } catch (e: ExecutionException) {
            pending = null
            val cause = e.cause
            controlMessage = "camera: ${cause?.message ?: cause}"
            return doubleArrayOf((if (cause is IllegalArgumentException) ERR_VALUE else ERR_IO).toDouble())
        } catch (e: CancellationException) {
            pending = null
            controlMessage = "camera: control cancelled"
            return doubleArrayOf(ERR_IO.toDouble())
        }
        pending = null
        val value = when (result) {
            is Int -> result.toDouble()
            is FocusMeteringResult -> if (result.isFocusSuccessful) 1.0 else 0.0
            else -> 0.0
        }
        return doubleArrayOf(0.0, value)
    }

    // Gives up on a control the native side stopped waiting for.
    @JvmStatic
    fun controlAbandon() {
        pending = null
    }

    // CameraInfo of the open camera. Numbers by key; strings by key.
    const val INFO_SENSOR_ROTATION = 0
    const val INFO_INTRINSIC_ZOOM = 1
    const val INFO_HAS_FLASH = 2
    const val INFO_TORCH_STATE = 3
    const val INFO_TORCH_STRENGTH_SUPPORTED = 4
    const val INFO_MAX_TORCH_STRENGTH = 5
    const val INFO_TORCH_STRENGTH = 6
    const val INFO_ZOOM_STATE = 7
    const val INFO_EXPOSURE_STATE = 8
    const val INFO_FOCUS_SUPPORTED = 9
    const val INFO_FRAME_RATE_RANGES = 10
    const val INFO_LOW_LIGHT_SUPPORTED = 11
    const val INFO_LOGICAL_MULTI_CAMERA = 12
    const val INFO_LENS_FACING = 20
    const val INFO_IMPLEMENTATION_TYPE = 21
    const val INFO_LOW_LIGHT_STATE = 22

    // null when no camera is open. x, y: INFO_FOCUS_SUPPORTED's point.
    @JvmStatic
    fun infoNumbers(key: Int, x: Float, y: Float): DoubleArray? {
        val info = camera?.cameraInfo ?: return null
        fun b(v: Boolean) = if (v) 1.0 else 0.0
        return when (key) {
            INFO_SENSOR_ROTATION -> doubleArrayOf(info.sensorRotationDegrees.toDouble())
            INFO_INTRINSIC_ZOOM -> doubleArrayOf(info.intrinsicZoomRatio.toDouble())
            INFO_HAS_FLASH -> doubleArrayOf(b(info.hasFlashUnit()))
            INFO_TORCH_STATE -> doubleArrayOf(b(info.torchState.value == TorchState.ON))
            INFO_TORCH_STRENGTH_SUPPORTED -> doubleArrayOf(b(info.isTorchStrengthSupported))
            INFO_MAX_TORCH_STRENGTH -> doubleArrayOf(info.maxTorchStrengthLevel.toDouble())
            INFO_TORCH_STRENGTH -> doubleArrayOf((info.torchStrengthLevel.value ?: 0).toDouble())
            INFO_ZOOM_STATE -> info.zoomState.value.let { z ->
                if (z == null) doubleArrayOf(1.0, 1.0, 1.0, 0.0)
                else doubleArrayOf(z.zoomRatio.toDouble(), z.minZoomRatio.toDouble(), z.maxZoomRatio.toDouble(), z.linearZoom.toDouble())
            }
            INFO_EXPOSURE_STATE -> info.exposureState.let { e ->
                doubleArrayOf(
                    e.exposureCompensationIndex.toDouble(),
                    e.exposureCompensationRange.lower.toDouble(),
                    e.exposureCompensationRange.upper.toDouble(),
                    e.exposureCompensationStep.toDouble(),
                    b(e.isExposureCompensationSupported),
                )
            }
            INFO_FOCUS_SUPPORTED -> doubleArrayOf(b(focusAction(x, y, 0, 0)?.let { info.isFocusMeteringSupported(it) } ?: false))
            INFO_FRAME_RATE_RANGES -> info.supportedFrameRateRanges
                .sortedWith(compareBy({ it.lower }, { it.upper }))
                .flatMap { listOf(it.lower.toDouble(), it.upper.toDouble()) }.toDoubleArray()
            INFO_LOW_LIGHT_SUPPORTED -> doubleArrayOf(b(info.isLowLightBoostSupported))
            INFO_LOGICAL_MULTI_CAMERA -> doubleArrayOf(b(info.isLogicalMultiCameraSupported))
            else -> null
        }
    }

    @JvmStatic
    fun infoString(key: Int): String? {
        val info = camera?.cameraInfo ?: return null
        return when (key) {
            INFO_LENS_FACING -> facingName(info.lensFacing)
            INFO_IMPLEMENTATION_TYPE -> info.implementationType
            INFO_LOW_LIGHT_STATE -> when (info.lowLightBoostState.value) {
                LowLightBoostState.ACTIVE -> "active"
                LowLightBoostState.INACTIVE -> "inactive"
                else -> "off"
            }
            else -> null
        }
    }

    // Idempotent. Unbinds on the main looper and waits for it.
    @JvmStatic
    fun close() {
        lock.withLock {
            closing = true
            changed.signalAll()
        }
        val useCase = analysis
        val lifecycleOwner = owner
        if (useCase != null || lifecycleOwner != null) {
            onMain { unbind(useCase, lifecycleOwner) }
        }
        finishClose()
    }

    // EngineService.onDestroy(), on the main thread, before the worker's
    // own close: unbinds without waiting for the main looper.
    @JvmStatic
    fun closeOnMain() {
        lock.withLock {
            closing = true
            if (error == null) error = "camera: engine stopped"
            changed.signalAll()
        }
        unbind(analysis, owner)
        finishClose()
    }

    private fun unbind(useCase: ImageAnalysis?, lifecycleOwner: Owner?) {
        stateObserver?.let { camera?.cameraInfo?.cameraState?.removeObserver(it) }
        useCase?.let {
            it.clearAnalyzer()
            provider?.unbind(it)
        }
        lifecycleOwner?.registry?.currentState = Lifecycle.State.DESTROYED
    }

    private fun finishClose() {
        val hadCamera = analysis != null
        executor?.shutdown()
        executor = null
        analysis = null
        owner = null
        camera = null
        stateObserver = null
        lock.withLock {
            slots = emptyArray()
            ready = -1
            reading = -1
        }
        if (hadCamera) EngineService.setCameraRotation(-1)
    }
}

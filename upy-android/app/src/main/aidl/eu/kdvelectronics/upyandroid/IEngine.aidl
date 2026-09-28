package eu.kdvelectronics.upyandroid;

import android.os.Bundle;
import android.view.Surface;
import eu.kdvelectronics.upyandroid.IEngineOutputListener;
import eu.kdvelectronics.upyandroid.IEngineShareListener;

// Typed AIDL surface for the MicroPython engine running in the :engine
// process. Not the raw-REPL byte protocol micro-repl's CommandsManager
// used for USB serial.
// see session-state: IEngine.aidl#IEngine
interface IEngine {
    // Blocks until the code has finished executing. Output includes both
    // normal print() text and any uncaught-exception traceback.
    String exec(String code);

    // Safe to call while exec() is in flight on another call. Sets a
    // pending-exception flag the running script's VM loop polls.
    void interrupt();

    // Explicit, fast, deterministic in-process reset. Not unbind/rebind.
    // see session-state: IEngine.aidl#reset
    void reset();

    // Registers (or, with null, unregisters) a live listener for
    // incremental print()/traceback output produced during exec() calls.
    // This is in addition to, not instead of, the full accumulated
    // string exec() still returns on completion. Callers must call this
    // again after every reconnect: state does not survive a crashed
    // :engine process, since the listener lives in the new EngineService
    // instance.
    void setOutputListener(IEngineOutputListener listener);

    // Registers (or, with null, unregisters) a live listener for
    // android.fileprovider.share() requests raised from a running
    // script. Same reconnect caveat as setOutputListener above.
    // see session-state: IEngine.aidl#setShareListener
    void setShareListener(IEngineShareListener listener);

    // Hands :engine a Surface to draw into (Surface is Parcelable) for
    // display.SPIDisplay.write(). Pass null when the fourth screen's
    // SurfaceView is torn down; a running script's write() calls
    // silently no-op until a fresh Surface is handed over, and the
    // script itself is never interrupted by this.
    //
    // oneway: the caller (a main-thread SurfaceHolder.Callback) must
    // never block on this. display_module.cpp's g_window_mutex is also
    // held by write() for the full ANativeWindow_lock()/copy/
    // unlockAndPost() sequence on the worker thread, and
    // ANativeWindow_lock() can legitimately stall waiting for a free
    // graphics buffer -- a synchronous call here hit exactly that
    // window and produced a real ANR ("Input dispatching timed out,
    // waited 10001ms") on-device. oneway calls to the same interface
    // stay ordered, so a surface-then-null sequence still applies
    // correctly; nothing here ever depended on synchronous completion.
    // see session-state: IEngine.aidl#setDisplaySurface
    oneway void setDisplaySurface(in Surface surface);

    // Pushes the current, non-secret settings snapshot (see
    // SettingsManager.kt) into :engine, never ssh_password/
    // http_password. Written directly into a native struct on this
    // calling Binder thread, mirroring setDisplaySurface, not queued
    // through the worker thread. Called on every successful connect
    // (before the caller sees Connected) and again whenever the user
    // changes a setting while connected.
    // see session-state: IEngine.aidl#setSettings
    void setSettings(in Bundle settings);
}

package eu.kdvelectronics.upyandroid;

import android.os.Bundle;
import android.view.Surface;
import eu.kdvelectronics.upyandroid.IEngineOutputListener;
import eu.kdvelectronics.upyandroid.IEngineShareListener;

// Typed AIDL surface for the MicroPython engine running in the :engine
// process. Not the raw-REPL byte protocol micro-repl's CommandsManager
// used for USB serial.
interface IEngine {
    // Blocks until the code has finished executing. Returns "output":
    // print() text and any uncaught-exception traceback, and "exception":
    // the uncaught exception's type name, "" if none.
    Bundle exec(String code);

    // Safe to call while exec() is in flight on another call. Sets a
    // pending-exception flag the running script's VM loop polls.
    void interrupt();

    // Raw bytes for the running script's sys.stdin / input(). Not queued
    // behind exec(): the bytes go into a 64 KB FIFO. Returns the number of
    // bytes accepted (fewer than data.length when the FIFO is full), or -1
    // when no script runs.
    int sendInput(in byte[] data);

    // Explicit, fast, deterministic in-process reset. Not unbind/rebind.
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
    void setShareListener(IEngineShareListener listener);

    // Hands :engine a Surface to draw into (Surface is Parcelable) for
    // display.SPIDisplay.write(). Pass null when the fourth screen's
    // SurfaceView is torn down; a running script's write() calls
    // silently no-op until a fresh Surface is handed over, and the
    // script itself is never interrupted by this.
    //
    // oneway: the caller (a main-thread SurfaceHolder.Callback) must
    // never block on this. write() holds display_module.cpp's
    // g_window_mutex while ANativeWindow_lock() may wait for a free
    // graphics buffer, so a synchronous call could block the main thread
    // into an ANR. oneway calls to the same interface stay ordered, so a
    // surface-then-null sequence still applies correctly.
    oneway void setDisplaySurface(in Surface surface);

    // Pushes the current, non-secret settings snapshot (see
    // SettingsManager.kt) into :engine, never ssh_password.
    // Written directly into a native struct on this
    // calling Binder thread, mirroring setDisplaySurface, not queued
    // through the worker thread. Called on every successful connect
    // (before the caller sees Connected) and again whenever the user
    // changes a setting while connected.
    void setSettings(in Bundle settings);
}

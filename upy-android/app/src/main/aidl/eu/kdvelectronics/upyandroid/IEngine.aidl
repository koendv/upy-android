package eu.kdvelectronics.upyandroid;

import android.view.Surface;
import eu.kdvelectronics.upyandroid.IEngineOutputListener;

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

    // Hands :engine a Surface to draw into (Surface is Parcelable) for
    // display.SPIDisplay.write(). Pass null when the fourth screen's
    // SurfaceView is torn down; a running script's write() calls
    // silently no-op until a fresh Surface is handed over, and the
    // script itself is never interrupted by this.
    // see session-state: IEngine.aidl#setDisplaySurface
    void setDisplaySurface(in Surface surface);
}

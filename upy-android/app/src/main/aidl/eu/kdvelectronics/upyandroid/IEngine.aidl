package eu.kdvelectronics.upyandroid;

import android.view.Surface;
import eu.kdvelectronics.upyandroid.IEngineOutputListener;

// Typed AIDL surface for the MicroPython engine, running in the :engine
// process. Deliberately NOT the raw-REPL byte protocol (Ctrl-A/B/C/D,
// >OK/\x04 markers) that micro-repl's CommandsManager used for USB serial
// -- that existed only because a UART gives you nothing better. Binder
// gives real typed call/return, so exec() just returns the captured
// output directly (see project memory).
interface IEngine {
    // Blocks until the code has finished executing. Output includes both
    // normal print() text and any uncaught-exception traceback.
    String exec(String code);

    // Safe to call while exec() is in flight on another call -- sets a
    // pending-exception flag the running script's VM loop polls.
    void interrupt();

    // Explicit, fast, deterministic in-process reset (NOT unbind/rebind
    // -- see the two-tier reset design in project memory).
    void reset();

    // Registers (or, with null, unregisters) a live listener for
    // incremental print()/traceback output produced during exec() calls,
    // in addition to (not instead of) the full accumulated string exec()
    // still returns on completion. Callers must call this again after
    // every reconnect -- state does not survive a crashed :engine
    // process, since the listener lives in the (new) EngineService
    // instance.
    void setOutputListener(IEngineOutputListener listener);

    // Hands :engine a Surface to draw into (Surface is Parcelable) for
    // display.SPIDisplay.write() -- native code wraps it via
    // ANativeWindow_fromSurface(). Pass null when the fourth screen's
    // SurfaceView is torn down (navigated away, backgrounded); a running
    // script's write() calls silently no-op until a fresh Surface is
    // handed over, per the surface-lifecycle decision (project memory /
    // SESSION_STATE.yaml) -- the script itself is never interrupted by
    // this.
    void setDisplaySurface(in Surface surface);
}

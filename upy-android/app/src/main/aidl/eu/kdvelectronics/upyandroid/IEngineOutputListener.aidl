package eu.kdvelectronics.upyandroid;

// Streamed output callback: the :engine process's worker thread pushes
// each chunk of print()/traceback text as it's produced, so a
// long-running script (e.g. a `while True: print(...)` loop) can show
// live progress instead of waiting for exec() to return -- exec() alone
// never surfaces anything until the whole script finishes (see
// IEngine.aidl / project memory). oneway is required: this call crosses
// from the :engine process's worker thread back into the main process,
// and a blocking call there would stall script execution on UI
// responsiveness and risks cross-process reentrancy.
oneway interface IEngineOutputListener {
    void onOutputChunk(String text);
}

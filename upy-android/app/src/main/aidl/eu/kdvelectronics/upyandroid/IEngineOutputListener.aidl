package eu.kdvelectronics.upyandroid;

// Streamed output callback for incremental print()/traceback text
// produced while a script runs in the :engine process.
// see session-state: IEngineOutputListener.aidl#IEngineOutputListener
//
// oneway is required: this call crosses from the :engine process's
// worker thread back into the main process. A blocking call here would
// stall script execution on UI responsiveness and risks cross-process
// reentrancy.
oneway interface IEngineOutputListener {
    void onOutputChunk(String text);
}

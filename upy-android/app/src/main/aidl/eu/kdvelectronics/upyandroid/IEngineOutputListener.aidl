package eu.kdvelectronics.upyandroid;

// Streamed output callback for incremental print()/traceback text
// produced while a script runs in the :engine process.
//
// oneway is required: this call crosses from the :engine process's
// worker thread back into the main process. A blocking call here would
// stall script execution on UI responsiveness and risks cross-process
// reentrancy.
oneway interface IEngineOutputListener {
    void onOutputChunk(String text);

    // A running script waits for input (true) or no longer does (false):
    // sys.stdin.read/readline, input(). Sent when a read finds the stdin
    // FIFO empty, and when the read gets its data or the script ends.
    void onInputWaiting(boolean waiting);
}

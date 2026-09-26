package eu.kdvelectronics.upyandroid;

// Share-request callback for android.fileprovider.share() (Part 7).
// Same "crosses from :engine's worker thread back into the main
// process" reasoning as IEngineOutputListener -- oneway for the same
// reason (a blocking call here would stall script execution, and
// starting an Activity is inherently a main-process/UI-context action
// anyway, so this call is one-way by nature, not just for performance).
// see session-state: IEngineShareListener.aidl#IEngineShareListener
oneway interface IEngineShareListener {
    void onShareRequest(String path, String mimeType);
}

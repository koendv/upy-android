package eu.kdvelectronics.upyandroid;

// Share-request callback for android.fileprovider.share(). Same
// "crosses from :engine's worker thread back into the main process"
// reasoning as IEngineOutputListener.
// see session-state: IEngineShareListener.aidl#IEngineShareListener
oneway interface IEngineShareListener {
    void onShareRequest(String path, String mimeType);
}

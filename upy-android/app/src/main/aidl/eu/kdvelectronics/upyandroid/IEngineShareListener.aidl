package eu.kdvelectronics.upyandroid;

// Share-request callback for android.fileprovider.share(). Same
// "crosses from :engine's worker thread back into the main process"
// reasoning as IEngineOutputListener.
// see session-state: IEngineShareListener.aidl#IEngineShareListener
//
// Also carries runtime-permission requests (android.location): only the
// main process has an Activity to show the permission prompt.
// And the screen lock while a camera is open.
oneway interface IEngineShareListener {
    void onShareRequest(String path, String mimeType);

    void onPermissionRequest(in String[] permissions);

    // Surface.ROTATION_* the camera image is upright in; the UI locks to
    // it. -1: no camera open, unlock.
    void onCameraOrientation(int rotation);
}

# test: manual the share sheet must be checked by eye
#
# android.fileprovider confidence test. Confirms the JNI/AIDL chain
# (fileprovider_module.cpp -> EngineService.requestShare() ->
# IEngineShareListener -> MainActivity's shareRequestListener ->
# FileProviderShim.shareFile()) actually reaches the main process and
# raises no exception. The one thing this script cannot confirm on its
# own is that the OS share chooser sheet actually appeared on screen.
# That is a real, visually-verifiable side effect, checked separately
# via a screenshot after running this.
#
# Same self-contained 1x1 JPEG fixture as mediastore_selftest.py. No
# camera/ulab dependency.
import android

JPEG_1X1 = bytes.fromhex(
    "ffd8ffe000104a46494600010100000100010000ffdb004300010101010101"
    "01010101010101010101010101010101010101010101010101010101010101"
    "01010101010101010101010101010101010101010101010101010101010101"
    "0101010101ffc0000b080001000101011100ffc4001f0000010501010101010"
    "10000000000000000000102030405060708090a0bffc400b5100002010303020"
    "403050504040000017d01020300041105122131410613516107227114328191"
    "a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363"
    "738393a434445464748494a535455565758595a636465666768696a73747576"
    "7778797a838485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3"
    "b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7"
    "e8e9eaf1f2f3f4f5f6f7f8f9faffda0008010100003f00fb"
    "d9"
)


def main():
    print("android.fileprovider selftest")
    path = "/upy_fileprovider_selftest.jpg"
    try:
        with open(path, "wb") as f:
            f.write(JPEG_1X1)
        android.fileprovider.share(path, "image/jpeg")
    except (OSError, RuntimeError) as e:
        print("FAIL:", e)
        return

    print("share() returned without raising -- check screen for the share sheet")
    print("PASS")


main()

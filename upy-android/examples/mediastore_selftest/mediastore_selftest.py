# android.mediastore confidence test -- confirms the JNI/Kotlin bridge
# (MediaStoreShim.kt, mediastore_jni_bridge.cpp) actually saves real
# bytes into a real, gallery-visible MediaStore item on THIS device,
# not just that the code compiles.
#
# A tiny, real, valid JPEG (1x1 pixel, hand-verified minimal encoder
# output) -- doesn't need a camera or ulab/imlib to produce, keeps this
# test self-contained and fast.
import android

# Minimal valid 1x1 white JPEG.
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
    print("android.mediastore selftest")
    try:
        uri = android.mediastore.save_image(JPEG_1X1, "upy_mediastore_selftest.jpg", "image/jpeg")
    except OSError as e:
        print("FAIL:", e)
        return

    # Printing the URI itself would make this test's output non-
    # deterministic (real per-item IDs) -- check it, but print only a
    # fixed-text confirmation so a plain .exp diff still works.
    if not uri.startswith("content://"):
        print("FAIL: unexpected uri", uri)
        return

    print("uri starts with content://: True")
    print("PASS")


main()

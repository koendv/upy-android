# Adapted from OpenMV scripts/unittest/tests/qrcodes.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer.

def unittest(data_path, temp_path):
    import image

    img = image.Image(data_path + "/qrcode.pgm")
    codes = img.find_qrcodes()
    return len(codes) == 1 and codes[0][0:10] == (
        76,
        36,
        168,
        168,
        "https://openmv.io",
        1,
        1,
        3,
        4,
        0,
    )

# Adapted from OpenMV scripts/unittest/tests/barcodes.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer.

def unittest(data_path, temp_path):
    import image

    img = image.Image(data_path + "/barcode.pgm")
    codes = img.find_barcodes()
    return codes[0][0:8] == (11, 12, 514, 39, "https://openmv.io/", 15, 0.0, 40)

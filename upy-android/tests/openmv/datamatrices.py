# Adapted from OpenMV scripts/unittest/tests/datamatrices.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer.

def unittest(data_path, temp_path):
    import image

    img = image.Image(data_path + "/datamatrix.pgm")
    matrices = img.find_datamatrices()
    return len(matrices) == 1 and matrices[0][0:10] == (
        34,
        15,
        90,
        89,
        "https://openmv.io/",
        0.0,
        18,
        18,
        18,
        0,
    )

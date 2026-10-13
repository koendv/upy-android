# Adapted from OpenMV scripts/unittest/tests/eye.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer.

def unittest(data_path, temp_path):
    import image

    img = image.Image(data_path + "/eye.pgm")
    iris = img.find_eye(roi=(100, 70, 250, 100))
    return iris == (159, 114)

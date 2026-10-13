# Adapted from OpenMV scripts/unittest/tests/rects.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer.

def unittest(data_path, temp_path):
    import image

    img = image.Image(data_path + "/shapes.ppm")
    rects = img.find_rects(threshold=50000)
    return len(rects) == 1 and rects[0][0:5] == (23, 39, 35, 36, 146566)

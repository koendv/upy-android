# Adapted from OpenMV scripts/unittest/tests/grayscale_to_rgb.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Unchanged.

def unittest(data_path, temp_path):
    import image

    rgb = image.grayscale_to_rgb(182)
    return rgb[0] == 181 and rgb[1] == 182 and rgb[2] == 181

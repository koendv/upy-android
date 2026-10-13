# Adapted from OpenMV scripts/unittest/tests/rgb_to_grayscale.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Unchanged.

def unittest(data_path, temp_path):
    import image

    gs = image.rgb_to_grayscale((120, 200, 120))
    return gs == 169

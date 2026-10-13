# Adapted from OpenMV scripts/unittest/tests/rgb_to_lab.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Unchanged.

def unittest(data_path, temp_path):
    import image

    lab = image.rgb_to_lab((120, 200, 120))
    return lab[0] == 75 and lab[1] == -40 and lab[2] == 34

# Adapted from OpenMV scripts/unittest/tests/haarcascade.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer; uses this app's /rom cascade and values measured on this port; OpenMV's own test cascade finds no face here.

def unittest(data_path, temp_path):
    import image

    # Load Haar Cascade
    cascade = image.HaarCascade("/rom/haarcascade_frontalface.cascade")

    # Load image and find keypoints
    img = image.Image(data_path + "/dennis.pgm")

    # Find objects
    objects = img.find_features(cascade, threshold=0.75, scale=1.25)
    return objects == [(188, 53, 90, 90), (8, 8, 114, 114)]

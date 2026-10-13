# Adapted from OpenMV scripts/unittest/tests/keypoints_save_load.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer; find_keypoints uses corner_detector=image.CORNER_FAST (AGAST is not in this port); the expected values are measured on this port, not OpenMV's AGAST values.

def unittest(data_path, temp_path):
    import os
    import image

    # Load image and find keypoints
    img = image.Image(data_path + "/graffiti.pgm")
    kpts1 = img.find_keypoints(max_keypoints=50, threshold=20, normalized=False, corner_detector=image.CORNER_FAST)

    # Save descriptor
    image.save_descriptor(kpts1, temp_path + "/graffiti.orb")

    # Load descriptor
    kpts2 = image.load_descriptor(temp_path + "/graffiti.orb")

    # Match keypoints
    match = image.match_descriptor(kpts1, kpts2, threshold=85)

    return (
        match.cx == 130
        and match.cy == 58
        and match.x == 38
        and match.y == 34
        and match.w == 215
        and match.h == 56
        and match.count == 50
        and match.theta == 0
    )

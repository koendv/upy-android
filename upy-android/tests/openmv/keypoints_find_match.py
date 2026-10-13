# Adapted from OpenMV scripts/unittest/tests/keypoints_find_match.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer; find_keypoints uses corner_detector=image.CORNER_FAST (AGAST is not in this port); the expected values are measured on this port, not OpenMV's AGAST values.

def unittest(data_path, temp_path):
    import image

    # Load image and find keypoints
    img = image.Image(data_path + "/graffiti.pgm")
    kpts1 = img.find_keypoints(max_keypoints=50, threshold=20, normalized=False, corner_detector=image.CORNER_FAST)

    # Load descriptor
    kpts2 = image.load_descriptor(data_path + "/graffiti.orb")

    # Match keypoints
    match = image.match_descriptor(kpts1, kpts2, threshold=85)
    return (
        match.cx == 133
        and match.cy == 54
        and match.x == 37
        and match.y == 34
        and match.w == 216
        and match.h == 41
        and match.count == 18
        and match.theta == 0
    )

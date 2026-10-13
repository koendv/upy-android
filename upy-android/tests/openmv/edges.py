# Adapted from OpenMV scripts/unittest/tests/edges.py
# (MIT license, Copyright (c) 2013-2023 OpenMV LLC).
# Changed: copy_to_fb removed, this port has no frame buffer.

def unittest(data_path, temp_path):
    import image

    img = image.Image(data_path + "/edges1.pgm")

    # Find edges
    img.find_edges(image.EDGE_CANNY, threshold=(50, 80))

    # Verify edge detection produced output
    stats = img.difference(data_path + "/edges2.pgm").get_statistics()
    return stats.max == 0 and stats.min == 0

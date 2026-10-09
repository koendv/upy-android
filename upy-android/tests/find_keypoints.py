# test: known-failure find_keypoints() returns None (2026-10-05)
#
# find_keypoints() on a synthetic checkerboard: 320x240 grayscale, 20 px
# squares. find_lines() finds the edges, find_keypoints() should find the
# corners. The .exp holds the correct output.
import image

W, H, SQ = 320, 240, 20
img = image.Image(W, H, image.GRAYSCALE)
img.draw_rectangle((0, 0, W, H), color=255, fill=True)
for y in range(0, H, SQ):
    for x in range(0, W, SQ):
        if (x // SQ + y // SQ) % 2:
            img.draw_rectangle((x, y, SQ, SQ), color=0, fill=True)

print("lines found:", len(img.find_lines(threshold=1000)) > 0)
print("keypoints found:", img.find_keypoints() is not None)

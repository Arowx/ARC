# compare.py BUILD_DIR: the spike's read-back pictures (spike*.rgba) against ARC's software renderer (ref.png)
import sys, os, glob
from PIL import Image
d = sys.argv[1]; ref = Image.open(os.path.join(d, 'ref.png')).convert('RGB'); W, H = ref.size; R = ref.tobytes()
for f in sorted(glob.glob(os.path.join(d, 'spike*.rgba'))):
    raw = open(f, 'rb').read(); img = Image.frombytes('RGBA', (W, H), raw).convert('RGB'); img.save(f[:-5] + '.png'); S = img.tobytes()
    diffs = [max(abs(R[i] - S[i]), abs(R[i + 1] - S[i + 1]), abs(R[i + 2] - S[i + 2])) for i in range(0, len(R), 3)]
    n = len(diffs); over2 = sum(1 for x in diffs if x > 2); over16 = sum(1 for x in diffs if x > 16)
    print('%s: mean %.3f, max %d, %.2f%% of pixels differ by > 2, %.2f%% by > 16 (edges)' % (os.path.basename(f), sum(diffs) / n, max(diffs), 100.0 * over2 / n, 100.0 * over16 / n))

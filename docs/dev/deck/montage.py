import sys
from PIL import Image
out, crop = sys.argv[1], tuple(int(v) for v in sys.argv[2].split(","))
ims = [Image.open(p).convert("RGB").crop(crop) for p in sys.argv[3:]]
w, h = ims[0].size
m = Image.new("RGB", (w, h * len(ims)))
for i, im in enumerate(ims): m.paste(im, (0, i * h))
m.save(out)

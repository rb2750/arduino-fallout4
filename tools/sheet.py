import sys
from PIL import Image
out, *names = sys.argv[1:]
sheet = Image.new('RGB', (960, 320 * ((len(names) + 1) // 2)), (60, 0, 60))
for i, n in enumerate(names):
    sheet.paste(Image.open(n), ((i % 2) * 480, (i // 2) * 320))
sheet.save(out)

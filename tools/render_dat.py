import sys, glob, collections, os
from PIL import Image, ImageDraw, ImageFont

def parse(d):
    i = 0; out = []; size = 1
    le = lambda j: d[j] | d[j+1] << 8
    fixed = {1: 3, 2: 3, 3: 4, 4: 4, 5: 6, 8: 4}
    while i < len(d):
        op = d[i]
        if i + 1 + 2*fixed.get(op, 3) > len(d):
            print('  truncated at', i, 'of', len(d)); break
        if op in fixed:
            n = fixed[op]
            args = [le(i+1+2*k) for k in range(n)]
            i += 1 + 2*n
            out.append((op, args, d[i], None)); i += 1
        elif op == 6:
            x, y = le(i+1), le(i+3); col = d[i+5]; n = d[i+6]
            out.append((6, [x, y, size, 0], col, d[i+7:i+7+n].decode('latin1'))); i += 7 + n
        elif op == 7:
            size = d[i+1]; i += 2
        else:
            raise ValueError(f"op {op:#x} at {i}: {d[i:i+20].hex(' ')}")
    return out

PAL = {0: (11, 114, 57), 1: (26, 255, 128)}

FONT = None
def text(g, x, y, s, c, size):
    # Adafruit GFX classic 5x7 font, 6px advance, read from glcdfont.c
    for ch in s:
        cols = FONT[ord(ch)*5:ord(ch)*5+5]
        for cx, bits in enumerate(cols):
            for cy in range(8):
                if bits >> cy & 1:
                    g.rectangle([x+cx*size, y+cy*size, x+cx*size+size-1, y+cy*size+size-1], fill=c)
        x += 6*size

def render_mono(cmds, image):
    g = ImageDraw.Draw(image)
    for op, a, col, s in cmds:
        if op == 1: g.line([a[0], a[1], a[0]+a[2]-1, a[1]], fill=1)
        elif op == 2: g.line([a[0], a[1], a[0], a[1]+a[2]-1], fill=1)
        elif op == 3: g.rectangle([a[0], a[1], a[0]+a[2]-1, a[1]+a[3]-1], fill=1)
        elif op == 4: g.line(a, fill=1)
        elif op == 5: g.polygon([(a[0], a[1]), (a[2], a[3]), (a[4], a[5])], fill=1)
        elif op == 8: g.rectangle([a[0], a[1], a[0]+a[2]-1, a[1]+a[3]-1], outline=1)

def render(cmds, path, im=None):
    if im is None: im = Image.new('RGB', (480, 320), (0, 0, 0))
    g = ImageDraw.Draw(im)
    for op, a, col, s in cmds:
        c = PAL.get(col, (255, 0, 255))
        if op == 1: g.line([a[0], a[1], a[0]+a[2]-1, a[1]], fill=c)
        elif op == 2: g.line([a[0], a[1], a[0], a[1]+a[2]-1], fill=c)
        elif op == 3: g.rectangle([a[0], a[1], a[0]+a[2]-1, a[1]+a[3]-1], fill=c)
        elif op == 4: g.line(a, fill=c)
        elif op == 8: g.rectangle([a[0], a[1], a[0]+a[2]-1, a[1]+a[3]-1], outline=c)
        elif op == 5: g.polygon([(a[0], a[1]), (a[2], a[3]), (a[4], a[5])], fill=c)
        else:
            size = a[2]
            text(g, a[0], a[1], s, c, size)
    if path: im.save(path)

def load_font():
    import re, glob
    src = open(glob.glob(os.path.expanduser('~/Arduino/libraries/Adafruit_GFX_Library/glcdfont.c'))[0]).read()
    body = src[src.index('{'):]
    return bytes(int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', body))

if __name__ == '__main__':
    FONT = load_font()
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for f in sorted(glob.glob(os.path.join(src, '*.DAT'))):
        cmds = parse(open(f, 'rb').read())
        stats = collections.Counter(c[0] for c in cmds); cols = collections.Counter(c[2] for c in cmds)
        print(os.path.basename(f), dict(stats), 'colours', dict(cols))
        for c in cmds:
            if c[3]: print('   ', c[0], c[1], c[2], repr(c[3]))
        render(cmds, os.path.join(dst, os.path.basename(f)[:-4] + '.png'))

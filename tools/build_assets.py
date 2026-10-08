"""Compiles the Pip-Boy screen designs (the .DAT files pulled from the SD card) into:

  build/PIPART/LAYnn.PIP   text, boxes and brackets per page (15..17 are the frames), bit packed
  build/PIPART/ARTnn.PIP   Vault Boy pictures as run-length 1 bit bitmaps
  pipboy/layout.h          bottom label positions, which the firmware animates

Live elements are dropped from the layout because the firmware draws and animates them: frame
labels, the DATA clock, the limb condition bars, selection markers and the radio waveform.
Then both outputs are decoded again and compared pixel for pixel with the original design.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import render_dat as r
from split_art import split, bbox, repair, ART_BOXES
from PIL import Image, ImageChops, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGES = ['00', '01', '02', '03', '04', '10', '11', '12', '13', '14', None, None, '22', '23', '24']
OPS = {1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 8: 7}
FIELDS = {1: 3, 2: 3, 3: 4, 4: 4, 5: 6, 8: 4}


class Bits:
    def __init__(self):
        self.out, self.acc, self.count = bytearray(), 0, 0

    def put(self, value, width):
        assert 0 <= value < 1 << width, (value, width)
        for i in reversed(range(width)):
            self.acc = self.acc << 1 | (value >> i) & 1
            self.count += 1
            if self.count == 8:
                self.out.append(self.acc); self.acc = self.count = 0

    def done(self):
        if self.count: self.out.append(self.acc << (8 - self.count))
        return bytes(self.out)


def live(name, c):
    op, a, col, text = c
    if name.startswith('FRAME'):
        return op == 6 and (a[1] >= 296 or ',' in text and '.77' in text)
    if name == '00': return op == 3 and a[2] == 43
    if name == '24': return op == 4 or (op == 3 and a[0] == 38)
    return False


def on_fill(text_box, commands):
    for op, a, col, _ in commands:
        if op == 3 and not (text_box[2] < a[0] or text_box[0] >= a[0] + a[2] or text_box[3] < a[1] or text_box[1] >= a[1] + a[3]):
            return True
    return False


def encode_layout(commands):
    # Text on a filled box is drawn transparently. Everything else can use the faster opaque path.
    bits = Bits()
    for op, a, col, text in commands:
        bits.put(OPS[op], 3); bits.put(1 if col else 0, 1)
        if op == 6:
            bits.put(1 if on_fill(bbox((op, a, col, text)), commands) else 0, 1)
            bits.put(a[0], 9); bits.put(a[1], 9); bits.put(a[2] - 1, 1); bits.put(len(text), 6)
            for ch in text: bits.put(ord(ch), 7)
        else:
            for v in a[:FIELDS[op]]: bits.put(v, 9)
    bits.put(0, 3)
    return bits.done()


def pack_art(name, art):
    box = ART_BOXES[name]
    image = Image.new('1', (480, 320), 0)
    r.render_mono(art, image)
    crop = image.crop(box[:2] + (box[2] + 1, box[3] + 1))
    tight = crop.getbbox()
    x0, y0 = box[0] + tight[0], box[1] + tight[1]
    crop = crop.crop(tight)
    w, h = crop.size
    px = crop.load()
    runs, current, length = bytearray(), 0, 0
    for y in range(h):
        for x in range(w):
            value = 1 if px[x, y] else 0
            if value == current: length += 1; continue
            while length >= 255: runs += bytes([255, 0]); length -= 255
            runs.append(length); current, length = value, 1
    while length >= 255: runs += bytes([255, 0]); length -= 255
    runs.append(length)
    header = bytes([x0 & 255, x0 >> 8, y0 & 255, y0 >> 8, w & 255, w >> 8, h & 255, h >> 8])
    return header + bytes(runs)


def decode_layout(data, image):
    g = ImageDraw.Draw(image)
    position = [0]

    def get(width):
        value = 0
        for _ in range(width):
            byte, bit = divmod(position[0], 8)
            value = value << 1 | (data[byte] >> (7 - bit)) & 1
            position[0] += 1
        return value
    commands = []
    while True:
        op = get(3)
        if not op: break
        col = get(1)
        if op == 6:
            get(1)
            x, y, size, n = get(9), get(9), get(1) + 1, get(6)
            commands.append((6, [x, y, size, 0], col, ''.join(chr(get(7)) for _ in range(n))))
        else:
            real = {v: k for k, v in OPS.items()}[op]
            commands.append((real, [get(9) for _ in range(FIELDS[real])], col, None))
    r.render(commands, None, image)


def decode_art(data, image):
    x0, y0, w, h = (data[i] | data[i + 1] << 8 for i in (0, 2, 4, 6))
    px = image.load()
    at, value = 0, 0
    for run in data[8:]:
        for _ in range(run):
            if value: px[x0 + at % w, y0 + at // w] = r.PAL[1]
            at += 1
        value ^= 1
    assert at == w * h, (at, w * h)


def c_array(name, data):
    rows = [', '.join('0x%02X' % b for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    return f'static const uint8_t {name}[] PROGMEM = {{\n  ' + ',\n  '.join(rows) + '\n};\n'


def main():
    r.FONT = r.load_font()
    src = os.path.join(ROOT, 'sdcard')
    art_dir = os.path.join(ROOT, 'build', 'PIPART')
    check_dir = os.path.join(ROOT, 'build', 'check')
    os.makedirs(art_dir, exist_ok=True); os.makedirs(check_dir, exist_ok=True)
    out = ['// Generated by tools/build_assets.py from the screen designs. Do not edit.\n#pragma once\n']
    names = []
    labels = []
    worst = 0
    for frame in range(3):
        commands = r.parse(open(f'{src}/FRAME{frame}.DAT', 'rb').read())
        labels.append([(c[1][0], c[3]) for c in commands if c[0] == 6 and c[1][1] >= 296])
        layout = [c for c in commands if not live(f'FRAME{frame}', c)]
        data = encode_layout(layout)
        assert len(data) <= 512
        open(os.path.join(art_dir, f'LAY{15 + frame}.PIP'), 'wb').write(data)
    for page, name in enumerate(PAGES):
        if name is None: continue
        commands = r.parse(open(f'{src}/SCREEN{name}.DAT', 'rb').read())
        art, layout = split(name, commands) if name in ART_BOXES else ([], commands)
        kept = [c for c in layout if not live(name, c)]
        data = encode_layout(kept)
        # The firmware reads a layout as one sector.
        assert len(data) <= 512, (name, len(data))
        open(os.path.join(art_dir, f'LAY{page:02d}.PIP'), 'wb').write(data)
        picture = None
        if art:
            picture = pack_art(name, art)
            open(os.path.join(art_dir, f'ART{page:02d}.PIP'), 'wb').write(picture)
        # Rebuild from the compiled outputs plus the live elements and compare with the design.
        frame = r.parse(open(f'{src}/FRAME{int(name[0])}.DAT', 'rb').read())
        rebuilt = Image.new('RGB', (480, 320))
        decode_layout(encode_layout(frame), rebuilt)
        decode_layout(data, rebuilt)
        if picture: decode_art(picture, rebuilt)
        r.render([c for c in layout if live(name, c)], None, rebuilt)
        # Compared with the design plus its deliberate repairs (the right boot, the torso bar).
        original = Image.new('RGB', (480, 320))
        r.render(frame + repair(name, commands), None, original)
        difference = ImageChops.difference(original, rebuilt).convert('L').point(lambda v: 255 if v else 0)
        wrong = sum(1 for v in difference.get_flattened_data() if v)
        worst = max(worst, wrong)
        rebuilt.save(os.path.join(check_dir, f'page{page:02d}.png'))
        print(f'page {page:2d} SCREEN{name}  layout {len(data):5d} B  art {len(picture) if picture else 0:5d} B  pixels different {wrong}')
    for frame, items in enumerate(labels):
        out.append(f'static const uint16_t LABEL_X_{frame}[] PROGMEM = {{' + ', '.join(str(x) for x, _ in items) + '};\n')
        out.append(f'static const char LABEL_TEXT_{frame}[] PROGMEM = "' + '\\0'.join(t for _, t in items) + '";\n')
    open(os.path.join(ROOT, 'pipboy', 'layout.h'), 'w').write('\n'.join(out))
    total = sum(len(v) for v in [encode_layout([])])
    print('worst page differs by', worst, 'pixels')
    return worst


if __name__ == '__main__':
    sys.exit(1 if main() else 0)

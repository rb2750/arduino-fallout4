"""Splits each screen file into Vault Boy art (packed bitmaps on SD) and layout (compiled into flash)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import render_dat as r

ART_BOXES = {
    '00': (115, 40, 365, 270), '01': (265, 40, 385, 182), '02': (315, 40, 415, 170),
    '03': (305, 40, 415, 185), '04': (300, 75, 380, 210), '10': (270, 55, 400, 192),
    '11': (290, 45, 390, 186), '12': (295, 50, 415, 186), '13': (285, 45, 430, 186),
    '14': (295, 45, 410, 186),
}

def bbox(c):
    op, a = c[0], c[1]
    if op == 1: return a[0], a[1], a[0] + a[2] - 1, a[1]
    if op == 2: return a[0], a[1], a[0], a[1] + a[2] - 1
    if op in (3, 8): return a[0], a[1], a[0] + a[2] - 1, a[1] + a[3] - 1
    if op == 4: return min(a[0], a[2]), min(a[1], a[3]), max(a[0], a[2]), max(a[1], a[3])
    if op == 5: return min(a[0::2]), min(a[1::2]), max(a[0::2]), max(a[1::2])
    if op == 6: return a[0], a[1], a[0] + 6 * a[2] * len(c[3]) - 1, a[1] + 8 * a[2] - 1

def inside(b, box):
    return b[0] >= box[0] and b[1] >= box[1] and b[2] <= box[2] and b[3] <= box[3]

# The design's Vault Boy has no boot on his right leg, just a rounded stub. Replace the stub with a
# toe shaped like the left boot.
BOOT_STUB = [[283, 241, 285, 250], [285, 250, 283, 255], [283, 255, 280, 257]]
RIGHT_BOOT = [[283, 241, 300, 240], [300, 240, 305, 241], [305, 241, 304, 245], [304, 245, 297, 248],
              [297, 248, 288, 252], [288, 252, 283, 256], [283, 256, 280, 257]]

# The torso bar's bracket crossed his chest under the chin. It moves below his feet, and the name
# moves down to make room. The bar itself is live and moves with it in the firmware.
TORSO_BRACKET = [(1, [218, 136, 47]), (2, [218, 136, 10]), (2, [264, 136, 10])]
TORSO_DROP = 124

def repair(name, commands):
    if name != '00': return commands
    kept = [c for c in commands if not (c[0] == 4 and c[1] in BOOT_STUB)]
    assert len(commands) - len(kept) == len(BOOT_STUB)
    moved = []
    for c in kept:
        if (c[0], c[1]) in TORSO_BRACKET:
            c = (c[0], [c[1][0], c[1][1] + TORSO_DROP] + c[1][2:], c[2], c[3])
        elif c[0] == 6 and c[3].startswith('Peter'):
            c = (6, [c[1][0], c[1][1] + 6] + c[1][2:], c[2], c[3])
        moved.append(c)
    assert sum(1 for c in moved if c[0] in (1, 2) and c[1][1] == 260) == 3
    return moved + [(4, line, 1, None) for line in RIGHT_BOOT]

def split(name, commands):
    commands = repair(name, commands)
    box = ART_BOXES.get(name)
    art, layout = [], []
    for c in commands:
        if box and c[0] != 6 and not (name == '00' and c[0] == 3) and inside(bbox(c), box):
            art.append(c)
        else:
            layout.append(c)
    return art, layout

if __name__ == '__main__':
    from PIL import Image, ImageChops
    src, dst = sys.argv[1], sys.argv[2]
    r.FONT = r.load_font()
    for name in sorted(ART_BOXES):
        commands = r.parse(open(f'{src}/SCREEN{name}.DAT', 'rb').read())
        art, layout = split(name, commands)
        r.render(art, f'{dst}/art{name}.png'); r.render(layout, f'{dst}/layout{name}.png')
        print(name, 'art', len(art), 'layout', len(layout))

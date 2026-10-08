import os, sys, time
sys.path.insert(0, '.')
import pip
LABEL_X = [[60, 160, 250, 333, 414], [70, 160, 250, 332, 415], [77, 175, 265, 342, 415]]
port = pip.connect()
t = time.time(); buf = b''
while time.time() - t < 25:
    buf += port.read(256)
    if b'RAW' in buf: break
port.write(b'K'); time.sleep(2)
steps = sys.argv[1:]
for step in steps:
    kind, *args = step.split(':')
    if kind == 'tap':
        x, y = map(int, args[0].split(','))
        print(step, pip.tap(port, x, y)); time.sleep(0.4)
    elif kind == 'hold':
        x, y = map(int, args[0].split(','))
        print(step, pip.tap(port, x, y, b'H')); time.sleep(0.4)
    elif kind == 'wait':
        time.sleep(float(args[0]))
    elif kind == 'cmd':
        print(step, pip.simple(port, args[0])); time.sleep(0.5)
    elif kind == 'shot':
        t0 = time.time(); pip.screenshot(port, os.path.join(os.environ.get('SHOTS', '.'), args[0] + '.png')); print(step, round(time.time() - t0, 1))

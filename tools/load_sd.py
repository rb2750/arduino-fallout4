"""Copies build/PIPART onto the SD card through the sdloader sketch, then reads every file back.

Flash tools/sdloader first. Only the PIPART folder on the card is touched.
Usage: python3 tools/load_sd.py [port] [file ...]
"""
import glob, os, sys, time
import serial

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
    names = sys.argv[2:] or sorted(os.path.basename(p) for p in glob.glob(os.path.join(ROOT, 'build', 'PIPART', '*.PIP')))
    link = serial.Serial(port, 115200, timeout=3)
    link.dtr = False; time.sleep(0.1); link.dtr = True
    ready = link.readline().strip()
    if ready != b'READY':
        sys.exit(f'sdloader did not start: {ready!r}')
    failures = 0
    for name in names:
        data = open(os.path.join(ROOT, 'build', 'PIPART', name), 'rb').read()
        link.write(f'W {name} {len(data)}\n'.encode())
        if link.readline().strip() != b'GO':
            sys.exit(f'{name}: could not open on the card')
        for i in range(0, len(data), 64):
            link.write(data[i:i + 64])
            if link.read(1) != b'k':
                sys.exit(f'{name}: write stalled')
        link.readline()
        link.write(f'R {name}\n'.encode())
        size = int(link.readline().split()[1])
        same = link.read(size) == data
        failures += not same
        print(f'{name:10} {len(data):5} bytes  {"OK" if same else "MISMATCH"}')
    sys.exit(1 if failures else 0)

if __name__ == '__main__':
    main()

"""Talk to the Pip-Boy firmware over serial: screenshots, injected taps, raw touch."""
import sys, time, struct, serial
from PIL import Image

def connect(reset=False):
    port = serial.Serial('/dev/ttyACM0', 1000000, timeout=2)
    if not reset:
        # Opening the port pulses DTR and resets the Uno unless hupcl is off. Callers that need
        # the running state set reset=False and accept a reboot on first open.
        pass
    return port

def save565(data, path):
    rgb = bytearray()
    for i in range(0, len(data), 2):
        v = data[i] << 8 | data[i + 1]
        rgb += bytes(((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3))
    Image.frombytes('RGB', (480, 320), bytes(rgb)).save(path)

def screenshot(port, path):
    port.reset_input_buffer()
    port.write(b'S')
    line = port.readline()
    if line != b'SHOT\n':
        raise RuntimeError(f'unexpected reply {line!r}')
    total = 480 * 320 * 2
    data = bytearray()
    deadline = time.time() + 90
    while len(data) < total and time.time() < deadline:
        port.write(b'k')
        want = min(60, total - len(data))
        chunk = port.read(want)
        while len(chunk) < want and time.time() < deadline:
            chunk += port.read(want - len(chunk))
        data += chunk
    if len(data) < total:
        raise RuntimeError(f'short read {len(data)}')
    save565(data, path)
    return data

def tap(port, x, y, command=b'T'):
    port.reset_input_buffer()
    port.write(command + struct.pack('<hh', x, y))
    return port.readline().decode().strip()

def simple(port, letter):
    port.reset_input_buffer()
    port.write(letter.encode())
    return port.readline().decode().strip()

if __name__ == '__main__':
    port = connect()
    command = sys.argv[1]
    if command == 'shot':
        screenshot(port, sys.argv[2])
    elif command in ('tap', 'hold'):
        print(tap(port, int(sys.argv[2]), int(sys.argv[3]), b'T' if command == 'tap' else b'H'))
    else:
        print(simple(port, command))

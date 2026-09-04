# CircuitPythonOscuino -- Oscuino over SLIP-encoded USB serial, in CircuitPython,
# for CircuitPython (Adafruit boards).
#
# GENERATED FILE -- do not edit directly.
# Source: extras/webserial/template-circuitpython.py + extras/webserial/boards.json
# Regenerate:  cd extras/webserial && make generate
#
# Runs code.py under CircuitPython, not an Arduino sketch -- see extras/python/. One example covers every Adafruit CircuitPython board: pins resolve by name at runtime, so /d/13 is board.D13 and /a/0 is board.A0, and the port filter matches on Adafruit's vendor id alone. boot.py must be copied along with code.py -- it adds the second CDC port (the data channel) that SLIP runs on; pick that SECOND port in the browser chooser. Analog reads are 16-bit, 0..65535.
#
# Copy this file to the CIRCUITPY drive as code.py, and boot.py (next to this
# file) alongside it, then press reset: boot.py runs at boot and adds a second
# USB serial port -- the "data" channel -- which is the one this program
# speaks SLIP on. Binary SLIP cannot share the console channel, where ctrl-C
# (0x03, a legal SLIP byte) would kill the program. Open CircuitPythonOscuino.html
# -- served over http://localhost or https://, never file:// -- click Connect,
# and pick the board's SECOND serial port. Same wire contract as the Arduino
# sketches: OSC 1.0 bundles inside RFC 1055 SLIP frames, so the CNMAT Max
# patches and test/hardware/oscprobe.py talk to it unchanged.
#
# Addresses: the standard Oscuino set. /d/<n> is board.D<n>, /a/<n> is
# board.A<n> (16-bit reads, 0..65535 -- CircuitPython normalises the ADC).
# /tone/<n> plays on D<n> via PWM; the optional duration argument is ignored,
# send 0 to stop. /s/l drives board.LED. /s/q exits to the console.

import struct

# ---- SLIP (RFC 1055), same constants as SLIPEncodedSerial.h ----------------
END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD


def slip_encode(payload):
    # Leading AND trailing END, like CNMAT beginPacket()/endPacket(). Every
    # decoder on the other side skips the empty frame the leading END closes.
    out = bytearray([END])
    for b in payload:
        if b == END:
            out += b'\xdb\xdc'
        elif b == ESC:
            out += b'\xdb\xdd'
        else:
            out.append(b)
    out.append(END)
    return bytes(out)


class SlipDecoder:
    # Stateful across feed() calls: a frame usually spans several reads.
    def __init__(self):
        self.buf = bytearray()
        self.esc = False

    def feed(self, data):
        frames = []
        for b in data:
            if self.esc:
                self.buf.append(END if b == ESC_END else ESC if b == ESC_ESC else b)
                self.esc = False
            elif b == ESC:
                self.esc = True
            elif b == END:
                if self.buf:
                    frames.append(bytes(self.buf))
                    self.buf = bytearray()
            else:
                self.buf.append(b)
        return frames


# ---- OSC 1.0, the subset the browser page speaks ---------------------------
def _pad(b):
    return b + b'\0' * ((4 - len(b) % 4) % 4)


def _s(s):
    # bytes(s, 'utf8') rather than s.encode(): the micro:bit V1's MicroPython
    # has no str.encode, and the codecs stay textually identical across ports.
    return _pad(bytes(s, 'utf8') + b'\0')


def _i32(v):
    # Two's-complement fold so a large tick count packs instead of raising.
    return struct.pack('>i', ((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000)


def message(addr, *args):
    tags, data = ',', b''
    for a in args:
        if a is True:
            tags += 'T'
        elif a is False:
            tags += 'F'
        elif a is None:
            tags += 'N'
        elif isinstance(a, int):
            tags += 'i'
            data += _i32(a)
        elif isinstance(a, float):
            tags += 'f'
            data += struct.pack('>f', a)
        elif isinstance(a, str):
            tags += 's'
            data += _s(a)
        else:
            tags += 'b'
            data += _i32(len(a)) + _pad(bytes(a))
    return _s(addr) + _s(tags) + data


def bundle(elems):
    out = b'#bundle\0' + b'\0\0\0\0\0\0\0\x01'
    for e in elems:
        out += _i32(len(e)) + e
    return out


def _dstr(p, i):
    j = p.index(b'\0', i)
    n = j - i + 1
    # str(b, 'utf8') rather than b.decode(): absent on the micro:bit V1.
    return str(p[i:j], 'utf8'), i + n + ((4 - n % 4) % 4)


# Flatten a packet -- message or bundle, nested included -- to a list of
# (address, args). Raises on malformed input; the caller drops the frame.
def messages(p, out=None):
    if out is None:
        out = []
    if p[:8] == b'#bundle\0':
        i = 16
        while i + 4 <= len(p):
            n = struct.unpack('>i', p[i:i + 4])[0]
            if n <= 0 or i + 4 + n > len(p):
                break
            messages(p[i + 4:i + 4 + n], out)
            i += 4 + n
        return out
    addr, i = _dstr(p, 0)
    tags, i = _dstr(p, i)
    args = []
    for t in tags[1:]:
        if t == 'i':
            args.append(struct.unpack('>i', p[i:i + 4])[0])
            i += 4
        elif t == 'f':
            args.append(struct.unpack('>f', p[i:i + 4])[0])
            i += 4
        elif t == 's':
            a, i = _dstr(p, i)
            args.append(a)
        elif t == 'b':
            n = struct.unpack('>i', p[i:i + 4])[0]
            args.append(p[i + 4:i + 4 + n])
            i += 4 + ((n + 3) & ~3)
        elif t in 'TFNI':
            args.append({'T': True, 'F': False}.get(t))
        elif t in 'hdt':
            i += 8          # known width, value unused here
        else:
            break           # unknown width: stop rather than misparse
    out.append((addr, args))
    return out


# ---- CircuitPython I/O -----------------------------------------------------
def run():
    import time, board, digitalio, analogio, pwmio, usb_cdc

    ser = usb_cdc.data
    if ser is None:
        # boot.py absent or data channel disabled. The console still moves
        # SLIP bytes -- until one of them is 0x03 -- so limp along and say so.
        ser = usb_cdc.console
    ser.timeout = 0

    # Index -> board pin, with gaps kept so /d/13 is always board.D13.
    dpins, apins = [], []
    for i in range(64):
        dpins.append(getattr(board, 'D%d' % i, None))
    while dpins and dpins[-1] is None:
        dpins.pop()
    for i in range(32):
        p = getattr(board, 'A%d' % i, None)
        if p is None:
            break
        apins.append(p)
    led = getattr(board, 'LED', None)

    # One live object per pin, whatever its current mode. Switching modes on a
    # pin means deinit first or CircuitPython raises "in use". vfreq remembers
    # which PWMOuts were built variable-frequency (for /tone) -- the object
    # itself does not reliably expose that across CircuitPython versions.
    claimed = {}
    vfreq = {}

    def release(p):
        vfreq.pop(p, None)
        o = claimed.pop(p, None)
        if o:
            o.deinit()

    def digital(p):
        o = claimed.get(p)
        if not isinstance(o, digitalio.DigitalInOut):
            release(p)
            o = digitalio.DigitalInOut(p)
            claimed[p] = o
        return o

    def write_pin(p, a0):
        if isinstance(a0, bool) or isinstance(a0, int):
            o = digital(p)
            o.switch_to_output(bool(a0))
        else:                               # float: PWM, 0.0 .. 1.0
            v = 0.0 if a0 < 0.0 else 1.0 if a0 > 1.0 else a0
            o = claimed.get(p)
            if not isinstance(o, pwmio.PWMOut) or vfreq.get(p):
                release(p)
                o = pwmio.PWMOut(p)
                claimed[p] = o
            o.duty_cycle = int(v * 65535)

    def micros():
        try:
            return time.monotonic_ns() // 1000 & 0x7FFFFFFF
        except AttributeError:
            return int(time.monotonic() * 1e6) & 0x7FFFFFFF

    def _num(s):
        try:
            return int(s)
        except ValueError:
            return -1

    def handle(addr, args, out):
        a0 = args[0] if args else None
        if addr == '/s/m':
            out.append(message('/s/m', micros()))
        elif addr == '/s/d':
            out.append(message('/s/d', len(dpins)))
        elif addr == '/s/a':
            out.append(message('/s/a', len(apins)))
        elif addr == '/s/l' and isinstance(a0, int) and led:
            o = digital(led)
            o.switch_to_output(a0 > 0)
            out.append(message('/s/l', a0))
        elif addr == '/s/q':
            raise SystemExit
        elif addr == '/enq':
            out.append(message('/enq', 'CircuitPythonOscuino'))
        elif addr.startswith('/tone/'):
            n = _num(addr[6:])
            p = dpins[n] if 0 <= n < len(dpins) else None
            f = int(a0) if isinstance(a0, (int, float)) else 0
            if p is None:
                return
            if f <= 0:
                release(p)
            else:
                o = claimed.get(p)
                if not isinstance(o, pwmio.PWMOut) or not vfreq.get(p):
                    release(p)
                    o = pwmio.PWMOut(p, variable_frequency=True)
                    claimed[p] = o
                    vfreq[p] = True
                o.frequency = f
                o.duty_cycle = 0x8000
        elif addr.startswith('/d/') or addr.startswith('/a/'):
            part = addr.split('/')          # ['', 'd', '13'] or ['', 'd', '13', 'u']
            n = _num(part[2])
            pins = dpins if part[1] == 'd' else apins
            p = pins[n] if 0 <= n < len(pins) else None
            if p is None:
                return
            if len(part) == 4 and part[3] == 'u':
                o = digital(p)
                o.switch_to_input(digitalio.Pull.UP)
                out.append(message(addr, 1 if o.value else 0))
            elif isinstance(a0, (bool, int, float)):
                write_pin(p, a0)
            elif part[1] == 'a':
                o = claimed.get(p)
                if not isinstance(o, analogio.AnalogIn):
                    release(p)
                    o = analogio.AnalogIn(p)
                    claimed[p] = o
                out.append(message(addr, o.value))
            else:
                o = digital(p)
                o.switch_to_input()
                out.append(message(addr, 1 if o.value else 0))

    dec = SlipDecoder()
    ser.write(slip_encode(bundle([message('/enq', 'CircuitPythonOscuino')])))
    while True:
        n = ser.in_waiting
        if n:
            for frame in dec.feed(ser.read(n)):
                out = []
                try:
                    for addr, args in messages(frame):
                        handle(addr, args, out)
                except Exception:
                    pass                    # malformed frame: drop, keep serving
                if out:
                    ser.write(slip_encode(bundle(out)))
        else:
            time.sleep(0.001)


if __name__ == '__main__':
    run()

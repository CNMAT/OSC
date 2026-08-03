"""Hardware probe: talk SLIP-framed OSC to a board over USB CDC.

Standard library only -- opens the tty directly and puts it in raw mode with
termios, so nothing has to be installed on the host.
"""
import os, sys, time, termios, struct, select

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem14501'

# Which analog channel to read in probe F. Not every variant defines A0 --
# the M5Stack NanoC6, for one, starts at A1 -- so /a/0 does not resolve
# everywhere and cannot be hardcoded.
#     python3 oscprobe.py /dev/cu.usbmodemXXXX /a/1
ANALOG = sys.argv[2] if len(sys.argv) > 2 else '/a/0'

END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD


def slip_encode(payload):
    out = bytearray([END])
    for b in payload:
        if b == END:
            out += bytes([ESC, ESC_END])
        elif b == ESC:
            out += bytes([ESC, ESC_ESC])
        else:
            out.append(b)
    out.append(END)
    return bytes(out)


def slip_frames(buf):
    """Split a raw byte stream into decoded SLIP frames."""
    frames, cur, esc = [], bytearray(), False
    for b in buf:
        if esc:
            cur.append(END if b == ESC_END else ESC if b == ESC_ESC else b)
            esc = False
        elif b == ESC:
            esc = True
        elif b == END:
            if cur:
                frames.append(bytes(cur))
                cur = bytearray()
        else:
            cur.append(b)
    return frames


def pad(b):
    return b + b'\0' * ((4 - len(b) % 4) % 4)


def ostr(s):
    return pad(s.encode() + b'\0')


def msg(addr, args=()):
    tags, data = ',', b''
    for a in args:
        if isinstance(a, int):
            tags += 'i'; data += struct.pack('>i', a)
        elif isinstance(a, float):
            tags += 'f'; data += struct.pack('>f', a)
        elif isinstance(a, str):
            tags += 's'; data += ostr(a)
        elif a is Impulse:
            tags += 'I'
        elif a is Null:
            tags += 'N'
        elif isinstance(a, (bytes, bytearray)):
            tags += 'b'; data += struct.pack('>i', len(a)) + pad(bytes(a))
    return ostr(addr) + ostr(tags) + data


class Impulse: pass
class Null: pass


def bundle(elems, timetag=b'\0' * 7 + b'\x01'):
    out = b'#bundle\0' + timetag
    for e in elems:
        out += struct.pack('>i', len(e)) + e
    return out


# ---- minimal OSC decoder for the replies -----------------------------------
def dec_str(b, i):
    j = b.index(b'\0', i)
    s = b[i:j].decode('ascii', 'replace')
    n = j - i + 1
    return s, i + n + ((4 - n % 4) % 4)


def decode(p):
    if p.startswith(b'#bundle\0'):
        i, out = 16, []
        while i < len(p):
            n = struct.unpack('>i', p[i:i + 4])[0]
            out.append(decode(p[i + 4:i + 4 + n])); i += 4 + n
        return ('bundle', out)
    addr, i = dec_str(p, 0)
    tags, i = dec_str(p, i)
    args = []
    for t in tags[1:]:
        if t == 'i':
            args.append(struct.unpack('>i', p[i:i + 4])[0]); i += 4
        elif t == 'f':
            args.append(round(struct.unpack('>f', p[i:i + 4])[0], 4)); i += 4
        elif t == 's':
            s, i = dec_str(p, i); args.append(s)
        elif t in 'TFIN':
            args.append(t)
    return (addr, args)


class Port:
    # Must match the sketches' SLIPSerial.begin(). A native USB CDC port
    # ignores the line rate entirely, which is why this went years without
    # being set at all -- every board tested was native USB. The UNO R4 WiFi
    # is not: it builds with -DNO_USB, so its Serial is a real UART that the
    # on-board ESP32-S3 bridges to the host, and the two ends must agree.
    # Leaving it unset there gets the macOS default, and the reply arrives as
    # framing noise (0xEF 0xED 0xEC...) rather than as no reply at all.
    BAUD = 115200

    def __init__(self, path, baud=BAUD):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0                       # iflag oflag lflag: raw
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[4] = a[5] = getattr(termios, 'B%d' % baud) # ispeed ospeed
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.drain(0.4)

    def write(self, b):
        os.write(self.fd, b)

    def write_slowly(self, b, gap):
        for byte in b:
            os.write(self.fd, bytes([byte]))
            time.sleep(gap)

    def drain(self, t=0.3):
        got = b''
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    got += os.read(self.fd, 4096)
                except OSError:
                    pass
        return got

    def close(self):
        os.close(self.fd)


def show(name, raw, expect_frames=1):
    frames = slip_frames(raw)
    ok = len(frames) >= expect_frames
    print(f"  {name:<44} {'ok  ' if ok else 'FAIL'} frames={len(frames)}"
          f" (want>={expect_frames})")
    for f in frames[:4]:
        try:
            print(f"        {decode(f)}")
        except Exception as e:
            print(f"        <undecodable {f[:24].hex()}> {e}")
    return ok


def main():
    fails = 0
    p = Port(PORT)
    print(f"port {PORT}\n")

    print("A. baseline: /s/m in one write")
    if not show("single frame, one write", (p.write(slip_encode(msg('/s/m'))), p.drain(0.6))[1]):
        fails += 1

    print("\nB. pollOSC: same packet, one byte at a time, 6ms apart")
    print("   (spans many loop() iterations - the case that used to be dropped)")
    pkt = slip_encode(msg('/s/m'))
    p.write_slowly(pkt, 0.006)
    if not show("byte-at-a-time delivery", p.drain(0.8)):
        fails += 1

    print("\nC. bundle carrying an impulse argument alongside a routed message")
    print("   (pre-fix the whole bundle was rejected as INVALID_OSC)")
    b = bundle([msg('/x', [Impulse]), msg('/s/m')])
    p.write(slip_encode(b))
    if not show("bundle with 'I' still routes /s/m", p.drain(0.8)):
        fails += 1

    print("\nD. two frames back to back in a single write")
    p.write(slip_encode(msg('/s/m')) + slip_encode(msg('/s/d')))
    if not show("two frames, one write", p.drain(1.0), expect_frames=2):
        fails += 1

    print("\nE. reported pin counts (Gemma routes only 3 pads)")
    p.write(slip_encode(msg('/s/d')))
    show("/s/d digital pin count", p.drain(0.6))
    p.write(slip_encode(msg('/s/a')))
    show("/s/a analog pin count", p.drain(0.6))

    print("\nF. analog read")
    p.write(slip_encode(msg(ANALOG)))
    if not show(ANALOG, p.drain(0.6)):
        fails += 1

    p.close()
    print(f"\n{'FAILURES' if fails else 'all hardware probes passed'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())

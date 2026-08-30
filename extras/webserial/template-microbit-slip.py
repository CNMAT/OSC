# slip.py -- SLIP layer (RFC 1055), imported by main.py.
# One of the four files the micro:bit deployment copies across. The split and
# the strip are both dictated by measurement on a V1 (16 KB heap, on-device
# compiler): parsing costs ~2.1x source bytes, the boot file gets the largest
# budget, and runtime imports get what is left -- so main.py carries the bulk
# and the codec arrives as three ~1 KB modules. See extras/python/README.md.
#
# GENERATED FILE -- do not edit directly.
# Source: extras/webserial/template-microbit-slip.py
# Regenerate:  cd extras/webserial && make generate

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

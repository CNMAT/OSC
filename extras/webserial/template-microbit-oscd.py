# oscd.py -- OSC 1.0 decoder, imported by main.py.
# One of the four files the micro:bit deployment copies across; see slip.py's
# header or extras/python/README.md for why the codec ships in small pieces.
#
# GENERATED FILE -- do not edit directly.
# Source: extras/webserial/template-microbit-oscd.py
# Regenerate:  cd extras/webserial && make generate

import struct


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

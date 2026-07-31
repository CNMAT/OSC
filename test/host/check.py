#!/usr/bin/env python3
"""Independent OSC 1.0 decoder (stdlib only) used to check the bytes the
CNMAT library puts on the wire.  Written from the OSC 1.0 spec, deliberately
sharing no code with the library under test.

Reads the `<name> <hex>` lines emitted by ./emit on stdin, decodes each one,
and asserts the decoded values against the expected ones.
"""

import struct
import sys

# ---------------------------------------------------------------- decoder ---


class Reader:
    def __init__(self, data):
        self.d = data
        self.i = 0

    def take(self, n):
        if self.i + n > len(self.d):
            raise ValueError("truncated: want %d at %d of %d" % (n, self.i, len(self.d)))
        out = self.d[self.i:self.i + n]
        self.i += n
        return out

    def string(self):
        """OSC-string: NUL-terminated, padded to a multiple of 4."""
        end = self.d.index(b"\x00", self.i)
        s = self.d[self.i:end].decode("ascii")
        self.i = (end + 4) & ~3          # step past the NUL, round up to 4
        return s

    def blob(self):
        (n,) = struct.unpack(">i", self.take(4))
        b = self.take(n)
        self.i = (self.i + 3) & ~3
        return b

    def done(self):
        return self.i >= len(self.d)


def decode_message(data):
    r = Reader(data)
    addr = r.string()
    tags = r.string()
    if not tags.startswith(","):
        raise ValueError("type tag string must start with ',': %r" % tags)
    args = []
    for t in tags[1:]:
        if t == "i":
            args.append(("i", struct.unpack(">i", r.take(4))[0]))
        elif t == "f":
            args.append(("f", struct.unpack(">f", r.take(4))[0]))
        elif t == "s":
            args.append(("s", r.string()))
        elif t == "b":
            args.append(("b", r.blob()))
        elif t == "h":
            args.append(("h", struct.unpack(">q", r.take(8))[0]))
        elif t == "d":
            args.append(("d", struct.unpack(">d", r.take(8))[0]))
        elif t == "t":
            args.append(("t", struct.unpack(">Q", r.take(8))[0]))
        elif t == "r":
            # OSC 1.0: 32-bit RGBA colour, bytes in r, g, b, a order
            args.append(("r", tuple(r.take(4))))
        elif t == "m":
            # OSC 1.0: 4-byte MIDI message, port id / status / data1 / data2
            args.append(("m", tuple(r.take(4))))
        elif t in "TFNI":
            args.append((t, None))       # no payload
        else:
            raise ValueError("unknown type tag %r" % t)
    if not r.done():
        raise ValueError("%d trailing bytes after arguments" % (len(data) - r.i))
    return addr, tags, args


def decode_bundle(data):
    r = Reader(data)
    hdr = r.string()
    if hdr != "#bundle":
        raise ValueError("not a bundle: %r" % hdr)
    (timetag,) = struct.unpack(">Q", r.take(8))
    elements = []
    while not r.done():
        (n,) = struct.unpack(">i", r.take(4))
        elements.append(decode_message(r.take(n)))
    return timetag, elements


# ---------------------------------------------------------------- expected ---

FAIL = []
PASS = []


def check(name, got, want):
    if got == want:
        PASS.append(name)
    else:
        FAIL.append("%s\n     got:  %s\n     want: %s" % (name, got, want))


def main():
    cases = {}     # name -> raw bytes,      from "<name> <hex>" lines
    fields = {}    # name -> {key: value},   from "<name> k=v k=v" lines
    lines = []
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        lines.append(line)
        if line.startswith("#"):
            continue
        name, _, rest = line.partition(" ")
        if "=" in rest:
            fields[name] = dict(kv.split("=", 1) for kv in rest.split())
        elif rest and all(c in "0123456789abcdef" for c in rest):
            cases[name] = bytes.fromhex(rest)

    for line in lines:
        print("  " + line)
    print()

    # -- 'r' -----------------------------------------------------------------
    addr, tags, args = decode_message(cases["encode.rgba"])
    check("encode.rgba", (addr, tags, args),
          ("/rgba", ",r", [("r", (0x11, 0x22, 0x33, 0x44))]))

    # -- 'm' -----------------------------------------------------------------
    # {port=1, status=0x90, channel=2, data1=0x3C, data2=0x40}
    # channel folds into the status byte low nibble -> 0x92
    addr, tags, args = decode_message(cases["encode.midi"])
    check("encode.midi", (addr, tags, args),
          ("/midi", ",m", [("m", (0x01, 0x92, 0x3C, 0x40))]))

    # channel already in the status byte, channel field 0 -> unchanged
    addr, tags, args = decode_message(cases["encode.midi_status_only"])
    check("encode.midi_status_only", (addr, tags, args),
          ("/midi", ",m", [("m", (0x01, 0x92, 0x3C, 0x40))]))

    # system-common status 0xF2 (song position): low nibble is not a channel
    addr, tags, args = decode_message(cases["encode.midi_syscommon"])
    check("encode.midi_syscommon", (addr, tags, args),
          ("/midi", ",m", [("m", (0x00, 0xF2, 0x2A, 0x01))]))

    # -- regression guard: scalars unchanged ---------------------------------
    addr, tags, args = decode_message(cases["encode.mixed"])
    check("encode.mixed", (addr, tags, args),
          ("/mixed", ",ifsT", [("i", 0x01020304), ("f", 1.0), ("s", "hi"), ("T", None)]))

    # -- bundle timetag ------------------------------------------------------
    timetag, elements = decode_bundle(cases["encode.bundle_default_timetag"])
    check("encode.bundle_default_timetag.timetag", timetag, 1)   # 1 == "immediately"
    check("encode.bundle_default_timetag.elements", elements,
          [("/a", ",i", [("i", 1)])])

    # -- round trips: re-encoding a decoded value must be byte-identical -----
    check("roundtrip.midi", cases["roundtrip.midi"].hex(),
          cases["encode.midi"].hex())
    check("roundtrip.midi_syscommon", cases["roundtrip.midi_syscommon"].hex(),
          cases["encode.midi_syscommon"].hex())

    # a received bundle must re-send byte-identically, timetag included
    check("roundtrip.bundle_timetag", cases["roundtrip.bundle_timetag"].hex(),
          cases["input.bundle_timetag"].hex())
    timetag, elements = decode_bundle(cases["roundtrip.bundle_timetag"])
    check("roundtrip.bundle_timetag.timetag", timetag, 0x83AA7E8040000000)
    check("roundtrip.bundle_timetag.elements", elements,
          [("/a", ",i", [("i", 1)])])

    # -- decoded values as the sketch sees them ------------------------------
    check("decode.rgba", fields["decode.rgba"],
          {"r": "11", "g": "22", "b": "33", "a": "44", "err": "0"})
    check("decode.midi", fields["decode.midi"],
          {"port": "01", "status": "92", "channel": "02",
           "data1": "3c", "data2": "40", "err": "0"})
    check("decode.midi_syscommon", fields["decode.midi_syscommon"],
          {"port": "00", "status": "f2", "channel": "00",
           "data1": "2a", "data2": "01", "err": "0"})
    check("roundtrip.rgba", fields["roundtrip.rgba"],
          {"r": "de", "g": "ad", "b": "be", "a": "ef", "err": "0"})
    check("decode.bundle_timetag", fields["decode.bundle_timetag"],
          {"seconds": "83aa7e80", "fraction": "40000000", "err": "0"})
    check("default.bundle_timetag", fields["default.bundle_timetag"],
          {"seconds": "00000000", "fraction": "00000001"})

    for name in PASS:
        print("  PASS %s" % name)
    for f in FAIL:
        print("  FAIL %s" % f)
    print("\n%d passed, %d failed" % (len(PASS), len(FAIL)))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

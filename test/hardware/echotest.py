"""Byte-exact OSC echo test against real hardware running OscEcho.ino.

Sends a packet, expects the identical bytes back. The board decodes with the
library and re-encodes with the library, so a byte-identical reply means the
decoder and encoder agree on the wire format on-target.
"""
import sys, time, struct
sys.path.insert(0, '.')
from oscprobe import (Port, slip_encode, slip_frames, msg, bundle, ostr, pad,
                      Impulse, Null, decode)

PORT = sys.argv[1]
p = Port(PORT)
p.drain(0.4)

CASES = [
    ("int32 limits",      msg('/i', [-2147483648, 2147483647])),
    ("int mixed",         msg('/i', [0, 1, -1, 123456])),
    ("float",             msg('/f', [1.5, -0.5])),
    ("string len0",       msg('/s', [""])),
    ("string len1..4",    msg('/s', ["a", "ab", "abc", "abcd"])),
    ("empty str then int", msg('/s', ["abc", "", 7])),
    ("blob len0",         msg('/b', [b''])),
    ("blob len1",         msg('/b', [bytes([0xA0])])),
    ("blob len4",         msg('/b', [bytes([0xA0, 0xA1, 0xA2, 0xA3])])),
    ("blob len5",         msg('/b', [bytes([0xA0, 0xA1, 0xA2, 0xA3, 0xA4])])),
    ("impulse",           msg('/e', [Impulse])),
    ("null",              msg('/e', [Null])),
    ("i I s N f",         msg('/e', [7, Impulse, "hi", Null, 2.5])),
    ("blob0 then float",  msg('/e', [b'', 2.5])),
    ("no args",           msg('/none')),
    ("addr residue 1",    msg('/a')),
    ("addr residue 2",    msg('/ab')),
    ("addr residue 3",    msg('/abc')),
    ("addr residue 0",    msg('/abcd')),
    ("bundle 2 msgs",     bundle([msg('/x', [1]), msg('/y', [2.5])])),
    ("bundle w/ impulse", bundle([msg('/x', [Impulse]), msg('/y', [3])])),
    ("bundle timetag",    bundle([msg('/x', [1])],
                                 timetag=bytes([0x83, 0xAA, 0x7E, 0x80,
                                                0x40, 0x00, 0x00, 0x00]))),
]

fails = 0
for name, packet in CASES:
    p.drain(0.05)
    p.write(slip_encode(packet))
    raw = p.drain(0.35)
    frames = slip_frames(raw)
    if not frames:
        print(f"  {name:<22} FAIL  no reply")
        fails += 1
        continue
    got = frames[0]
    if got == packet:
        print(f"  {name:<22} ok    {len(got)} bytes identical")
    else:
        print(f"  {name:<22} FAIL  sent {packet.hex()}")
        print(f"  {'':<22}       got  {got.hex()}")
        fails += 1

p.close()
print(f"\n{len(CASES)} cases, {fails} mismatches")
sys.exit(1 if fails else 0)

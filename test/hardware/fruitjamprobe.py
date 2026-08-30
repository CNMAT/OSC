"""Fruit Jam extension probes, on top of oscprobe's port and codec.

Covers what the generic suite does not: /fj/b buttons, /fj/led NeoPixels
(value echo — the color itself is for human eyes), /fj/beep through the
TLV320 codec, /fj/t on the DVI display, and a literal 0x03 payload byte.
Runtime-agnostic: the CircuitPython code.py and the Arduino sketch answer
the same addresses. Pass --repl to also test CircuitPython's /s/q escape
(the Arduino firmware has no REPL to exit to).

    python3 fruitjamprobe.py /dev/cu.usbmodemXXXX [--repl]
"""
import sys, time

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oscprobe
from oscprobe import msg, bundle, slip_encode, slip_frames, decode

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem14501'
TEST_REPL = '--repl' in sys.argv

fails = 0


def ask(p, payload, label, want=None, want_reply=True, settle=0.6):
    global fails
    p.write(slip_encode(payload))
    raw = p.drain(settle)
    got = []
    for f in slip_frames(raw):
        try:
            got.append(decode(f))
        except Exception as e:
            got.append(('undecodable', str(e)))
    okc = bool(got) if want_reply else not got
    if okc and want is not None:
        okc = any(want == m for g in got if g[0] == 'bundle' for m in g[1])
    print('  %-36s %s %r' % (label, 'ok  ' if okc else 'FAIL', got[:3]))
    if not okc:
        fails += 1
    return got


p = oscprobe.Port(PORT)
print('port %s' % PORT)

ask(p, bundle([msg('/fj/b')]), '/fj/b buttons (3 ints)')
ask(p, bundle([msg('/fj/led', [255, 0, 0])]), '/fj/led red echo',
    want=('/fj/led', [255, 0, 0]))
time.sleep(0.4)
ask(p, bundle([msg('/fj/led', [1, 0, 128, 96])]), '/fj/led pixel 1 teal echo',
    want=('/fj/led', [1, 0, 128, 96]))
time.sleep(0.4)
ask(p, bundle([msg('/fj/led', [0, 0, 0])]), '/fj/led off echo',
    want=('/fj/led', [0, 0, 0]))
got = ask(p, bundle([msg('/fj/beep', [880, 400])]), '/fj/beep 880 (listen!)')
if any(m == ('/fj/beep', [-1]) for g in got if g[0] == 'bundle' for m in g[1]):
    print('       NOTE: firmware reports audio unavailable (codec driver missing)')
time.sleep(0.6)
ask(p, bundle([msg('/fj/beep', [0])]), '/fj/beep stop')
ask(p, bundle([msg('/fj/t', ['hello from OSC'])]), '/fj/t DVI text (watch screen)')
# 0x03 inside a frame: int arg 3 — the byte a REPL would read as ctrl-C.
ask(p, bundle([msg('/fj/led', [3, 3, 3, 3])]), '0x03 bytes survive in-frame',
    want=('/fj/led', [3, 3, 3, 3]))
ask(p, bundle([msg('/s/m'), msg('/s/d'), msg('/s/a')]), 'system triplet')
ask(p, bundle([msg('/a/0')]), '/a/0 analog read')

if TEST_REPL:
    # The REPL itself appears on the CONSOLE channel, not this data channel —
    # what is observable here is that the serve loop stopped answering. To
    # resume serving afterwards, send ctrl-D on the console port (reload).
    p.write(slip_encode(bundle([msg('/s/q')])))
    time.sleep(0.8)
    p.write(slip_encode(bundle([msg('/s/m')])))
    raw = p.drain(1.0)
    okq = not slip_frames(raw)
    print('  %-36s %s' % ('/s/q stops the serve loop', 'ok  ' if okq else 'FAIL'))
    if not okq:
        fails += 1

p.close()
print('\n%s (%d failed)' % ('FAILURES' if fails else 'all Fruit Jam probes passed', fails))
sys.exit(1 if fails else 0)

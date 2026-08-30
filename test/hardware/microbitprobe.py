"""micro:bit extension probes, on top of oscprobe's port and codec.

Covers what the generic hardware suite does not: /mb/a /mb/b /mb/t, /s/l,
/d/5 (button A pin), /tone/0, and finally /s/q -- which must land us back at
a REPL prompt, proving the escape hatch.
"""
import os, sys, time

REPO = '/Users/adrianfreed/Documents/Arduino/libraries/OSC'
sys.path.insert(0, os.path.join(REPO, 'test', 'hardware'))
import oscprobe
from oscprobe import msg, bundle, slip_encode, slip_frames, decode

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem14102'

fails = 0


def ask(p, payload, label, want_reply=True, settle=0.6):
    global fails
    p.write(slip_encode(payload))
    raw = p.drain(settle)
    frames = slip_frames(raw)
    got = []
    for f in frames:
        try:
            got.append(decode(f))
        except Exception as e:
            got.append(('undecodable', str(e)))
    okc = bool(frames) if want_reply else not frames
    print('  %-34s %s %r' % (label, 'ok  ' if okc else 'FAIL', got[:3]))
    if not okc:
        fails += 1
    return got


p = oscprobe.Port(PORT)
print('port %s' % PORT)

ask(p, bundle([msg('/mb/b')]), '/mb/b buttons')
ask(p, bundle([msg('/mb/a')]), '/mb/a accelerometer')
ask(p, bundle([msg('/s/l', [1])]), '/s/l 1 display on')
time.sleep(0.5)
ask(p, bundle([msg('/s/l', [0])]), '/s/l 0 display off')
ask(p, bundle([msg('/d/5', [])]), '/d/5 button A pin read')
ask(p, bundle([msg('/mb/t', ['OSC'])]), '/mb/t scroll (no reply)', want_reply=False)
time.sleep(2.5)
ask(p, bundle([msg('/tone/0', [440])]), '/tone/0 440 (no reply)', want_reply=False)
time.sleep(0.4)
ask(p, bundle([msg('/tone/0', [0])]), '/tone/0 stop (no reply)', want_reply=False)
# the int arg 3 puts a raw 0x03 on the wire: kbd_intr proof
ask(p, bundle([msg('/s/l', [3])]), '/s/l 3 (0x03 byte survives)')
ask(p, bundle([msg('/s/d'), msg('/s/a')]), 'pin counts')

# /s/q: program exits; ctrl-C then a newline must produce a REPL prompt
p.write(slip_encode(bundle([msg('/s/q')])))
time.sleep(0.6)
p.write(b'\r')
raw = p.drain(1.0)
okq = b'>>>' in raw
print('  %-34s %s %r' % ('/s/q exits to REPL', 'ok  ' if okq else 'FAIL', raw[-60:]))
if not okq:
    fails += 1

p.close()
print('\n%s (%d failed)' % ('FAILURES' if fails else 'all micro:bit probes passed', fails))
sys.exit(1 if fails else 0)

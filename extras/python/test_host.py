"""Host-side proof for the python firmwares, standard library only.

Imports the GENERATED files that actually ship -- MicrobitOscuino/main.py and
CircuitPythonOscuino/code.py -- and cross-checks their codecs against the two
independent references this repo already trusts:

  test/hardware/oscprobe.py   the probe that talks to real boards
  test/host/oracle.py         an OSC 1.0 decoder written from the spec, which
                              rejects bad padding and trailing bytes

Importing the files is also the syntax check: their hardware imports live
inside run(), which only fires when __name__ == '__main__', so the codec half
runs under any CPython. The two templates carry deliberately identical codecs;
the cross-firmware pass here is what keeps that duplication honest, the same
job check.mjs does for the N copies of the browser codec.

Run from anywhere:  python3 extras/python/test_host.py
"""
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'test', 'hardware'))
sys.path.insert(0, os.path.join(ROOT, 'test', 'host'))

import oscprobe
import oracle


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, rel))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


# The micro:bit deployment is split across four files sized for the V1's
# on-device compiler (see the template headers): main.py plus the slip/osce/
# oscd modules. Load the modules under their deployed names so main.py's
# imports resolve; importing main.py is also this suite's proof that it
# compiles. `mb` presents the combined codec, like the other targets.
slip_mod = load('slip', 'MicrobitOscuino/slip.py')
osce_mod = load('osce', 'MicrobitOscuino/osce.py')
oscd_mod = load('oscd', 'MicrobitOscuino/oscd.py')
load('mb_main', 'MicrobitOscuino/main.py')


class _MB:
    message = staticmethod(osce_mod.message)
    bundle = staticmethod(osce_mod.bundle)
    messages = staticmethod(oscd_mod.messages)
    slip_encode = staticmethod(slip_mod.slip_encode)
    SlipDecoder = slip_mod.SlipDecoder


mb = _MB
cp = load('cp_fw', 'CircuitPythonOscuino/code.py')
fj = load('fj_fw', 'FruitJamOscuino/code.py')

passed = failed = 0


def ok(label, cond, detail=''):
    global passed, failed
    if cond:
        passed += 1
        print('  ok   ' + label)
    else:
        failed += 1
        print('  FAIL ' + label + ('\n       ' + str(detail) if detail else ''))


def _raises(fn, *a):
    try:
        fn(*a)
        return False
    except Exception:
        return True


# ---- encoders agree with oscprobe, byte for byte ---------------------------
print('encoders vs oscprobe')
CORPUS = [
    ('/s/m', ()),
    ('/a/0', (1023,)),
    ('/d/13', (0,)),
    ('/d/13', (3,)),                       # 0x03: a ctrl-C byte on the wire
    ('/x', (192, 219, 220, 221)),          # END, ESC, ESC_END, ESC_ESC as ints
    ('/d/9', (0.5,)),
    ('/f', (-1.25, 0.0)),
    ('/hello', ('MicrobitOscuino',)),
    ('/s', ('', 'abc', 'abcd')),           # padding at every remainder
    ('/blob', (b'\xc0\xdb\x00\x01',)),
    ('/blob', (b'',)),
    ('/mix', (7, 0.5, 'x', b'\xdb')),
    ('/neg', (-1, -2147483648, 2147483647)),
]
for addr, args in CORPUS:
    want = oscprobe.msg(addr, list(args))
    ok('message %s %r' % (addr, args), mb.message(addr, *args) == want,
       mb.message(addr, *args).hex() + ' != ' + want.hex())

m1, m2 = oscprobe.msg('/a/1'), oscprobe.msg('/d/5', [1])
ok('bundle framing and immediate timetag',
   mb.bundle([m1, m2]) == oscprobe.bundle([m1, m2]))

# ---- the spec oracle accepts everything the firmwares emit -----------------
print('encoders vs the spec oracle')
for addr, args in CORPUS:
    try:
        oracle.decode(mb.message(addr, *args))
        ok('oracle accepts %s %r' % (addr, args), True)
    except oracle.Bad as e:
        ok('oracle accepts %s %r' % (addr, args), False, e)

got = oracle.decode(mb.message('/tfn', True, False, None))
ok('T F N encode as zero-width tags',
   got == ('msg', '/tfn', [('T', None), ('F', None), ('N', None)]), got)

try:
    oracle.decode(mb.bundle([mb.message('/a/1', 7)]))
    ok('oracle accepts a firmware bundle', True)
except oracle.Bad as e:
    ok('oracle accepts a firmware bundle', False, e)

ok('int32 overflow folds instead of raising',
   oracle.decode(mb.message('/x', 2 ** 31)) == ('msg', '/x', [('i', -2 ** 31)]))

# ---- decoder flattens what oscprobe (and the page) send --------------------
print('decoders vs oscprobe traffic')
pkt = oscprobe.bundle([oscprobe.msg('/a/1'),
                       oscprobe.bundle([oscprobe.msg('/d/5', [1])]),
                       oscprobe.msg('/s/l', [0])])
ok('nested bundle flattens in order',
   mb.messages(pkt) == [('/a/1', []), ('/d/5', [1]), ('/s/l', [0])],
   mb.messages(pkt))

pkt = oscprobe.msg('/mix', [7, 0.5, 'x', b'\xdb'])
ok('bare message decodes',
   mb.messages(pkt) == [('/mix', [7, 0.5, 'x', b'\xdb'])], mb.messages(pkt))

# h is skipped at its known 8-byte width, so the int after it still aligns.
raw = oscprobe.ostr('/x') + oscprobe.ostr(',hi') + struct.pack('>q', 7) + struct.pack('>i', 42)
ok('h skipped at known width, next arg aligned', mb.messages(raw) == [('/x', [42])])

# An unknown tag has no known width: parsing stops rather than misparses.
raw = oscprobe.ostr('/x') + oscprobe.ostr(',qi') + struct.pack('>i', 42)
ok('unknown tag stops the argument walk', mb.messages(raw) == [('/x', [])])

ok('truncated packet raises for the caller to drop',
   _raises(mb.messages, oscprobe.msg('/a/1', [7])[:-2]))

# ---- SLIP ------------------------------------------------------------------
print('SLIP framing')
payload = oscprobe.msg('/x', [192, 219, 3])
ok('slip_encode matches oscprobe (double-END framing)',
   mb.slip_encode(payload) == oscprobe.slip_encode(payload))

stream = mb.slip_encode(m1) + b'\xc0' + mb.slip_encode(payload)   # stray END,
for n in (1, 2, 3, 7, len(stream)):                               # as ZLP pad
    dec = mb.SlipDecoder()
    frames = []
    for i in range(0, len(stream), n):
        frames += dec.feed(stream[i:i + n])
    ok('stream reassembles fed %d bytes at a time' % n,
       frames == [m1, payload], [f.hex() for f in frames])

ok('decoder output matches oscprobe.slip_frames',
   mb.SlipDecoder().feed(stream) == oscprobe.slip_frames(stream))

# ---- every firmware carries the same codec ---------------------------------
print('cross-firmware')
for addr, args in CORPUS:
    ok('message bytes identical: %s %r' % (addr, args),
       mb.message(addr, *args) == cp.message(addr, *args) == fj.message(addr, *args))
ok('bundle bytes identical',
   mb.bundle([m1, m2]) == cp.bundle([m1, m2]) == fj.bundle([m1, m2]))
ok('slip bytes identical',
   mb.slip_encode(payload) == cp.slip_encode(payload) == fj.slip_encode(payload))
ok('decode results identical',
   mb.messages(pkt) == cp.messages(pkt) == fj.messages(pkt))

print('\n%s (%d passed, %d failed)'
      % ('FAILURES' if failed else 'all host checks passed', passed, failed))
sys.exit(1 if failed else 0)

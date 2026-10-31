#!/usr/bin/env python3
"""Hardware check of the I2C bus and a Wii extension on it (ADDRESSES.md:
"i2c", "wii", "wii fields"), on XiaoSamd21Oscuino or KB2040Oscuino: the
board's hello names which, and the checks that differ follow it.

    python3 test/hardware/wiiprobe.py PORT [--watch=SECONDS] [--none]

Plug an extension into the adapter first: on the XIAO, the adapter wired to
D4 (SDA), D5 (SCL), 3V3 and GND; on the KB2040, a STEMMA QT cable from its
port to the adapter's. Or pass --none to check the bus with nothing on it.
The extension's kinds and fields are read from the WiiExtension.h beside the
sketch the board names. The checks:

  - /i2c 1 is echoed with /i2c/pull, and the hello comes again with /enq/i2c
    and no /enq/cap. On the XIAO /i2c/pull carries two values, 0 or 1, and a
    /cap ask goes unanswered: the pads have stopped. On the KB2040, whose own
    pull-ups are always on both lines, /i2c/pull is bare
  - /i2c/scan lists 0x52; /wii/id names a kind WiiExtension.h binds, with six
    identity bytes that are not all zero; /enq/wii names the same kind
  - /wii/raw, 50 times: the board's /wii has one value per field of that kind,
    each an int (a float for a WII_GETF field) within the field's range, and
    /wii/raw has 6..21 bytes (the library's MinRequestSize and
    ControlDataSize), each 0..255
  - with the stream at /rate 20, at least 10 bundles in 1 s carry /wii
  - /i2c 0 is echoed 0 and the hello comes again. On the XIAO it gives the
    pins back: /enq/cap in the hello, /cap answered. On the KB2040 the hello
    has /enq/i2c and no /enq/wii, and the stream at /rate 20 carries /state
    and no /wii

With --watch=S it then records the stream for S seconds while a person
presses buttons and moves the sticks, and prints each field's range and the
order in which the 0..1 fields first read 1. It asserts nothing about the watch: the
person compares that order with what they did, which is the evidence for
button polarity and for which way a stick counts. Exit status 0 only if
every check passed.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import Port, slip_encode, slip_frames, msg, decode  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCHES = {'XiaoSamd21Oscuino': True, 'KB2040Oscuino': False}   # the sketches this knows: has touch pads
FAILED, PASSED = [], []


def check(name, ok, detail=''):
    print(f"  {'ok  ' if ok else 'FAIL'}  {name}" + (f"  {detail}" if detail else ''))
    (PASSED if ok else FAILED).append(name)


def flat(d):
    if d[0] == 'bundle':
        for e in d[1]:
            yield from flat(e)
    else:
        yield d


def load_kinds(path):
    """WiiExtension.h's kinds: name -> [(field, lo, hi, is_float)], each field
    one WII_GET or WII_GETF line, in order."""
    h = open(path).read()
    fields = {}
    for arr, body in re.findall(r'static const WiiField (\w+)\[\] = \{(.*?)\n\};', h, re.S):
        fields[arr] = [(getter, int(lo), int(hi), f == 'F') for f, _, getter, lo, hi in
                       re.findall(r'WII_GET(F?)\((\w+), ([\w.]+), (-?\d+), (-?\d+)\)', body)]
    return {name: fields[arr] for name, arr in
            re.findall(r'\{ "(\w+)", ExtensionType::\w+, (\w+), WII_COUNT_OF\(\2\) \}', h)}


def in_field(value, field):
    """The value's type is the field's, and it lies within the field's range."""
    _, lo, hi, is_float = field
    typed = isinstance(value, float) if is_float else isinstance(value, int)
    return typed and lo <= value <= hi


class Board:
    def __init__(self, path):
        self.p = Port(path)
        self.bad = []

    def ask(self, addr, *args, wait=0.3):
        self.p.write(slip_encode(msg(addr, list(args))))
        return self.collect(wait)

    def collect(self, wait):
        out = []
        for f in slip_frames(self.p.drain(wait)):
            try:
                out.extend(flat(decode(f)))
            except Exception as e:      # kept and counted, never guessed at
                self.bad.append((repr(e), f.hex()))
        return out

    def reply(self, addr, *args, want=None, wait=0.3):
        for a, v in self.ask(addr, *args, wait=wait):
            if a == (want or addr):
                return v
        return None

    def close(self):
        self.p.close()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    watch = next((float(a.split('=', 1)[1]) for a in sys.argv[1:] if a.startswith('--watch=')), 0)
    nothing = '--none' in sys.argv
    b = Board(args[0])
    try:
        b.ask('/rate', 0, wait=0.5)
        name = next((v[0] for a, v in b.ask('/enq', wait=0.6) if a == '/enq' and v), None)
        check('the hello names a sketch this probe knows', name in SKETCHES, name)
        if name not in SKETCHES:
            return 1
        pads = SKETCHES[name]
        kinds = load_kinds(os.path.join(HERE, '..', '..', 'examples', name, 'WiiExtension.h'))
        print(f'{name}: WiiExtension.h beside it binds {len(kinds)} kinds')
        b.ask('/i2c', 0, wait=0.8)
        print('the bus on:')
        got = b.ask('/i2c', 1, wait=1.0)
        d = {}
        for a, v in got:
            d.setdefault(a, v)
        check('/i2c 1 echoed', d.get('/i2c') == [1], d.get('/i2c'))
        pull = d.get('/i2c/pull')
        if pads:
            check('/i2c/pull read: two values, 0 or 1', bool(pull) and len(pull) == 2 and all(x in (0, 1) for x in pull), pull)
        else:
            check("/i2c/pull bare: the board's own pull-ups are on both lines", pull == [], pull)
        check('the hello again, with /enq/i2c and no /enq/cap', '/enq' in d and '/enq/i2c' in d and '/enq/cap' not in d,
              [a for a, _ in got if a.startswith('/enq')])
        if pads:
            check('a /cap ask goes unanswered while the bus has the pins', b.reply('/cap') is None)

        scan = b.reply('/i2c/scan', wait=0.6)
        print(f'    /i2c/scan: {[hex(x) for x in scan or []]}')
        got = b.ask('/wii/id', wait=1.0)
        ident = next((v for a, v in got if a == '/wii/id'), None)
        enq_wii = next((v for a, v in got if a == '/enq/wii'), None)
        print(f'    /wii/id: {ident}')
        if nothing:
            check('nothing plugged in: 0x52 absent from the scan', scan is not None and 0x52 not in scan, scan)
            check('nothing plugged in: /wii/id is the bare address', ident == [], ident)
            check('nothing plugged in: /wii answers nothing', b.reply('/wii') is None)
        else:
            check('/i2c/scan lists 0x52', scan is not None and 0x52 in scan, scan)
            name = ident[0] if ident else None
            check('/wii/id names a kind WiiExtension.h knows', name in kinds, name)
            if name in kinds:
                fields = kinds[name]
                idb = ident[1:]
                check('... with six identity bytes, not all zero', len(idb) == 6 and any(idb),
                      ' '.join(f'{x:02x}' for x in idb))
                check('/enq/wii in the hello names the same kind', enq_wii in (None, [name]) and
                      any(a == '/enq/wii' and v == [name] for a, v in b.ask('/enq', wait=0.6)), enq_wii)
                compared, mismatch = 0, None
                for _ in range(50):
                    r = dict((a, v) for a, v in b.ask('/wii/raw', wait=0.08))
                    raw, vals = r.get('/wii/raw'), r.get('/wii')
                    if raw is None or vals is None:
                        continue
                    compared += 1
                    if mismatch is None:
                        if not 6 <= len(raw) <= 21 or not all(isinstance(x, int) and 0 <= x <= 255 for x in raw):
                            mismatch = f'/wii/raw {raw}'
                        elif len(vals) != len(fields):
                            mismatch = f'{len(vals)} values for {len(fields)} fields: {vals}'
                        else:
                            out = [f'{fl[0]} {v!r} not in {fl[1]}..{fl[2]}{" float" if fl[3] else ""}'
                                   for v, fl in zip(vals, fields) if not in_field(v, fl)]
                            if out:
                                mismatch = '; '.join(out)
                check(f'/wii: one value per field, each of its type and in its range; /wii/raw 6..21 bytes '
                      f'({compared} reads of 50 compared)', compared >= 40 and mismatch is None, mismatch or '')
                b.ask('/rate', 20, wait=0.05)
                frames = [a for a, _ in b.collect(1.0)]
                b.ask('/rate', 0, wait=0.3)
                n_wii = frames.count('/wii')
                check(f'the stream at /rate 20 carries /wii: {n_wii} in 1 s, {frames.count("/state")} /state',
                      n_wii >= 10)
                if watch:
                    print(f'\nwatch: {watch:g} s of the stream; press buttons and move sticks now', flush=True)
                    b.ask('/rate', 20, wait=0.05)
                    rows, end = [], time.time() + watch
                    while time.time() < end:
                        for a, v in b.collect(0.2):
                            if a == '/wii' and len(v) == len(fields):
                                rows.append((time.time(), v))
                    b.ask('/rate', 0, wait=0.3)
                    print(f'    {len(rows)} reports')
                    firsts = []
                    for i, (fname, lo, hi, _) in enumerate(fields):
                        col = [v[i] for _, v in rows]
                        if not col:
                            continue
                        line = f'    {fname:>18}: first {col[0]:>8.6g}  min {min(col):>8.6g}  max {max(col):>8.6g}'
                        if (lo, hi) == (0, 1):
                            on = next((t for t, v in rows if v[i] == 1), None)
                            line += '  pressed at ' + (f'{on - rows[0][0]:.1f} s' if on else '-')
                            if on:
                                firsts.append((on, fname))
                        print(line)
                    print('    buttons in the order they first read 1: ' +
                          (' '.join(n for _, n in sorted(firsts)) or 'none'))
        print('the bus off:')
        got = b.ask('/i2c', 0, wait=1.2)
        d = {}
        for a, v in got:
            d.setdefault(a, v)
        check('/i2c 0 echoed', d.get('/i2c') == [0], d.get('/i2c'))
        if pads:
            check('the hello again, with /enq/cap', '/enq/cap' in d, d.get('/enq/cap'))
            cap = b.reply('/cap')
            check('a /cap ask answered again', cap is not None and len(cap) == (d.get('/enq/cap') or [0])[0], cap)
        else:
            check('the hello again, with /enq/i2c and no /enq/wii', '/enq' in d and '/enq/i2c' in d and '/enq/wii' not in d,
                  [a for a, _ in got if a.startswith('/enq')])
            b.ask('/rate', 20, wait=0.05)
            frames = [a for a, _ in b.collect(1.0)]
            b.ask('/rate', 0, wait=0.3)
            check(f'the stream at /rate 20 with the bus off: {frames.count("/state")} /state in 1 s, no /wii',
                  frames.count('/state') >= 10 and '/wii' not in frames)
        b.ask('/rate', 20, wait=0.2)
    finally:
        b.close()
    if b.bad:
        print(f'\n{len(b.bad)} frame(s) would not decode; the first: {b.bad[0]}')
        FAILED.append('undecodable frames')
    print(f'\n{len(PASSED)} passed, {len(FAILED)} failed')
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main())

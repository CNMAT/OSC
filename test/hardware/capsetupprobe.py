#!/usr/bin/env python3
"""Hardware check of the /cap set-up addresses (ADDRESSES.md, "cap set-up"),
written against XiaoSamd21Oscuino.

    python3 test/hardware/capsetupprobe.py PORT

Leave the pads untouched while it runs (about 20 s). It checks two things per
setting: the echo (what the sketch says it holds), and the readings (what the
PTC does with it), so a setting that is echoed but never reaches the
conversion fails. Exit status 0 only if every check passed.

"moved" means the mean of 5 asks lies more than MOVED_FACTOR x the baseline's
own min..max range (at least 1 count) from the baseline mean. The factor is a
choice, not a derived quantity; every number is printed so the reader can
judge for themselves.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import Port, slip_encode, slip_frames, msg, decode  # noqa: E402

MOVED_FACTOR = 5
FAILED = []
PASSED = []


def check(name, ok, detail=''):
    print(f"  {'ok  ' if ok else 'FAIL'}  {name}" + (f"  {detail}" if detail else ''))
    (PASSED if ok else FAILED).append(name)


def flat(d):
    if d[0] == 'bundle':
        for e in d[1]:
            yield from flat(e)
    else:
        yield d


class Board:
    def __init__(self, path):
        self.p = Port(path)

    def ask(self, addr, *args, wait=0.25):
        self.p.write(slip_encode(msg(addr, list(args))))
        out = []
        for f in slip_frames(self.p.drain(wait)):
            out.extend(flat(decode(f)))
        return out

    def reply(self, addr, *args, want=None):
        """Args of the first reply on `want` (default: the address sent)."""
        want = want or addr
        for a, v in self.ask(addr, *args):
            if a == want:
                return v
        return None

    def readings(self, n=5):
        rows = [self.reply('/cap') for _ in range(n)]
        rows = [r for r in rows if r]
        return rows

    def close(self):
        self.p.close()


def stats(rows):
    cols = list(zip(*rows))
    return [sum(c) / len(c) for c in cols], [max(c) - min(c) for c in cols]


def moved(base, new):
    bm, br = stats(base)
    nm, _ = stats(new)
    return [abs(n - b) > MOVED_FACTOR * max(1, r) for b, r, n in zip(bm, br, nm)], bm, nm


def main():
    b = Board(sys.argv[1])
    try:
        b.ask('/rate', 0, wait=0.5)
        print('defaults and pins:')
        cfg = dict(b.ask('/cap/defaults'))
        n = len(cfg.get('/cap/pins', []))
        check('/cap/defaults answers with 7 pins', n == 7, cfg.get('/cap/pins'))
        expect = {'/cap/oversample': 4, '/cap/resistor': 50, '/cap/freq': 0,
                  '/cap/hop': 1, '/cap/compcap': 8192, '/cap/intcap': 63}
        for k, v in expect.items():
            check(f'{k} default {v} on every pad', cfg.get(k) == [v] * n, cfg.get(k))
        check('/cap/pins is D0 D1 D6 D7 D8 D9 D10',
              b.reply('/cap/pins') == [0, 1, 6, 7, 8, 9, 10])

        base = b.readings(10)
        bm, br = stats(base)
        print(f'  baseline mean {[round(x, 1) for x in bm]}  range {br}')

        print('every pad, one value:')
        check('/cap/compcap 4000 echoed on every pad',
              b.reply('/cap/compcap', 4000) == [4000] * n)
        mv, _, nm = moved(base, b.readings())
        check('compcap 4000 moves every pad', all(mv), [round(x, 1) for x in nm])
        b.ask('/cap/compcap', 8192)

        print('one pad by index:')
        check('/cap/3/intcap 10 echoed on pad 3 only',
              b.reply('/cap/3/intcap', 10, want='/cap/intcap') == [63, 63, 63, 10, 63, 63, 63])
        mv, _, nm = moved(base, b.readings())
        check('intcap 10 on pad 3 moves pad 3 and no other',
              mv == [i == 3 for i in range(n)], f'{[round(x, 1) for x in nm]} moved={mv}')
        b.ask('/cap/intcap', 63)

        print('pattern and list forms:')
        check('/cap/[0-1]/hop 5 sets pads 0 and 1',
              b.reply('/cap/[0-1]/hop', 5, want='/cap/hop') == [5, 5, 1, 1, 1, 1, 1])
        check('/cap/*/hop 1 sets every pad',
              b.reply('/cap/*/hop', 1, want='/cap/hop') == [1] * n)
        check('/cap/intcap with 7 values sets each pad',
              b.reply('/cap/intcap', 1, 2, 3, 4, 5, 6, 7) == [1, 2, 3, 4, 5, 6, 7])
        b.ask('/cap/intcap', 63)
        check('float 100.4 rounds to 100', b.reply('/cap/compcap', 100.4) == [100] * n)
        b.ask('/cap/compcap', 8192)

        print('illegal values change nothing:')
        for addr, v, keep in [('/cap/oversample', 3, 4), ('/cap/oversample', 128, 4),
                              ('/cap/resistor', 30, 50), ('/cap/freq', 4, 0),
                              ('/cap/hop', 0, 1), ('/cap/hop', 17, 1),
                              ('/cap/compcap', 16384, 8192), ('/cap/compcap', -1, 8192),
                              ('/cap/intcap', 64, 63)]:
            check(f'{addr} {v} refused', b.reply(addr, v) == [keep] * n)

        print('oversample keeps the scale (measure() normalisation):')
        for os_ in (1, 4, 16, 64):
            b.ask('/cap/oversample', os_)
            nm, nr = stats(b.readings())
            diag = b.reply('/enq', want='/diag')
            scan = re.search(r'last scan (\d+) us', diag[0]).group(1) if diag else '?'
            ratio = [x / y for x, y in zip(nm, bm)]
            print(f'    oversample {os_:2}: mean {[round(x, 1) for x in nm]} range {nr} '
                  f'scan {scan} us')
            check(f'oversample {os_}: every pad within 0.5..2x of oversample 4',
                  all(0.5 < r < 2 for r in ratio), [round(r, 2) for r in ratio])
        b.ask('/cap/oversample', 4)

        print('resistor and freq, readings only (no expectation):')
        for res in (0, 20, 50, 100):
            b.ask('/cap/resistor', res)
            nm, nr = stats(b.readings())
            print(f'    resistor {res:3} kOhm: mean {[round(x, 1) for x in nm]} range {nr}')
        b.ask('/cap/resistor', 50)
        for fr in (0, 1):
            b.ask('/cap/freq', fr)
            nm, nr = stats(b.readings())
            print(f'    freq {fr}: mean {[round(x, 1) for x in nm]} range {nr}')

        print('back to defaults:')
        b.ask('/cap/defaults')
        mv, _, nm = moved(base, b.readings())
        check('defaults restore the baseline on every pad', not any(mv),
              [round(x, 1) for x in nm])
        b.ask('/rate', 20)
    finally:
        b.close()
    print(f'\n{len(PASSED)} passed, {len(FAILED)} failed')
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main())

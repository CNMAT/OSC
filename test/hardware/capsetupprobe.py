#!/usr/bin/env python3
"""Hardware check of the /cap set-up addresses (ADDRESSES.md, "cap set-up"),
written against XiaoSamd21Oscuino.

    python3 test/hardware/capsetupprobe.py PORT

Leave the pads untouched while it runs (about 45 s). It checks two things per
setting: the echo (what the sketch says it holds), and the readings (what the
PTC does with it), so a setting that is echoed but never reaches the
conversion fails. Then it switches /cap/lib through FastTouch and
CapacitiveSensor and back, and checks that each library reports its own pins,
that CapacitiveSensor refuses bad pins, and that FreeTouch's resting counts
come back after the other two have had the pins. With no resistors fitted,
CapacitiveSensor can only be checked for its plumbing: every read times out.
Exit status 0 only if every check passed.

Two checks guard fixes that a reading cannot show:
  - the PTC as /diag reads it back: on, with exactly the pads' Y lines,
    under FreeTouch; off with none under the other two, whose pins it
    otherwise held (FastTouch read nothing on the Y-line pins)
  - OSC is answered between pads: with the stream on and four
    CapacitiveSensor pads each waiting out its timeout, an echo comes back
    within LATENCY_LIMIT s, where a scan read in one go held it for up to
    the whole scan (/diag's "last scan" is printed and must exceed 2 s, or
    the check proves nothing)

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
# One CapacitiveSensor pad's timeout at the default 2000, 0.70 s measured on
# this board on 2026-10-28, plus 0.3 s for the USB round trip and drain()'s
# 50 ms select: a choice, printed beside every latency.
LATENCY_LIMIT = 1.0
# The sketch header's pad table: FreeTouch's getYLine() on the XIAO_m0 variant.
YLINE = {0: 0, 1: 2, 6: 14, 7: 15, 8: 5, 9: 3, 10: 4}
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

    def reply(self, addr, *args, want=None, wait=0.25):
        """Args of the first reply on `want` (default: the address sent)."""
        want = want or addr
        for a, v in self.ask(addr, *args, wait=wait):
            if a == want:
                return v
        return None

    def latency(self, addr, *args, want=None, limit=4.0):
        """Seconds until the first message on `want` (default: addr) comes
        back, or None after `limit`."""
        want = want or addr
        self.p.drain(0.05)                 # what was already in flight is not the answer
        t0 = time.time()
        self.p.write(slip_encode(msg(addr, list(args))))
        buf = b''
        while time.time() - t0 < limit:
            buf += self.p.drain(0.01)
            if any(a == want for f in slip_frames(buf) for a, _ in flat(decode(f))):
                return time.time() - t0
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


def ptc_in(diag):
    """The 'PTC on, Y 0x....' part of a /diag string, or None."""
    m = re.search(r'PTC (on|off), Y 0x([0-9a-f]{4})', (diag or [''])[0])
    return (m.group(1), int(m.group(2), 16)) if m else None


def check_ptc(when, diag, pins):
    """FreeTouch's pads hold the PTC on with their Y lines; anything else, off with none."""
    want = ('on', sum(1 << YLINE[p] for p in pins)) if pins else ('off', 0)
    got = ptc_in(diag)
    check(f'{when}: /diag reads the PTC back {want[0]}, Y 0x{want[1]:04x}', got == want,
          f'got {got}' if got else f'no PTC in {diag}')


def main():
    b = Board(sys.argv[1])
    try:
        b.ask('/rate', 0, wait=0.5)
        b.ask('/cap/lib', 'FreeTouch', wait=0.6)
        print('defaults and pins:')
        cfg = dict(b.ask('/cap/defaults'))
        n = len(cfg.get('/cap/pins', []))
        check('/cap/defaults answers with 7 pins', n == 7, cfg.get('/cap/pins'))
        expect = {'/cap/oversample': 4, '/cap/resistor': 50, '/cap/freq': 0,
                  '/cap/hop': 1, '/cap/compcap': 8192, '/cap/intcap': 63}
        for k, v in expect.items():
            check(f'{k} default {v} on every pad', cfg.get(k) == [v] * n, cfg.get(k))
        ft_pins = [0, 1, 6, 7, 8, 9, 10]
        check('/cap/pins is D0 D1 D6 D7 D8 D9 D10', b.reply('/cap/pins') == ft_pins)
        check_ptc('FreeTouch at the start', b.reply('/enq', want='/diag'), ft_pins)

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

        print('the library switch:')
        got = dict(b.ask('/cap/lib', 'FastTouch', wait=0.6))
        check('/cap/lib FastTouch echoed, and the hello re-sent with 11 pads',
              got.get('/cap/lib') == ['FastTouch'] and got.get('/enq/cap') == [11], got.get('/enq/cap'))
        check_ptc('FastTouch', got.get('/diag'), [])
        check('FastTouch pins are D0..D10', b.reply('/cap/pins') == list(range(11)))
        ft = b.readings(5)
        check('FastTouch readings are eleven values within 0..23',
              len(ft) == 5 and all(len(r) == 11 and all(0 <= v <= 23 for v in r) for r in ft),
              ft[-1] if ft else None)
        check('a FreeTouch setting is still answered while FastTouch measures',
              b.reply('/cap/intcap') == [63] * 7)

        got = dict(b.ask('/cap/lib', 'CapacitiveSensor', wait=0.6))
        check('/cap/lib CapacitiveSensor echoed, and the hello carries no pads',
              got.get('/cap/lib') == ['CapacitiveSensor'] and '/enq' in got and '/enq/cap' not in got)
        check_ptc('CapacitiveSensor', got.get('/diag'), [])
        check('with no pads, /cap answers nothing', b.reply('/cap') is None)
        for addr, args, want in [('/cap/send', [11], []), ('/cap/send', [-1], []),
                                 ('/cap/send', [10], [10]), ('/cap/recv', [10, 9], []),
                                 ('/cap/recv', [9, 9], []), ('/cap/recv', [9, 8], [9, 8]),
                                 ('/cap/send', [9], [10])]:
            check(f'{addr} {" ".join(map(str, args))} -> {want}', b.reply(addr, *args, wait=0.6) == want)
        check('two pads: samples and timeout at their defaults',
              b.reply('/cap/samples') == [30, 30] and b.reply('/cap/timeout') == [2000, 2000])
        check('/cap/timeout 70000 refused', b.reply('/cap/timeout', 70000) == [2000, 2000])
        b.ask('/cap/timeout', 20)
        r = b.reply('/cap', wait=0.6)
        check('with no resistors fitted, both pads read the library\'s -2', r == [-2, -2], r)

        print('OSC between pads (the stream on, four pads timing out):')
        b.ask('/cap/recv', 9, 8, 7, 6, wait=0.6)
        b.ask('/cap/timeout', 2000, wait=0.6)
        b.ask('/rate', 20, wait=0.1)
        lat = []
        for k in range(5):
            time.sleep(0.37 * k)           # land at different points in the scan
            lat.append(b.latency('/cap/samples'))
        diag = b.reply('/enq', want='/diag', wait=3.5)
        m = re.search(r'last scan (\d+) us', (diag or [''])[0])
        scan_us = int(m.group(1)) if m else None
        print(f'    latencies {[None if x is None else round(x, 2) for x in lat]} s; '
              f'last scan {scan_us} us')
        check('a scan of four timed-out pads takes over 2 s (else the next check proves nothing)',
              scan_us is not None and scan_us > 2_000_000, scan_us)
        check(f'every echo came back within {LATENCY_LIMIT} s while such scans ran',
              all(x is not None and x < LATENCY_LIMIT for x in lat))
        b.ask('/rate', 0, wait=3.5)

        cfg = dict(b.ask('/cap/defaults', wait=0.6))
        check('/cap/defaults takes the pins away', cfg.get('/cap/pins') == [] and cfg.get('/cap/send') == [])

        got = dict(b.ask('/cap/lib', 'FreeTouch', wait=0.6))
        check_ptc('FreeTouch again', got.get('/diag'), ft_pins)
        mv, _, nm = moved(base, b.readings())
        check('back on FreeTouch, every pad rests where it did', not any(mv),
              [round(x, 1) for x in nm])
        b.ask('/rate', 20)
    finally:
        b.close()
    print(f'\n{len(PASSED)} passed, {len(FAILED)} failed')
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main())

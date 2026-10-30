#!/usr/bin/env python3
"""Wait for a touch on a /cap board and say which pad rose, per library.

    python3 test/hardware/captouchprobe.py PORT LIB [LIB ...] [--secs=N]

For each LIB in turn (as /cap/lib names it: FreeTouch, FastTouch,
CapacitiveSensor) the board is switched to it, its resting level is read for
1.5 s, and then the probe WAITS -- up to --secs (default 120) -- for one pad to
rise, records until every pad has been back at rest for 1 s, and reports each
pad's resting range and peak. There is no window to miss: tell the person
what to touch before starting it, and they act whenever they like.

Written for XiaoSamd21Oscuino because humanprobe.py cannot answer "which pad":
it reports per-element changes only for one-value messages and returns on the
first change, and resting /cap jitter is a change.

A pad counts as risen when it exceeds its own resting maximum by more than
max(2, its resting range) counts. That margin is a choice, not a derived
quantity; every number is printed.

A touch during the rest window raises that pad's resting maximum and hides
its rise. A rest whose range on any pad exceeds max(2, a tenth of its
minimum) is read again, up to three times, and reported as SUSPECT if it
never settles. The tenth is a choice between what one run on this board showed
on 2026-10-28: of its three untouched FreeTouch pads, the widest spread was 6
counts on a rest of 348 (D1); touched during the window, D7 read 166..549. The windows (1.5 s of
rest, which gave 14..23 reads here, and 1 s back at rest to end a touch) are
choices too.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import Port, slip_encode, slip_frames, msg, decode  # noqa: E402


def flat(d):
    if d[0] == 'bundle':
        for e in d[1]:
            yield from flat(e)
    else:
        yield d


BAD = []     # frames that would not decode: kept, counted and shown, never guessed at


def ask(p, addr, args=(), wait=0.05):
    p.write(slip_encode(msg(addr, list(args))))
    out = []
    for f in slip_frames(p.drain(wait)):
        try:
            out.extend(flat(decode(f)))
        except Exception as e:   # oscprobe.decode raises on a frame it cannot parse
            BAD.append((repr(e), f.hex()))
    return out


def reply(p, addr, args=(), want=None, wait=0.3):
    for a, v in ask(p, addr, args, wait):
        if a == (want or addr):
            return v
    return None


def reading(p):
    return reply(p, '/cap', wait=0.06)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    secs = 120
    for f in sys.argv[1:]:
        if f.startswith('--secs='):
            secs = int(f.split('=', 1)[1])
    port, libs = args[0], args[1:]
    p = Port(port)
    rc = 0
    try:
        ask(p, '/rate', [0], wait=0.4)
        for lib in libs:
            got = reply(p, '/cap/lib', [lib], wait=0.6)
            pins = reply(p, '/cap/pins')
            print(f'\n== {lib}: /cap/lib -> {got}, pins {pins}')
            if got != [lib] or not pins:
                print('   not selected, or no pads; skipped')
                rc = 1
                continue
            for attempt in range(3):
                print('   hands off: reading the resting level for 1.5 s', flush=True)
                rest, end = [], time.time() + 1.5
                while time.time() < end:
                    r = reading(p)
                    if r and len(r) == len(pins):
                        rest.append(r)
                lo = [min(c) for c in zip(*rest)]
                hi = [max(c) for c in zip(*rest)]
                wide = [i for i, (l, h) in enumerate(zip(lo, hi)) if h - l > max(2, l / 10)]
                print(f'   resting over {len(rest)} reads: min {lo}  max {hi}')
                if not wide:
                    break
                print('   too wide to be rest on ' + ', '.join(f'D{pins[i]}' for i in wide)
                      + ' (touched during the window?)')
            else:
                print('   SUSPECT: never settled; every rise below is measured from a raised maximum')
                rc = 1
            margin = [max(2, h - l) for l, h in zip(lo, hi)]
            print(f'   waiting up to {secs} s for a touch ...', flush=True)
            rows, touched, quiet_since, end = [], False, None, time.time() + secs
            while time.time() < end:
                r = reading(p)
                if not r or len(r) != len(pins):
                    continue
                up = [v - h > m for v, h, m in zip(r, hi, margin)]
                if any(up):
                    touched, quiet_since = True, None
                    rows.append(r)
                elif touched:
                    quiet_since = quiet_since or time.time()
                    if time.time() - quiet_since > 1.0:
                        break
            if not rows:
                print('   VERDICT: nothing rose. Either it was not touched, or this library\n'
                      '   does not see it -- a uniform negative is the apparatus until a\n'
                      '   known-good touch says otherwise.')
                rc = 1
                continue
            peak = [max(c) for c in zip(*rows)]
            rose = [i for i, (pk, h, m) in enumerate(zip(peak, hi, margin)) if pk - h > m]
            first = next(i for i, v in enumerate(rows[0]) if v - hi[i] > margin[i])
            print(f'   {len(rows)} reads above rest; peak {peak}')
            print(f'   first to rise: pad {first} = D{pins[first]}; risen: '
                  + ', '.join(f'pad {i} = D{pins[i]} (+{peak[i] - hi[i]})' for i in rose))
        ask(p, '/rate', [20], wait=0.3)
    finally:
        p.close()
    if BAD:
        print(f'\n{len(BAD)} frame(s) would not decode; the first:')
        for err, hx in BAD[:2]:
            print(f'   {err}\n   {hx}')
        rc = 1
    return rc


if __name__ == '__main__':
    sys.exit(main())

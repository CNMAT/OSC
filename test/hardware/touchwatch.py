#!/usr/bin/env python3
"""Record a /cap stream while a person touches pads, then say which pad rose,
in what order.

    python3 test/hardware/touchwatch.py PORT SECONDS [OUT.csv]

Every /cap row is written to OUT.csv (default touch-<time>.csv here in the
current directory; an existing file is refused).

This records a FIXED window, the design humanprobe.py exists to avoid: tool
output reaches the person only when the turn ends. Tell them what to touch, in
the channel they read, before starting it. Its first run (2026-10-28,
xiaosamd21-touch-2026-10-28.csv) had the instruction arrive late, and only the
last few seconds of 60 were touched one pin at a time. It is kept because
humanprobe.py reports per-element changes only for one-value messages, and
returns on the first change, which resting /cap jitter provides. A pad
counts as touched in a row when it reads more than RISE x its own minimum over
the whole capture (a touch only raises the count, so the minimum is the
untouched level); a touch is a run of at least MIN_ROWS such rows. RISE and
MIN_ROWS are choices, not derived; the CSV holds the numbers to judge them by.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import Port, slip_encode, slip_frames, msg, decode  # noqa: E402

RISE = 1.25
MIN_ROWS = 3          # at /rate 20, 60 ms


def flat(d):
    if d[0] == 'bundle':
        for e in d[1]:
            yield from flat(e)
    else:
        yield d


def main():
    port, secs = sys.argv[1], float(sys.argv[2])
    path = sys.argv[3] if len(sys.argv) > 3 else time.strftime('touch-%Y-%m-%d-%H%M%S.csv')
    if os.path.exists(path):
        sys.exit(f'{path} exists; refusing to overwrite')
    p = Port(port)
    p.write(slip_encode(msg('/cap/pins')))
    pins = None
    for f in slip_frames(p.drain(0.4)):
        for a, v in flat(decode(f)):
            if a == '/cap/pins':
                pins = v
    p.write(slip_encode(msg('/rate', [20])))
    rows, buf, end = [], b'', time.time() + secs
    while time.time() < end:
        buf += p.drain(0.2)
        cut = buf.rfind(b'\xc0')
        if cut > 0:
            for f in slip_frames(buf[:cut + 1]):
                ms, cap = None, None
                for a, v in flat(decode(f)):
                    if a == '/state':
                        ms = v[1]
                    elif a == '/cap':
                        cap = v
                if ms is not None and cap:
                    rows.append((ms, cap))
            buf = buf[cut:]
    p.close()

    tmp = path + '.tmp'
    with open(tmp, 'w') as f:
        f.write('millis,' + ','.join(f'D{x}' for x in (pins or [])) + '\n')
        for ms, cap in rows:
            f.write(f'{ms},' + ','.join(map(str, cap)) + '\n')
    os.replace(tmp, path)
    print(f'{len(rows)} rows -> {path}')
    if not rows:
        return 1

    n = len(rows[0][1])
    lo = [min(r[1][i] for r in rows) for i in range(n)]
    hi = [max(r[1][i] for r in rows) for i in range(n)]
    print('pin      ' + ' '.join(f'{"D%d" % x:>6}' for x in pins))
    print('minimum  ' + ' '.join(f'{x:6}' for x in lo))
    print('maximum  ' + ' '.join(f'{x:6}' for x in hi))
    touches, cur = [], None          # cur = [pad, first ms, last ms, peak, rows]
    for ms, cap in rows:
        over = [i for i in range(n) if cap[i] > RISE * lo[i]]
        pad = max(over, key=lambda i: cap[i] / lo[i]) if over else None
        if cur and pad == cur[0]:
            cur[2], cur[3], cur[4] = ms, max(cur[3], cap[pad]), cur[4] + 1
            continue
        if cur and cur[4] >= MIN_ROWS:
            touches.append(cur)
        cur = [pad, ms, ms, cap[pad], 1] if pad is not None else None
    if cur and cur[4] >= MIN_ROWS:
        touches.append(cur)
    t0 = rows[0][0]
    print('touches, in order:')
    for pad, a, b, peak, k in touches:
        print(f'  pad {pad} = D{pins[pad]}  t={(a - t0) / 1000:6.2f} s  '
              f'{(b - a) / 1000:5.2f} s  peak {peak} (min {lo[pad]})')
    order = [pins[t[0]] for t in touches]
    print('order:', ' '.join(f'D{x}' for x in order))
    return 0


if __name__ == '__main__':
    sys.exit(main())

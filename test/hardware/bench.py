#!/usr/bin/env python3
"""Loss-attributing USB bench. Board runs test/hardware/OscBench.

Four counters, three segments:

    host wrote --USB--> board received --sketch--> board sent --USB--> host received
        A                    B                        C                    D

This tool holds A and D; OscBench holds B and C. Every run reports which
segment lost what, or that none did. "Lost somewhere" is not a result.

    python3 bench.py PORT verify              trickle gate: 20 frames at 20/s
    python3 bench.py PORT in  [N] [GAP_US]    host->device: N frames (default
                                              200), GAP_US apart (default 0);
                                              GAP_US=-1 sends all N in ONE
                                              kernel write (max burst pressure)
    python3 bench.py PORT out [N] [GAP_US]    device->host: board floods N
    python3 bench.py PORT search              max clean host->device rate by
                                              bisection on the inter-frame gap
    python3 bench.py PORT ring [LAZY_MS]      burst-size sweep against a
                                              deliberately slow application --
                                              maps a fixed RX ring empirically

Rules, enforced or printed because the withdrawn numbers taught them:
  * the trickle gate runs before anything fast; if slow traffic is not 100%
    the setup is broken and no fast number means anything
  * repeats: in/out/search run each condition 3x; a cliff that does not
    reproduce is not a cliff
  * a number belongs in the README only with a named mechanism, and only if
    the same command passed clean on a known-good reference board (Teensy
    4.0 or any native-CDC SAMD) with the same host on the same day
"""
import sys
import time
import struct
import zlib

sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from oscprobe import Port, slip_encode, slip_frames, msg, decode

REPEATS = 3


def seq_frame(seq):
    """/b/s ,ii seq crc32(seq) -- must match OscBench's crc32_of_seq."""
    crc = zlib.crc32(struct.pack('>I', seq)) & 0xFFFFFFFF
    return slip_encode(msg('/b/s', [seq - 0x100000000 if seq > 0x7FFFFFFF else seq,
                                    crc - 0x100000000 if crc > 0x7FFFFFFF else crc]))


def query(p, tries=3):
    """Ask for /b/r. Retries because the query itself rides the path under test."""
    for _ in range(tries):
        p.write(slip_encode(msg('/b/q')))
        raw = p.drain(1.0)
        for f in slip_frames(raw):
            try:
                addr, args = decode(f)
            except Exception:
                continue
            if addr == '/b/r' and len(args) == 7:
                return dict(zip(('rxFrames', 'rxBytes', 'seqErrs', 'crcErrs',
                                 'decodeErrs', 'firstGap', 'spanMs'), args))
    return None


def report_in(n_sent, r):
    if r is None:
        print("  no /b/r reply -- board unreachable, nothing attributable")
        return False
    clean = (r['rxFrames'] == n_sent and r['seqErrs'] == 0
             and r['crcErrs'] == 0 and r['decodeErrs'] == 0)
    rate = (r['rxFrames'] * 1000 // r['spanMs']) if r['spanMs'] > 0 else 0
    print(f"  A host wrote     : {n_sent} frames (write_all complete by definition)")
    print(f"  B board received : {r['rxFrames']} frames, seqErrs={r['seqErrs']}"
          f" crcErrs={r['crcErrs']} decodeErrs={r['decodeErrs']}"
          + (f" firstGap@{r['firstGap']}" if r['firstGap'] >= 0 else "")
          + (f"  ({rate} f/s on-board" if rate else "")
          + (")" if rate else ""))
    print(f"  A->B             : {'clean' if clean else 'LOSS IN HOST->DEVICE SEGMENT'}")
    return clean


def run_in(p, n, gap_us):
    query(p)                                     # reset counters
    if gap_us < 0:
        p.write(b''.join(seq_frame(i) for i in range(n)))    # one kernel write
    else:
        gap = gap_us / 1e6
        for i in range(n):
            p.write(seq_frame(i))
            if gap:
                time.sleep(gap)
    time.sleep(0.6)                              # quiesce before reporting
    return query(p)


def run_out(p, n, gap_us):
    query(p)
    p.write(slip_encode(msg('/b/f', [n, gap_us])))
    raw = p.drain(2.0 + n * max(gap_us, 100) / 1e6)
    got, seq_bad, crc_bad = 0, 0, 0
    expect = 0
    first_gap = None
    for f in slip_frames(raw):
        try:
            addr, args = decode(f)
        except Exception:
            continue
        if addr != '/b/s' or len(args) != 2:
            continue
        got += 1
        seq = args[0] & 0xFFFFFFFF
        crc = args[1] & 0xFFFFFFFF
        if crc != zlib.crc32(struct.pack('>I', seq)) & 0xFFFFFFFF:
            crc_bad += 1
        if seq != expect:
            seq_bad += 1
            if first_gap is None:
                first_gap = expect
            expect = seq + 1
        else:
            expect += 1
    clean = got == n and seq_bad == 0 and crc_bad == 0
    print(f"  C board sent     : {n} frames commanded")
    print(f"  D host received  : {got} frames, seqErrs={seq_bad} crcErrs={crc_bad}"
          + (f" firstGap@{first_gap}" if first_gap is not None else ""))
    print(f"  C->D             : {'clean' if clean else 'LOSS IN DEVICE->HOST SEGMENT'}")
    return clean


def gate(p):
    print("trickle gate: 20 frames at 20/s (any loss here = broken setup, stop)")
    r = run_in(p, 20, 50000)
    ok = report_in(20, r)
    if not ok:
        print("\nGATE FAILED. Fix the setup before believing any fast number.")
    return ok


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    port_path, mode = sys.argv[1], sys.argv[2]
    a3 = int(sys.argv[3]) if len(sys.argv) > 3 else None
    a4 = int(sys.argv[4]) if len(sys.argv) > 4 else None
    p = Port(port_path)
    fails = 0
    try:
        if mode == 'verify':
            fails += 0 if gate(p) else 1

        elif mode == 'in':
            if not gate(p):
                return 1
            n, gap = a3 or 200, a4 if a4 is not None else 0
            label = "ONE kernel write" if gap < 0 else f"{gap} us apart"
            for k in range(REPEATS):
                print(f"\nin: {n} frames, {label} (run {k + 1}/{REPEATS})")
                fails += 0 if report_in(n, run_in(p, n, gap)) else 1

        elif mode == 'out':
            if not gate(p):
                return 1
            n, gap = a3 or 200, a4 if a4 is not None else 0
            for k in range(REPEATS):
                print(f"\nout: {n} frames, {gap} us apart (run {k + 1}/{REPEATS})")
                fails += 0 if run_out(p, n, gap) else 1

        elif mode == 'search':
            if not gate(p):
                return 1
            n = a3 or 200
            print(f"\nsearch: {n} frames, unpaced back-to-back writes first")
            if all(report_in(n, run_in(p, n, 0)) for _ in range(REPEATS)):
                print(f"\nno pacing needed: {n} frames clean unpaced x{REPEATS}")
            else:
                lo, hi = 0, 20000            # us; hi known-clean by the gate
                while hi - lo > 50:
                    mid = (lo + hi) // 2
                    ok = all(report_in(n, run_in(p, n, mid)) for _ in range(REPEATS))
                    print(f"  gap {mid} us: {'clean' if ok else 'lossy'}")
                    if ok:
                        hi = mid
                    else:
                        lo = mid
                        fails = 0            # lossy runs below threshold expected
                print(f"\nminimum clean inter-frame gap: ~{hi} us"
                      f" ({1e6 / hi:.0f} f/s). A number this precise still"
                      f" needs a mechanism before the README will take it.")

        elif mode == 'ring':
            lazy = a3 if a3 is not None else 20
            print(f"ring map: bursts in one write against a {lazy} ms/loop application")
            p.write(slip_encode(msg('/b/lazy', [lazy])))
            time.sleep(0.3)
            for k in (5, 10, 15, 20, 30, 50):
                r = run_in(p, k, -1)
                got = r['rxFrames'] if r else 0
                print(f"  burst {k:3d} frames ({k * 22} SLIP bytes): received {got}"
                      + ("" if r and r['firstGap'] < 0 else
                         f", firstGap@{r['firstGap']}" if r else ", no reply"))
            p.write(slip_encode(msg('/b/lazy', [0])))
        else:
            print(f"unknown mode {mode}")
            return 2
    finally:
        p.close()
    print(f"\n{'CLEAN' if fails == 0 else 'LOSSY'} ({fails} failing runs)")
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

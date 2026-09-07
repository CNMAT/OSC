#!/usr/bin/env python3
"""Watch for something only a person can cause. Waits for it; no window to miss.

    python3 test/hardware/humanprobe.py PORT /btn        "press the button"
    python3 test/hardware/humanprobe.py PORT /enc  --dir "turn it clockwise"
    python3 test/hardware/humanprobe.py PORT /light      "cover the sensor" --secs 30

Some things cannot be verified without hands: a photon, a beep, a knob, a
press. This runs the window for them, and exists because getting that wrong
wasted three windows in one session.

THE MISTAKE IT PREVENTS, and the one that replaced it. The instruction was
first printed inside the sampling loop; tool output does not reach the person
until the turn ends, so they read "turn the knob now" after the window had
closed. Result: 185 samples, nothing moved, indistinguishable from a dead
encoder. The next attempt printed the instruction first and counted down --
which fixed nothing, because when this runs in the background its stdout goes
to a log file nobody is watching. The person is reading a chat window, not
this program's output.

So the timing dependency is gone instead of being managed. By default this
WAITS FOR MOTION rather than sampling a fixed window: it polls until the value
changes or --secs elapses (default 300), and returns the moment it sees
something. Whoever is at the bench can act whenever they like; there is no
window to miss and nothing to synchronise. Say the instruction in whatever
channel the person actually reads.

It reports what changed, not merely whether it changed: for /enc it names the
direction of the first motion, which is what settles whether clockwise counts
up on this board.
"""

import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def load(port):
    spec = importlib.util.spec_from_file_location(
        "oscprobe", os.path.join(HERE, "oscprobe.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["oscprobe", port]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    if len(args) < 2:
        print(__doc__)
        return 2
    # One action often exercises more than one address -- pressing the centre of
    # the T-Encoder-Pro's screen is meant to move BOTH /touch and /btn -- and
    # asking a person to repeat the same gesture once per address wastes the
    # scarcest thing here, which is their attention. So every leading argument
    # that starts with "/" is an address to watch, and the first that does not
    # is the instruction.
    port = args[0]
    addrs = [a for a in args[1:] if a.startswith("/")]
    rest = [a for a in args[1:] if not a.startswith("/")]
    if not addrs:
        print(__doc__)
        return 2
    addr = addrs[0]
    what = rest[0] if rest else f"make {' or '.join(addrs)} change"
    secs = 300           # a ceiling, not a window: it returns on motion
    for f in flags:
        if f.startswith("--secs="):
            secs = int(f.split("=", 1)[1])
    direction = "--dir" in flags

    op = load(port)
    Port = next(c for c in vars(op).values()
                if isinstance(c, type) and hasattr(c, "drain"))
    p = Port(port)
    time.sleep(0.3)
    p.drain(0.4)

    def ask(a, argv=()):
        p.write(op.slip_encode(op.bundle([op.msg(a, tuple(argv))])))
        out = []
        for f in op.slip_frames(p.drain(0.3)):
            d = op.decode(f)
            out += d[1] if d[0] == "bundle" else [d]
        return out

    if "/enc" in addrs:
        ask("/enc/zero")

    print(f"\n>>> {what.upper()}", flush=True)
    print(f">>> waiting on {' '.join(addrs)} at {port} for up to {secs}s, "
          f"returning as soon as one moves", flush=True)

    seen = {a: [] for a in addrs}
    end = time.time() + secs
    while time.time() < end:
        for a in addrs:
            for m in ask(a):
                if m[0] != a or not m[1]:
                    continue
                v = tuple(m[1])
                seen[a].append(v)
                if len(seen[a]) > 1 and v != seen[a][0]:
                    end = min(end, time.time() + 3)   # it moved; catch the rest

    rc = 1
    for a in addrs:
        vals = seen[a]
        if not vals:
            print(f"{a}: no reply at all -- the board is not answering it")
            continue
        uniq = {v for v in vals}
        if len(uniq) == 1:
            print(f"{a}: {len(vals)} samples, one value {vals[0]} -- never changed")
            continue
        rc = 0
        scalars = [v[0] for v in vals if len(v) == 1]
        span = f", values {min(scalars)}..{max(scalars)}" if scalars else ""
        print(f"{a}: {len(vals)} samples{span} -- MOVED")
        if direction and scalars:
            moves = [b - x for x, b in zip(scalars, scalars[1:]) if b != x]
            if moves:
                first = "UP" if moves[0] > 0 else "DOWN"
                print(f"      first motion counted {first} -- so the direction "
                      f"you were asked for is "
                      f"{'positive' if moves[0] > 0 else 'NEGATIVE'}")
    if rc:
        print("VERDICT: nothing moved. Either it was not touched, or the pin or "
              "address is wrong.\n         A uniform negative is the apparatus "
              "until a known-good case says otherwise.")
    return rc


if __name__ == "__main__":
    sys.exit(main())

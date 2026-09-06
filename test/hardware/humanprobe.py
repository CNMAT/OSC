#!/usr/bin/env python3
"""Ask a person to do something, then watch for it. The asking comes first.

    python3 test/hardware/humanprobe.py PORT /btn        "press the button"
    python3 test/hardware/humanprobe.py PORT /enc  --dir "turn it clockwise"
    python3 test/hardware/humanprobe.py PORT /light      "cover the sensor" --secs 30

Some things cannot be verified without hands: a photon, a beep, a knob, a
press. This runs the window for them, and exists because getting that wrong
wasted three windows in one session.

THE MISTAKE IT PREVENTS. The instruction used to be printed inside the sampling
loop. Tool output does not reach the person at the bench until the turn ends,
so they read "turn the knob now" after the window had already closed, and the
result -- 185 samples, nothing moved -- looked exactly like a dead encoder or a
wrong pin. Two of those in a row before the apparatus was suspected, which the
global rule about uniform negatives says should have been the first thought.

So: this prints the instruction, flushes it, counts down visibly, and only then
opens the window. Run it in the foreground where the person can see it, or
background it and hand them the instruction yourself BEFORE it starts.

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
    port, addr = args[0], args[1]
    what = args[2] if len(args) > 2 else f"make {addr} change"
    secs = 25
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

    if addr == "/enc":
        ask("/enc/zero")

    # The instruction, before the window, flushed so it cannot sit in a buffer.
    print(f"\n>>> {what.upper()}", flush=True)
    print(f">>> watching {addr} on {port} for {secs}s", flush=True)
    for n in (3, 2, 1):
        print(f"    starting in {n}...", flush=True)
        time.sleep(1)
    print("    GO\n", flush=True)

    seen, end = [], time.time() + secs
    while time.time() < end:
        for m in ask(addr):
            if m[0] == addr and m[1]:
                seen.append(m[1][0])

    if not seen:
        print(f"no reply on {addr} at all -- the board is not answering it")
        return 1
    lo, hi = min(seen), max(seen)
    print(f"{len(seen)} samples, values {lo}..{hi}")
    if lo == hi:
        print(f"VERDICT: {addr} never changed. Either it was not touched, or "
              f"the pin is wrong.\n         A uniform negative is the apparatus "
              f"until a known-good case says otherwise.")
        return 1
    print(f"VERDICT: {addr} moved.")
    if direction:
        moves = [b - a for a, b in zip(seen, seen[1:]) if b != a]
        if moves:
            first = "UP" if moves[0] > 0 else "DOWN"
            print(f"         first motion counted {first} -- so the direction "
                  f"you were asked for is {'positive' if moves[0] > 0 else 'NEGATIVE'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

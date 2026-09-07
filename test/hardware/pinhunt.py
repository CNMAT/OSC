#!/usr/bin/env python3
"""Which pin is that control actually on? Watch many pins; report the one that moves.

    python3 test/hardware/pinhunt.py PORT --chip=esp32s3 --exclude=1,2,3,4,5,6,7,8,9,10,11,12,13,14,17
    python3 test/hardware/pinhunt.py PORT --pins=0,15,16,18,21,38,39,40

This is humanprobe.py's question turned around. humanprobe asks "did /btn
move?" and can only answer yes or no; when the answer is no it cannot tell a
dead switch from a wrong pin number, and a uniform negative is worth very
little. This asks "did ANYTHING move?" over a whole set of candidates at once,
so a press lands somewhere and names its own pin.

WHY IT EXISTS. The T-Encoder-Pro's vendor header says `#define KNOB_KEY 0`.
That define is used nowhere in the vendor's own repository -- not one example
reads it -- so it is a claim, not a measurement, and two 300-second windows on
IO0 saw nothing. Believing a number because it appears in a header is the same
error as believing a port name: BRINGUP.md Phase 0, applied to pins.

SAFETY. Setting INPUT_PULLUP on a pin that carries flash or PSRAM hangs the
chip, and on a pin that is driving something (a display enable, a backlight)
releases it. So there is no "scan everything" mode:

  * `--chip=` supplies the hardware-reserved set for that part, and those pins
    are refused with a reason rather than silently dropped.
  * `--exclude=` is where the BOARD's own wiring goes -- the pins from its
    pin_config -- because only the caller knows those.
  * `--pins=` names candidates outright and still honours the reserved set.

It reads with `/d/<pin>/u`, which the template sketch answers on every board,
so nothing has to be reflashed to run it.

Like humanprobe it WAITS rather than timing a window (default 300s ceiling),
and returns as soon as a pin changes. Say the instruction where the person is
actually reading; this program's stdout may be a log file nobody is watching.
"""

import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

# How long to wait for a group's replies. A press is momentary: at one
# round every 0.5 s a 300 ms press is missed more often than caught, so
# this is as short as the board's turnaround allows, not as long as is
# comfortable. Ask for a press-and-HOLD as well; both, not either.
DRAIN = 0.06

# Pins that are not free to touch, per part, with the reason printed on refusal.
# "Reserved" here means the silicon or the module uses it, not that a board
# happens to have wired something there -- that is what --exclude is for.
RESERVED = {
    "esp32s3": {
        **{p: "SPI flash" for p in range(26, 33)},
        **{p: "octal PSRAM / quad flash" for p in range(33, 38)},
        19: "USB D-", 20: "USB D+",
    },
    "esp32c3": {
        **{p: "SPI flash" for p in range(12, 18)},
        11: "VDD_SPI",
        18: "USB D-", 19: "USB D+",
    },
    "esp32c6": {
        **{p: "SPI flash" for p in range(24, 31)},
        12: "USB D-", 13: "USB D+",
    },
    "esp32s2": {
        **{p: "SPI flash" for p in range(26, 33)},
        19: "USB D-", 20: "USB D+",
    },
    "esp32": {
        **{p: "SPI flash" for p in range(6, 12)},
    },
    "rp2040": {},
    "": {},
}

# The full pin range to consider when --pins is not given.
RANGE = {
    "esp32s3": list(range(0, 22)) + list(range(38, 49)),
    "esp32s2": list(range(0, 22)) + list(range(26, 47)),
    "esp32c3": list(range(0, 22)),
    "esp32c6": list(range(0, 31)),
    "esp32": list(range(0, 40)),
    "rp2040": list(range(0, 30)),
}


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


def parse_list(s):
    out = []
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {}
    for a in sys.argv[1:]:
        if a.startswith("--"):
            k, _, v = a[2:].partition("=")
            flags[k] = v
    if not args:
        print(__doc__)
        return 2

    port = args[0]
    what = args[1] if len(args) > 1 else "press or move the control"
    chip = flags.get("chip", "")
    secs = int(flags.get("secs", 300))
    reserved = RESERVED.get(chip, {})
    if chip and chip not in RESERVED:
        print(f"unknown --chip={chip}; known: {', '.join(k for k in RESERVED if k)}")
        return 2

    if "pins" in flags:
        want = parse_list(flags["pins"])
    elif chip in RANGE:
        want = list(RANGE[chip])
    else:
        print("give --pins= or a --chip= with a known pin range")
        return 2

    excluded = set(parse_list(flags.get("exclude", "")))
    pins, refused = [], []
    for p in want:
        if p in reserved:
            refused.append((p, reserved[p]))
        elif p in excluded:
            pass
        else:
            pins.append(p)
    if not pins:
        print("every candidate was reserved or excluded")
        return 2

    if refused:
        print("refusing (pulling these up hangs or disturbs the part):")
        for p, why in refused:
            print(f"  IO{p:<3d} {why}")
    if excluded:
        print(f"excluded by caller: {','.join(str(p) for p in sorted(excluded))}")

    op = load(port)
    Port = next(c for c in vars(op).values()
                if isinstance(c, type) and hasattr(c, "drain"))
    p = Port(port)
    time.sleep(0.3)
    p.drain(0.4)

    def sweep(group):
        """Ask a group of pins in one bundle; return {pin: value} for replies."""
        payload = op.bundle([op.msg(f"/d/{n}/u", ()) for n in group])
        p.write(op.slip_encode(payload))
        got = {}
        for f in op.slip_frames(p.drain(DRAIN)):
            try:
                d = op.decode(f)
            except Exception:
                continue
            for addr, a in (d[1] if d[0] == "bundle" else [d]):
                if addr.startswith("/d/") and addr.endswith("/u") and a:
                    try:
                        got[int(addr[3:-2])] = a[0]
                    except ValueError:
                        pass
        return got

    groups = [pins[i:i + 8] for i in range(0, len(pins), 8)]

    def read_all():
        got = {}
        for g in groups:
            got.update(sweep(g))
        return got

    base = read_all()
    missing = [n for n in pins if n not in base]
    if missing:
        print(f"no reply for IO{','.join(str(n) for n in missing)} "
              f"-- above NUM_DIGITAL_PINS, or the sketch does not route /d")
    live = [n for n in pins if n in base]
    if not live:
        print("the board answered for no pin at all -- is a sketch running?")
        return 1

    print(f"\n>>> {what.upper()}")
    print(f">>> watching IO{','.join(str(n) for n in live)} at {port} "
          f"for up to {secs}s, returning the moment one changes")
    print("    rest: " + "  ".join(f"IO{n}={base[n]}" for n in live), flush=True)

    end, moved, rounds = time.time() + secs, {}, 0
    while time.time() < end:
        now = read_all()
        rounds += 1
        for n in live:
            if n in now and now[n] != base[n]:
                was, _, hits = moved.get(n, (base[n], now[n], 0))
                moved[n] = (was, now[n], hits + 1)
                # It moved. Keep sampling briefly to count how often, rather
                # than demanding it still be moved -- see the note above.
                end = min(end, time.time() + 2)

    if not moved:
        print(f"\nVERDICT: none of IO{','.join(str(n) for n in live)} changed "
              f"in {secs}s ({rounds} rounds, {rounds / secs:.1f} Hz per pin).")
        print("         Either nothing was touched, the control is on a pin "
              "excluded above,\n         it is not wired to a GPIO at all, or "
              "the press was shorter than a round.")
        return 1

    print(f"\nVERDICT: it moved.  ({rounds} rounds)")
    for n, (was, now, hits) in sorted(moved.items()):
        note = "  ONE SAMPLE ONLY -- brief, or a glitch; repeat it" if hits == 1 else ""
        print(f"         IO{n}: {was} at rest -> {now} when acted on, "
              f"{hits} samples  "
              f"({'active low' if was == 1 else 'active high'}){note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

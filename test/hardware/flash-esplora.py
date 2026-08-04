#!/usr/bin/env python3
"""Flash a Caterina 32U4 board (Esplora, Leonardo, Micro) by racing the
bootloader window. Use it when `arduino-cli ... --upload` fails on such a
board and nothing is obviously wrong.

The diagnostic tell is an avrdude line like

    Programmer id    = built s; type = t

where the "programmer id" is the first few bytes of your *sketch's* serial
output. avrdude reconnected to the running application instead of the
bootloader and is parsing its chatter as protocol replies. You then get
"initialization failed (rc = -1)" and "protocol error for command: leave
prog mode".

Two things make this awkward, both measured on 2026-08-03:

  * The bootloader reuses the SAME tty name as the sketch. On a stock Esplora
    the name comes from the USB location (14400000 -> usbmodem14401), not from
    a serial number, so watching for a *new* port never fires. The port drops
    and returns under its own name ~0.75 s later, and programming has to start
    the instant it returns.
  * The 1200-baud touch only resets the board if DTR is really dropped.
    Opening with CLOCAL set makes termios ignore modem control lines, so a
    plain open-and-close at 1200 baud does nothing whatsoever.

If the port never drops, the firmware does not implement the boot-key
handshake the touch relies on -- third-party firmware with its own USB stack
(non-Arduino VID, or a serial number that changes each boot) typically does
not. Only the physical RESET button opens the bootloader there, and note that
a replug is a power-on reset, which skips it.

    python3 flash-esplora.py [sketch] [build-dir] [port]

Defaults to examples/EsploraOscuino built into /tmp/eo_build.
"""
import fcntl
import glob
import os
import struct
import subprocess
import sys
import termios
import time

TIOCMBIS, TIOCMBIC, TIOCM_DTR = 0x8004746C, 0x8004746B, 0x002


def ver(exe):
    return exe.split('/avrdude/')[1].split('/')[0]


def find_avrdudes():
    """Every avrdude under Arduino15 this machine can actually execute.

    Several versions sit side by side and the older ones can be i386-only --
    6.3.0-arduino9 is, and running it raises OSError 86 'Bad CPU type in
    executable', which is why picking blindly by glob order fails. Each
    avrdude.conf comes from its own directory so the pair always matches.
    Newest first, but the caller should try them all: 8.0.0-arduino1 failed to
    sync with Caterina on a board that 6.3.0-arduino17 programmed fine.
    """
    out = []
    for root in sorted(glob.glob(os.path.expanduser(
            "~/Library/Arduino15/packages/arduino/tools/avrdude/*/")), reverse=True):
        exe = os.path.join(root, "bin", "avrdude")
        conf = os.path.join(root, "etc", "avrdude.conf")
        if not (os.path.exists(exe) and os.path.exists(conf)):
            continue
        try:
            subprocess.run([exe, "-?"], capture_output=True, timeout=10)
        except OSError:
            continue                      # wrong architecture; skip it
        out.append((exe, conf))
    return out


def touch_1200(port):
    """Reset into the bootloader: drop DTR while the line rate is 1200."""
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        a = termios.tcgetattr(fd)
        a[2] = termios.CS8 | termios.CREAD          # no CLOCAL: honour DTR
        a[4] = a[5] = termios.B1200
        termios.tcsetattr(fd, termios.TCSANOW, a)
        fcntl.ioctl(fd, TIOCMBIS, struct.pack('I', TIOCM_DTR))
        time.sleep(0.06)
        fcntl.ioctl(fd, TIOCMBIC, struct.pack('I', TIOCM_DTR))
        time.sleep(0.06)
    finally:
        os.close(fd)


def main():
    sketch = sys.argv[1] if len(sys.argv) > 1 else "examples/EsploraOscuino"
    build = sys.argv[2] if len(sys.argv) > 2 else "/tmp/eo_build"
    name = os.path.basename(sketch.rstrip("/"))
    hexfile = f"{build}/{name}.ino.hex"

    builds = find_avrdudes()
    if not builds:
        sys.exit("no runnable avrdude found under Arduino15")
    print("avrdude:", ", ".join(ver(e) for e, _ in builds))

    if not os.path.exists(hexfile):
        print(f"building {sketch} -> {build}")
        if subprocess.run(["arduino-cli", "compile", "-b", "arduino:avr:esplora",
                           "--output-dir", build, sketch]).returncode:
            sys.exit("compile failed")

    if len(sys.argv) > 3:
        port = sys.argv[3]
    else:
        ports = glob.glob('/dev/cu.usbmodem*')
        if len(ports) != 1:
            sys.exit(f"pass the port explicitly; candidates: {sorted(ports)}")
        port = ports[0]

    others = set(glob.glob('/dev/cu.*')) - {port}
    print(f"touching {port} at 1200 baud")
    touch_1200(port)

    # The bootloader may come back under EITHER name, and which one depends on
    # the board. A stock Esplora is named from its USB location, so the
    # bootloader reuses the sketch's name. A board whose firmware sets its own
    # USB serial gets a different name for the bootloader, and waiting only for
    # the original name misses the whole ~8 s window -- the original then
    # reappears late, and late is the SKETCH, not the bootloader. Take
    # whichever shows up first.
    gone = False
    t0 = time.time()
    while time.time() - t0 < 10:
        fresh = sorted(set(glob.glob('/dev/cu.*')) - others - {port})
        here = os.path.exists(port)
        if not here and not gone:
            gone = True
            print(f"  dropped at {time.time() - t0:.2f}s")
        if fresh:
            port = fresh[0]
            print(f"  bootloader appeared as {port} at {time.time() - t0:.2f}s")
        if fresh or (gone and here):
            if not fresh:
                print(f"  back at {time.time() - t0:.2f}s -- programming")
            time.sleep(0.15)              # let the CDC finish coming up
            # Caterina holds for ~8 s, so there is room for a few goes. Try
            # every runnable avrdude: 8.0.0 can fail to sync on this board
            # where 6.3.0-arduino17 succeeds, and the reverse is plausible.
            for attempt in range(4):
                exe, cfg = builds[attempt % len(builds)]
                r = subprocess.run([exe, "-C", cfg, "-c", "avr109",
                                    "-p", "atmega32u4", "-P", port, "-b", "57600",
                                    "-D", f"-U", f"flash:w:{hexfile}:i"],
                                   capture_output=True, text=True, errors="replace")
                if "bytes of flash written" in r.stderr:
                    print(f"*** FLASHED OK *** ({ver(exe)})")
                    return 0
                why = "; ".join(l for l in r.stderr.splitlines() if 'rror' in l)
                print(f"  {ver(exe)} attempt {attempt + 1}: {why[:110]}")
                time.sleep(0.2)
            return 1
        time.sleep(0.01)

    print("  port never dropped -- the touch did nothing.\n"
          "  That firmware likely has no boot-key handshake; press the physical\n"
          "  RESET button (not a replug) and re-run while it is in bootloader.")
    return 1


if __name__ == "__main__":
    sys.exit(main())

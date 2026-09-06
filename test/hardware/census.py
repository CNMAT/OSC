#!/usr/bin/env python3
"""What is actually plugged in, by identity rather than by port name.

    python3 test/hardware/census.py [--probe]

BRINGUP.md Phase 0 says identify the chip AND the board it sits on, and never
identify by port name. This is that rule as a command. For every USB serial
device it reports the port, what `arduino-cli` thinks it is, and — for ESP32
parts — the chip and MAC that `esptool chip-id` reads out of the silicon.

Why it exists rather than a remembered procedure:

  * Ports renumber on every flash and boards displace each other on a hub. In
    one session a XIAO's port name moved three times, an upload aimed at the
    stale name failed outright, and two boards vanished from the bus entirely.
    Anything that resolves a board by NAME is wrong by the next command.
  * The chip is not the board. Every native-USB ESP32 enumerates as
    303a:1001, so a VID/PID or an `arduino-cli` guess separates an Adafruit
    board from an Espressif one and nothing finer. The MAC is what tells two
    identical-looking ESP32s apart, and it is what BOARDS.md rows carry.

`--probe` additionally asks each port for an OSC `/enq` greeting, in both
framings, so a board already running an Oscuino sketch names itself. That is
the cheapest identification of all and needs no reset.

NOTE: `esptool chip-id` leaves the part in download mode. This runs it only
when asked with --probe=chip, because doing it to a freshly flashed board
stops the sketch you were about to test.
"""

import glob
import importlib.util
import os
import re
import select
import subprocess
import sys
import termios
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ESPTOOL = os.path.expanduser(
    "~/Library/Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool")


def ports():
    """Serial devices, minus the Mac's own Bluetooth and modem ttys."""
    return [p for p in sorted(glob.glob("/dev/cu.*"))
            if not re.search(r"BLTH|Bluetooth|URT[0-9]|Joy-", p)]


def cli_view():
    """What arduino-cli believes, keyed by port."""
    out = {}
    try:
        r = subprocess.run(["arduino-cli", "board", "list"],
                           capture_output=True, text=True, timeout=60)
    except Exception:
        return out
    for line in r.stdout.splitlines():
        if not line.startswith("/dev/"):
            continue
        cols = line.split()
        out[cols[0]] = " ".join(cols[4:7])[:38] if len(cols) > 5 else ""
    return out


def usb_view():
    """VID/PID and serial per USB product name, from system_profiler."""
    try:
        r = subprocess.run(["system_profiler", "SPUSBDataType"],
                           capture_output=True, text=True, timeout=120)
    except Exception:
        return []
    rows, name = [], None
    for line in r.stdout.splitlines():
        m = re.match(r"^\s{2,}([A-Za-z0-9][^:]*):\s*$", line)
        if m:
            name = m.group(1).strip()
        for key in ("Product ID", "Vendor ID", "Serial Number"):
            if key + ":" in line and name:
                rows.append((name, key, line.split(":", 1)[1].strip()))
    merged = {}
    for n, k, v in rows:
        merged.setdefault(n, {})[k] = v
    return [(n, d) for n, d in merged.items()
            if "Vendor ID" in d and re.search(r"0x(239a|2341|303a|2e8a|1a86|2886|16c0|1b4f)",
                                              d.get("Vendor ID", ""), re.I)]


def greet(path, timeout=1.2):
    """Ask for an OSC /enq greeting, trying both framings. Returns a name."""
    spec = importlib.util.spec_from_file_location(
        "oscprobe", os.path.join(HERE, "oscprobe.py"))
    op = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["oscprobe", path]
    try:
        spec.loader.exec_module(op)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    try:
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError:
        return None
    try:
        a = termios.tcgetattr(fd)
        a[0] = a[1] = a[3] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[4] = a[5] = termios.B115200
        a[6][termios.VMIN] = a[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, a)
        for payload in (op.bundle([op.msg("/enq", ())]), op.msg("/enq", ())):
            os.write(fd, op.slip_encode(payload))
            buf, end = b"", time.time() + timeout / 2
            while time.time() < end:
                r, _, _ = select.select([fd], [], [], 0.1)
                if r:
                    try:
                        buf += os.read(fd, 4096)
                    except OSError:
                        break
            for f in op.slip_frames(buf):
                try:
                    d = op.decode(f)
                except Exception:
                    continue
                for addr, args in (d[1] if d[0] == "bundle" else [d]):
                    if addr == "/enq" and args:
                        return args[0]
    finally:
        os.close(fd)
    return None


def main():
    probe = "--probe" in sys.argv
    chip = "--probe=chip" in sys.argv or "--chip" in sys.argv
    cli = cli_view()

    print(f"{'port':32s} {'arduino-cli says':40s} identity")
    for p in ports():
        ident = ""
        if probe or chip:
            name = greet(p)
            if name:
                ident = f"/enq -> {name}"
        if chip and not ident and os.path.exists(ESPTOOL):
            try:
                r = subprocess.run([ESPTOOL, "--port", p, "chip-id"],
                                   capture_output=True, text=True, timeout=90)
                c = re.search(r"Detecting chip type\.\.\.\s*(\S+)", r.stdout)
                m = re.search(r"^MAC:\s*(\S+)", r.stdout, re.M)
                if c:
                    ident = c.group(1) + (f"  MAC {m.group(1)}" if m else "")
                    ident += "   (left in download mode -- `esptool run` to restart)"
            except Exception:
                pass
        print(f"{p:32s} {cli.get(p, ''):40s} {ident}")

    usb = usb_view()
    if usb:
        print("\nUSB, by vendor id (a bridge chip's id belongs to the bridge, "
              "not the board):")
        for name, d in usb:
            print(f"  {name[:34]:34s} {d.get('Vendor ID','')[:6]}:"
                  f"{d.get('Product ID','')[:6]}  {d.get('Serial Number','')}")

    if not (probe or chip):
        print("\n(--probe asks each port for its /enq greeting; --chip also runs "
              "esptool chip-id,\n which leaves the part in download mode)")


if __name__ == "__main__":
    main()

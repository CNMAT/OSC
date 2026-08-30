# OSC over SLIP for python-flavored microcontrollers

The wire contract this library puts on a serial port — OSC 1.0 packets inside
RFC 1055 SLIP frames — does not care what produced the bytes. This directory
carries firmwares that speak it from boards that run Python rather than
compiled sketches, so the same browser pages, the CNMAT Max patches, and
`test/hardware/oscprobe.py` talk to a micro:bit running MicroPython or an
Adafruit board running CircuitPython exactly as they talk to an Arduino.

```
browser page  ⇄  OSC 1.0 packet  ⇄  SLIP frame  ⇄  USB serial  ⇄  main.py / code.py
```

Each target gets a folder holding the firmware and its matched Web Serial page:

```
MicrobitOscuino/main.py                    ← copy ALL FOUR .py files onto a
MicrobitOscuino/slip.py                       micro:bit running MicroPython
MicrobitOscuino/osce.py
MicrobitOscuino/oscd.py
MicrobitOscuino/MicrobitOscuino.html       ← open this
CircuitPythonOscuino/code.py               ← copy to the CIRCUITPY drive
CircuitPythonOscuino/boot.py               ←   … together with this
CircuitPythonOscuino/CircuitPythonOscuino.html
```

Serve the page over `http://localhost` or `https://` (Web Serial refuses
`file://`), e.g. `python3 -m http.server 8000` in the folder.

## The wire contract, spelled out

A port to any further runtime needs exactly this list; everything else is
convention, not protocol.

- **SLIP, RFC 1055**: `END` 0xC0 terminates a frame, `ESC` 0xDB escapes, with
  `ESC ESC_END` (0xDB 0xDC) for a literal END and `ESC ESC_ESC` (0xDB 0xDD)
  for a literal ESC.
- **Double-END framing**: send an END before the packet as well as after, as
  CNMAT's `beginPacket()`/`endPacket()` do. Decoders must skip the empty
  frames that leading ENDs (and the browser's ZLP-avoidance padding) close.
- **OSC 1.0**, big-endian, 4-byte aligned: types `i f s b` with payloads,
  `T F N I` as zero-width tags, `h d t` tolerated on receive by skipping
  their known 8 bytes. An *unknown* tag has no known width, so parsing stops
  there rather than misparsing what follows.
- **Bundles both directions**: traffic travels as `#bundle` with the immediate
  timetag (7 zero bytes then 0x01), which is what the stock Oscuino clients
  expect. Receivers must flatten nested bundles.
- **The standard Oscuino address space**: `/d/<pin>`, `/d/<pin>/u`,
  `/a/<pin>`, `/tone/<pin>`, `/s/m` `/s/d` `/s/a` `/s/l`, announced by one
  `/hello <name>` at startup.

The python firmwares add `/s/q` — exit the program, back to the REPL — because
on an interpreter that is a meaningful, useful thing to do, and there is no
reset button worth pressing mid-session.

## micro:bit (MicroPython)

Get MicroPython onto the board once (python.microbit.org flashes it as a side
effect of sending a program; `uflash` does it from a shell — and if flashing
fails with DAPLink's `type: target` error, the nRF51 is protected and needs a
`pyocd erase --chip` first, measured on a V1.3). Then copy **all four**
`.py` files across — the online editor's project view, Mu, or
`ufs put` each.

Four files is not a style choice; it is the V1's 16 KB heap, measured on the
board (2026-08-26): the on-device compiler needs ~2.1× a file's source bytes
to parse it, the boot file gets the largest budget, and every runtime import
gets what earlier files left behind. One 9 KB file dies in `MemoryError`, so
does every two-file split tried; `main.py` plus three ~1 KB codec modules
boots. For the same reason the deployed files are squeezed to bare code by
the generator — the readable, commented sources are the
`extras/webserial/template-microbit*.py` templates.

Pin numbers mean micro:bit pins: `/a/0` reads the pin0 alligator pad, analog
exists on 0–4 and 10, and the buttons/accelerometer/display arrive under
`/mb/…` (see the header of `template-microbit.py`). The DAPLink USB bridge
carries a real UART, so the browser's baud must stay at 115200 to match
`uart.init()`. Opening the port does not reset the board, so press reset
after connecting if you want the `/hello`.

Traps this firmware already avoids, recorded because any rewrite must keep
avoiding them, all measured on a V1.3 running MicroPython v1.9.2:
`micropython.kbd_intr(-1)` is called because 0x03 is a legal byte inside a
SLIP frame, not ctrl-C — an integer argument of 3 would otherwise kill the
program mid-packet. The SLIP decoder keeps its state *across* reads, because
a frame almost never arrives in one read. The serve loop reads through a
preallocated buffer with `uart.readinto()`, because `uart.read()` allocates
256 bytes per call and dies on the post-compile heap. And strings convert
via `bytes(s, 'utf8')` / `str(b, 'utf8')`, because v1.9.2 has no
`str.encode` or `bytes.decode`.

`/s/q` exits to the REPL. On hardware,
`test/hardware/microbitprobe.py` covers the `/mb` extensions, the display,
a literal 0x03 payload, and that escape hatch — alongside the standard
`oscprobe.py` suite, which this board passes 7/7.

## CircuitPython (Adafruit boards)

One `code.py` covers the catalogue: pins resolve by name at runtime
(`/d/13` is `board.D13`, `/a/0` is `board.A0`, `/s/l` is `board.LED`), so
there is nothing board-specific to generate. Analog reads are 16-bit
(0..65535) — CircuitPython normalises every ADC to that range, where an
Arduino sketch reports the converter's native width.

`boot.py` matters: it enables the second CDC serial port (the `usb_cdc.data`
channel) at enumeration time, and that second port is the one to pick in the
browser. Binary SLIP cannot share the console channel, where 0x03 reads as
ctrl-C. Without `boot.py`, `code.py` falls back to the console and works —
until the first frame that contains 0x03.

## Fruit Jam (CircuitPython) — and its Arduino twin

`FruitJamOscuino/` covers Adafruit's Fruit Jam (RP2350B) with the board's own
hardware on top of the standard set: `/fj/b` reads the three buttons
(1 = pressed), `/fj/led` drives the five NeoPixels (three ints = all, four =
index first; values echo back because probes cannot see photons), `/fj/beep`
plays a sine through the TLV320 codec into headphones and speaker, and
`/fj/t` prints a line on the DVI output — the firmware brings the display up
itself with the builtin `picodvi` and the console terminal lands on the
monitor. Deploy = copy `code.py`, `boot.py` **and the `lib/` folder** (it
vendors the official `adafruit_tlv320` driver, MIT, from
Adafruit_CircuitPython_TLV320 — replace that file to update it) to
CIRCUITPY, reset, and pick the board's second serial port.

Hardware notes that cost real time, measured 2026-08-27 on CircuitPython
10.2.1: the codec shares its I2C bus with the DVI connector's DDC lines, and
a wedged bus object NACKs every multi-byte write while still ACKing address
probes — `code.py` therefore creates the bus fresh with `busio.I2C` and
retries; `PERIPH_RESET` stays untouched (the codec comes up fine, and the
pin also resets the ESP32). The factory firmware has no REPL but honours
the 1200-baud touch into the UF2 bootloader — and if a firmware ever wedges
before USB comes up, front button #1 doubles as BOOT: hold it, tap reset.

The same board has a hand-written Arduino sketch,
`examples/FruitJamOscuino/`, answering the same addresses through the same
generated page (the generator writes the page beside both). One firmware
occupies the board at a time; `test/hardware/fruitjamprobe.py` verifies
either.

## Generated files — edit the templates, not these

`main.py`, `code.py`, `boot.py` and the `.html` pages are generated, exactly
like the Arduino Web Serial pairs and by the same machinery:

```bash
cd ../webserial
make generate     # rewrite from template-microbit.py / template-circuitpython.py
make check        # fail if anything here drifted from its template
```

A board entry in `../webserial/boards.json` whose `firmware` field names a
runtime renders here instead of into `examples/`, which keeps the Arduino IDE
from listing a sketch-less folder.

## Verification

`test_host.py` (standard library, no hardware) imports the two shipped
firmware files under CPython and cross-checks their codecs against the two
references this repo already trusts: byte-for-byte against
`test/hardware/oscprobe.py`, and strict-decode against `test/host/oracle.py`,
including SLIP reassembly at every chunk boundary. It is also the check that
keeps the two templates' deliberately identical codec sections from drifting
apart — the same job `check.mjs` does for the browser pages.

```bash
python3 extras/python/test_host.py
```

On hardware, `oscprobe.py` is firmware-agnostic — point it at the port and it
runs the same probes it runs against Arduino boards:

```bash
python3 test/hardware/oscprobe.py /dev/cu.usbmodemXXXX
```

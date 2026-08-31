# Bringing up OSC on a new board

The path from "a board just got plugged in" to "it is in [BOARDS.md](./BOARDS.md)
with honest numbers", in phases. Two flows share phases 0–3:

* **Transport flow** — the board talks OSC over SLIP and proves it. Ends at
  phase 3. This is the minimum for a BOARDS.md row.
* **Demo flow** — the board has built-in peripherals worth showing (screen,
  microphone, camera, IMU, buttons, LEDs), so it also gets an
  `examples/XxxOscuino` sketch and a Web Serial page that draws the board
  and its live state. Phases 4–5.

One rule governs everything: **verify, don't assert.** Numbers come from
measurements run under the Method rules
([test/hardware/README.md](./test/hardware/README.md)); code that has not
run on hardware says so in its own comments; a claim without a mechanism is
a hypothesis, not a finding. Most of the traps below are listed because
each one cost real bench time.

## Phase 0 — identify what is actually plugged in

Do not trust the label, the shelf, or the person. Boards displace each
other on hubs, ports renumber after every reflash, and bootloaders lie.

```sh
arduino-cli board list                       # port + FQBN guess
system_profiler SPUSBDataType | grep -A4 -i "product id"   # VID/PID truth
lsof /dev/cu.usbmodem*                       # who is holding the port
```

* VID tells the family: `0x2341` Arduino, `0x239A` Adafruit, `0x303A`
  Espressif, `0x2E8A` Raspberry Pi. Teensy enumerates under `0x16C0` — but
  that is the VOTI shared VID used by many V-USB devices, so treat it as a
  hint, not an identification.
* For an ESP32, `esptool --port PORT chip_id` names the exact chip
  read-only. A board sold as one thing has enumerated as another in this
  very repo's history.
* A UF2 bootloader's `INFO_UF2.TXT` names the bootloader's board, which is
  **not** the model — an EdgeBadge can carry a PyBadge bootloader.
* Find the variant files: `boards.txt` for FQBN menu options and defaults,
  `variant.h`/`variant.cpp` for the pin map. The variant's own comments
  ("SPI for PDM mic") are the wiring documentation.

## Phase 1 — compile

```sh
arduino-cli compile -b VENDOR:ARCH:BOARD test/hardware/OscEcho
```

* The USB detection ladder in `SLIPEncodedSerial.h` decides whether
  `SLIPEncodedUSBSerial`/`thisBoardsSerialUSB` exist
  (`BOARD_HAS_USB_SERIAL`). A new core may need a new rung; the moddo
  pinch and TinyUSB rungs show the shape.
* ESP32 boards need per-board FQBN options and they do not transfer
  between boards: XIAO S3 wants stock defaults, C3/C6 want
  `:CDCOnBoot=cdc`, QT Py S3 wants `:USBMode=hwcdc,CDCOnBoot=cdc`. Read
  `boards.txt`, don't guess — the failure mode is a board that flashes,
  verifies, and says nothing.
* Classic AVR has no `<type_traits>`; keep template tricks out of code
  that must build there. `int` is 16 bits — that is what `IntWidths`
  exists to catch.
* Parallel builds for several boards need `--build-path` per board; the
  cache is keyed on sketch path alone.

## Phase 2 — flash

The per-family procedures that were learned the hard way are in
[BOARDS.md](./BOARDS.md) under *Flashing procedures*. The general rules:

* After every flash, re-run `arduino-cli board list` — the port name has
  probably changed.
* "Verify successful" is not universal; some uploaders say "done" or
  print a hash. Grep for the tool's actual success line, or better, check
  the board answers afterwards.
* An upload that reports success can still leave the old sketch running
  (seen on UNO R4 WiFi). If behaviour looks impossible, confirm which
  sketch is actually running before debugging it — the trickle gate in
  phase 3 doubles as that check.
* If uploads fail with "no device", check `lsof` — a Web Serial page in a
  browser holds the port exclusively.
* A *compile* that succeeds and an *upload* that then cannot find its own
  artifact usually means a packaging recipe failed quietly. The Seeed nRF52
  platform shells out to `python`, which modern macOS does not ship: the
  `.hex` appears, the `.zip` never does, and the upload's complaint names
  only the missing file. Put a `python` → `python3` shim on PATH.

### Boot modes — know which one the chip is in before debugging anything

A board is always in exactly one of these, and a large share of "dead
board" hours in this repo's history were spent debugging an app while the
chip was in a different mode entirely:

1. **App mode** — the sketch runs.
2. **ROM download / DFU mode** — a mask-ROM loader speaks a flashing
   protocol. USB usually enumerates and looks perfectly healthy; nothing
   you flashed runs. Fingerprint: the flasher always connects instantly,
   the app is totally silent, and there is no bootloop spam.
3. **UF2 / mass-storage bootloader** — a drive appears (`RPI-RP2`,
   `RP2350`, `xxxBOOT`); the volume name identifies the *bootloader*, not
   the board model.
4. **Interface firmware**, on bridged boards — the interface chip has its
   own app/bootloader split (micro:bit DAPLink's `MICROBIT` vs
   `MAINTENANCE` drives), entirely independent of the target chip's state.

Entry and exit gestures, per family, as measured here:

* **RP2040 / RP2350** — BOOTSEL held through power-on → UF2 drive; the
  1200-baud touch does the same *from a live CDC app* (arduino-pico,
  CircuitPython, even a TinyUSB factory demo honoured it). A wedged app
  that never brings up USB leaves only the button — which may be shared:
  on the Fruit Jam, front button #1 doubles as BOOT. Exit: flash a UF2.
* **ESP32 family** — GPIO0 (classic/S2/S3) or GPIO9 (C3/C6) strapped low
  at reset → ROM download mode. esptool enters it with a DTR/RTS dance
  where those lines actually reach reset and strap; on bare
  USB-Serial-JTAG boards the post-flash "Hard resetting via RTS pin" can
  be a no-op — **the chip stays parked in download mode and the freshly
  flashed app never starts**. `esptool --port PORT run` after the upload
  boots the app (measured — it turned every later flash cycle
  autonomous); so does a physical replug. Some boards additionally want
  BOOT held *while plugging in* to reach download mode for flashing
  (measured on the EGG C3, GPIO9). Three "flashed, verified, says
  nothing" C3 flash cycles here were this, not the firmware.
* **ATmega32U4 Caterina** — 1200-baud touch → an 8-second bootloader
  window on a *renamed* port; race avrdude against it (procedure in
  BOARDS.md). Some boards ignore the touch (Circuit Playground Classic):
  physical reset, then the same race.
* **SAMD** — double-tap reset → UF2 bootloader, under a *different USB
  PID* (record both in `usbFilters`).
* **micro:bit** — target flashing via the `MICROBIT` drive or WebUSB both
  fail with DAPLink's `type: target` error when the nRF51 arrived
  flash-protected; recovery is a `pyocd erase --chip` over CMSIS-DAP,
  which no drive or button reaches. Interface firmware updates go through
  `MAINTENANCE` (reset held while plugging).
* **MicroPython boards** — when the 1200-baud touch does nothing, ask the
  REPL: `machine.bootloader()` (PicoBricks).
* **STM32** — BOOT0 strap, or give the sketch a `/dfu` escape address so
  no strap juggling is needed (F103/F407 rows).

Identification is asking the chip, never reasoning from what you flashed:
`esptool chip_id` connecting + silent app = download mode; a UF2 volume
name = which bootloader; DAPLink's `DETAILS.TXT` = interface state; a
port that echoes keystrokes is a REPL, not your sketch. And the phase-3
trickle gate doubles as proof of *which* sketch is actually running.

## Phase 3 — verify the transport

In this order; each stage assumes the previous one passed.

```sh
python3 test/hardware/echotest.py PORT        # flash OscEcho first: 22 byte-exact cases
python3 test/hardware/widths.py PORT          # flash IntWidths: integer dispatch on-target
python3 test/hardware/oscprobe.py PORT        # flash an Oscuino sketch: 7 liveness probes
# then flash OscBench:
python3 test/hardware/bench.py PORT verify    # trickle gate — mandatory first
python3 test/hardware/bench.py PORT in 50 -1  # one-write burst, host->device
python3 test/hardware/bench.py PORT out 200 0 # device->host flood
python3 test/hardware/bench.py PORT compound  # both ends counted under echo load
python3 test/hardware/bench.py PORT ring 20   # ring map vs a lazy reader
```

* The bench attributes loss to a segment (host wrote / board received /
  board sent / host received). "Lost somewhere" is not a result.
* Place the board's USB stack in the family table in BOARDS.md. A NAK
  stack should run everything clean; a drop stack shows a byte ceiling in
  `ring`; a shared-pool stack shows `compound` loss with clean separate
  directions. If a new stack fits none of these fingerprints, that is a
  finding — mechanism first, then the table.
* Record per the Method rules: gate first, three repeats, same-day
  reference board, mechanism named. No number without all four.

## Phase 4 — the example sketch (demo flow)

`examples/XxxOscuino/XxxOscuino.ino`, named for the board. Conventions
from the ones that exist (PyBadge, XiaoS3Sense, Esplora):

* **`/hello` first**: name plus a boolean per optional peripheral
  (`accelOK`, `micOK`, `cameraOK`). The page uses these to hide panels, so
  a lesser variant of the board degrades instead of failing — this is how
  one sketch serves both PyBadge (no mic) and EdgeBadge (mic).
* **Probe capabilities at runtime, not compile time**, when variants share
  an FQBN. And probe the *signal*, not the driver: `begin()` succeeding
  proves the bus exists, not the part — the EdgeBadge mic check listens
  100 ms and requires the samples to actually move, because the SERCOM is
  present on badges with no microphone soldered to it.
* **Respect driver contracts.** Some drivers are ISR-shaped:
  `Adafruit_ZeroPDMSPI::begin()` arms an interrupt at priority 0, and
  leaving its handler undefined vectors into the default trap — the board
  hangs in `setup()` having sent nothing. Read the driver's own example
  before wiring it in.
* **Integer DSP scaling, measured lessons**: accumulate squares in
  `uint64` (a pre-shift like `(v>>5)²` or `(s*s)>>16` silently zeroes
  quiet signals); divide in float (`sumsq/count` in integer truncates a
  quiet room to nothing); normalise a decimated scope to the *scope
  window's* peak, not the frame's (the frame peak lives in samples the
  scope skipped); send levels at full scale and let the display do dBFS;
  calibrate gain by measuring the clip point, then back off.
* **Pace everything** with `millis()` scheduling and accept `/xxx/rate`
  messages; never block in `loop()`.
* **Wire-format care**: pad blobs to 4 bytes; on 32U4 never let a SLIP
  frame land on a 64-byte multiple (`padAwayZLP` on the page side); on
  ESP32 the library already enlarges the receive ring
  (`OSC_SLIP_RX_BUFFER`).
* Anything unverified ships with a STATUS comment saying exactly what has
  and has not run. The XiaoS3Sense mic block is the template.

## Credentials never enter the repository

A WiFi example needs an SSID and a password, and the safe place for them is
not the sketch. The pattern here, on the XIAO C6 WiFi example:

* `arduino_secrets.h` holds `SECRET_SSID` / `SECRET_PASS` and is
  **git-ignored**; `arduino_secrets.h.example` is the tracked template.
* The sketch pulls it in with `#if __has_include("arduino_secrets.h")` and
  falls back to placeholders, so it still compiles in CI and for anyone who
  has not made one.
* `.git/hooks/pre-commit` (install with `sh tools/install-hooks.sh`) refuses
  any commit that stages an `arduino_secrets.h`, or that adds a line
  assigning a non-placeholder value to an ssid/password/passphrase/psk name
  in *any* file — `.gitignore` alone does not cover `git add -f`, nor a
  password pasted somewhere else.

**Test the guard by attacking it.** The first version of that hook filtered
diff lines with `grep -E '^\+' | grep -v '^\+\+\+'`, which is not portable:
BSD grep rejects an empty alternative in an ERE, and the `grep` on this
machine is ugrep, which rejects `^\+\+\+` outright. Either way the grep
exited non-zero, matched nothing, and the hook **allowed** the commit. A
guard that fails open is worse than no guard, because it gets trusted. Both
filters are awk substring comparisons now, and the check is five deliberate
attacks — force-added secrets file, password in a sketch, in a Python
helper, in a YAML file, plus a control that placeholders still commit.

## Phase 5 — the web page (demo flow)

`examples/XxxOscuino/XxxOscuino.html`, self-contained, no dependencies.
The generated pages in `extras/webserial/` cover simple boards; a board
with rich peripherals gets a hand-written page (and then a contract test
tying it to its sketch, since `make check` cannot see hand-written pages —
`extras/webserial/test/test-cpx-contract.mjs` is the pattern).

* **Serving**: Web Serial needs `http://localhost`, never `file://`, and
  Chrome caches hard — serve with `Cache-Control: no-store` or edits will
  silently not appear. One page connection holds the port; disconnect
  before flashing.
* **Decode everything the sketch can send.** The page's OSC decoder must
  handle every tag in use — `i f s b h d T F N I` — because an unknown tag
  aborts the argument list. A missing `b` case cost this repo a waveform
  display that had never drawn once, silently.
* **Draw the board to scale** (SVG, viewBox in mm×10) with controls where
  the physical controls are, and mirror outbound state — an emulated
  screen that renders what `/screen/text` sent, a backlight that dims with
  `/screen/backlight`. The page is the board's mirror, not a dashboard.
* **Instrument like an instrument**: dBFS meters with a floor (a linear
  bar pins a −64 dBFS room at the left stop), peak-hold with the clip
  indicator latched to the *held* peak, shape-only scopes labelled as
  such, level history over time.
* **Hide what isn't there**, driven by `/hello`'s booleans.

## Phase 6 — record

* BOARDS.md gets the row (chip, suites run, findings with mechanisms) and
  any new flashing procedure.
* test/hardware/README.md gets the measurement detail if the board taught
  something new about its stack family.
* Commit messages state what was measured, what the mechanism is, and what
  is *not* verified — the repo's history is part of the record.

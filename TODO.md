# Release work list

Everything in the tree that is deferred, unverified, or dead, assembled in one
place. Each entry says what is actually outstanding and where the edit goes.

Audited 2026-08-25 against `af33522`, re-verified after PR #164 merged and the
STM32 work landed. Every line reference below was re-checked against the tree
rather than carried over from the previous pass.

---

## 1. Dead code — the guards that could never be true  ✅ FIXED 2026-08-23

Both are done. Kept here as the record of what changed and what proves it.

### 1.1 `TEMPoraray` — deleted

`examples/SerialReceivewithServo/SerialReceivewithServo.ino`

Misspelt symbol, defined nowhere, so the `else if (msg.isFloat(0))` branch had
never been compiled by anyone. Fixing the spelling would not have helped: the
body called `SoftPWMSet()` from the SoftPWM library, which the sketch does not
include and never has. The branch had no working form, so the 13 lines went.

Verified: compiles for `arduino:avr:uno`, 10800 B (33 %).

### 1.2 `TOUCHSUPPORT` → `BOARD_HAS_CAPACITANCE_SENSING` — `/c` now works

`examples/UDPOscuino/UDPOscuino.ino:366`

This was `BOARD_HAS_TONE` (see `OSCBoards.h:34`) repeated exactly: a handler
guarded by a name nothing defines. No new code was needed — `routeTouch()` and
its three Teensy pin tables were **already** correctly wrapped in
`#ifdef BOARD_HAS_CAPACITANCE_SENSING` at lines 238–270, and the library
**already** defines that macro for the right parts at `OSCBoards.h:15`. Only the
route registration used the phantom name, so the handler compiled in and was
then never reachable. One word.

Proved on the linked binary rather than assumed — Teensy 3.2, `arm-none-eabi-nm`:

| build | flash | `routeTouch` / `touchRead` |
|---|---|---|
| as shipped (`TOUCHSUPPORT`) | 36148 B | **absent** — linker dropped both, nothing referenced them |
| fixed | 36620 B | **present** (+472 B) |

Compiled across every branch of the pin table, and the negative cases:

| board | result |
|---|---|
| Teensy 3.0 (`__MK20DX128__`) | 35716 B, `/c` routed |
| Teensy 3.2 (`__MK20DX256__`) | 36620 B, `/c` routed |
| Teensy 3.6 (`__MK66FX1M0__`) | 36480 B, `/c` routed |
| Teensy 3.5 | 34384 B, `/c` correctly absent — see 1.3 |
| `arduino:avr:uno` | 21446 B, guard compiles out clean |

Still unverified: `/c` has not been exercised **on hardware**. It builds and it
is routed; no pad has been touched. Add to the bench list in §3.

### 1.3 Teensy 3.5 is locked out of `/c`, and the silicon disagrees — **OPEN**

`OSCBoards.h:13`

The `BOARD_HAS_CAPACITANCE_SENSING` list covers `__MK20DX128__`,
`__MK20DX256__`, `__MKL26Z64__` and `__MK66FX1M0__` — Teensy 3.0, 3.1/3.2, LC
and 3.6. It omits `__MK64FX512__`, the Teensy 3.5.

Measured 2026-08-23: a `touchRead(A0)` probe **compiles for
`teensy:avr:teensy35`**, so the function is there and the omission looks like an
oversight rather than a fact about the part.

Not fixed here, because adding `__MK64FX512__` alone would be wrong: the
`#else` branch of the pin table at `UDPOscuino.ino:248` would hand the 3.5 the
**Teensy 3.1/3.2 touch pin list**, which is not the 3.5's. Doing this properly
means adding a `__MK64FX512__` table from PJRC's pinout, and that wants a board
to check against. Same shape as the bug above: a guard that does not match the
hardware.

---

## 2. The one library-gated opt-in — **not stale, keep it**

`examples/CircuitPlaygroundSensors/CircuitPlaygroundSensors.ino:131`

```c
//#define CPX_MIC 1
```

This is the only "uncomment after installing a library" note in the tree. It is
deliberate, and it still works:

- **The library is live.** `arduino-cli lib search` on 2026-08-23 returns
  *Adafruit Zero PDM Library*, versions 1.0.0 / 1.1.2 / 1.2.0 / 1.2.1 / 1.2.3 /
  1.2.4. The dependency has not been renamed or withdrawn.
- **Off by default is the right default**, for the reason the header gives: the
  sketch builds unchanged without it and reports sound as `-1`, so the example
  is not a front door that fails to compile on a fresh machine.
- The sketch is documented as compiling with `CPX_MIC` **both off and on**.

No change wanted. The outstanding item is verification, in 3.1 below.

Related, and worth deciding once for the whole release: `PyBadgeOscuino` takes
the opposite policy — `BADGE_HAS_PDM_MIC` defaults to **1**
(`PyBadgeOscuino.ino:75`), so that sketch requires the same Adafruit package
unconditionally. Two sketches, two policies for one dependency. Pick one.

---

## 3. Unverified on hardware

The library's own rule (`BRINGUP.md:140`) is that anything unverified ships with
a STATUS comment saying exactly what has and has not been seen. These are those
comments, collected. None is stale; each is a real measurement not yet made.

### 3.1 Circuit Playground Express — three open items
`CircuitPlaygroundSensors.ino:106–119`

- **The microphone.** Needs `CPX_MIC` plus the library above. The `-1` reported
  when compiled out is what was observed; the mic path itself has never run.
- **Which side of the slide switch reports 1.** Both states were seen, but
  nothing ties either to the silkscreen.
- **Thermistor divider orientation.** A plausible room temperature does not
  prove it — a divider wired the other way still yields a number, and near the
  nominal 25 °C a sign error is small. The NeoPixel self-heating experiment
  settled nothing (+0.2 °C, same as ambient drift). **Test: hold a finger on the
  sensor and confirm `tempC` RISES.**

### 3.2 M5Stack NanoC6 — never seen to answer
Note lives in `extras/webserial/boards.json:133`. Compiles only; the test board
stopped running any sketch part way through bringup (ROM bootloader still
answers esptool, applications produce no output, under every FQBN tried
including ones that had worked on it earlier). **Needs a second unit.**

> Edit `boards.json`, not the sketch — see §6.

### 3.3 XIAO ESP32-C6 WiFi — the whole radio path
`examples/XiaoC6ExpWiFi/XiaoC6ExpWiFi.ino:37–42`

Compiles clean at 81 % of flash and shares its OSC handlers with the serial
twin, which *is* verified. Never run: association, the UDP listener and its
reply to `Udp.remoteIP()`, the three HTTP routes and their CORS headers, and the
IP shown on the OLED. No credentials were available on the bench.

This one has already bitten once: the header's untested warning is exactly what
allowed the `http.arg("plain")` NUL-truncation bug that left the entire browser
path dead (`XiaoC6ExpWiFi.ino:257`). Treat the remaining untested list as live
risk, not paperwork.

### 3.4 The four stock WiFi examples — not run, one branch not even compiled
`WiFiEcho.ino:16`, `WiFiSendMessage.ino:12`, `WiFiSendBundle.ino:15`,
`WiFiReceiveMessage.ino:14`

Compiled with arduino-cli 1.5.1 for UNO R4 WiFi, Nano 33 IoT, Portenta C33 and
ESP32. **The ESP8266 branch has never been compiled** — that core was not
installed on the development machine — and none of the four has run on hardware.

Installing the ESP8266 core clears the compile half of this without any board:

```
arduino-cli core install esp8266:esp8266 \
  --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

### 3.5 Atom JoyStick — button mapping not confirmed by press
`examples/AtomJoyOscuino/AtomJoyOscuino.ino:123`

All four button bits and the front button were each seen to assert, but which
physical control drives which bit is not established: the one run that produced
an order was contaminated by stick clicks during a sweep. The mapping in the
sketch is the *firmware's*, which is a good source but is not a measurement.

Partly overtaken by `27f28eb`, which established that the AtomS3's **screen is
the front button** (GPIO41) and fixed the page. The four stick-side bits remain
unconfirmed. Redo with the sticks untouched.

### 3.6 PicoMate — LTR-381 colour channels uncalibrated
`examples/PicoMateOscuino/PicoMateOscuino.ino:59`

R/G/B channel counts read very low at the library's default gain while lux is
sensible. Treat the raw colour ratio as uncalibrated. Needs a gain sweep.

### 3.7 PyBadge — `MIC_GAIN` default unverified
`BOARDS.md:56`

Mic measured at −64 dBFS quiet floor, 26.6 dB range to speech, clipping at
`MIC_GAIN` 16. The default was then changed to 8, and 8 has not been measured.

### 3.8 M5Dial — RFID  ✅ DONE 2026-08-24 (`f101293`)

Implemented after this list was written, so the entry that stood here is
obsolete. The WS1850S turns out to be register-compatible with the MFRC522 and
is driven over M5Unified's own internal bus. A tag arriving streams `/rfid T`
with its UID; silence for two polls streams `/rfid F`. The RC522's own timer
bounds every transceive at ~7 ms, so a missing tag cannot stall the loop, and
`/hello` gained an `rfidPresent` flag. Encoder direction was measured in the
same commit.

`M5DialOscuino.ino:34` now documents the driver rather than its absence.

### 3.9 `/c` cap-touch on Teensy — routed, never touched

`examples/UDPOscuino/UDPOscuino.ino:366`

Enabled in 1.2 and confirmed present in the linked binary for Teensy 3.0, 3.2
and 3.6, but no pad has been touched. Needs one Teensy 3.x with a wire on a
touch pin: send `/c/<pin>` and confirm the returned count rises on contact.

### 3.10 STM32 Blue Pill (F103C6) — compile-fit only, never run

`BOARDS.md:108`

`OscEcho` + CDC fits at 24,076 B (73 % of the 32 KB part), RAM 46 %, and that is
the whole of what is known. Not run: the F103 ROM boots USART1 only, so there is
no USB DFU path, and the board is unflashed pending an ST-Link or serial
adapter. Clone Blue Pills are also notorious for a wrong D+ pull-up that breaks
CDC enumeration even with correct firmware, so a failure here would need that
ruled out before it meant anything about the library.

Landed in `dbff6dd`, after this list was first written.
---

## 4. The STM32 queue remedy is not in any example

`test/hardware/{OscBench,OscEcho,IntWidths}/build_opt.h`

stm32duino's CDC receive queue is 3 × 64 B = **192 bytes**, and it does not merely
back-pressure on overflow — it **drops, with mid-frame corruption**. Measured on
the F407: 110 B bursts clean, 220 B bursts truncate with `seqErrs` and `crcErrs`.
The remedy is one line, `-DCDC_RECEIVE_QUEUE_BUFFER_PACKET_NUMBER=64`, and with it
the whole bench runs clean.

That line currently exists in exactly three places, all of them under
`test/hardware/`. **No sketch in `examples/` carries it.** So the instruments
prove the library is sound on STM32 while every example a user actually opens
ships the stock 192-byte queue and will drop under burst — and drop silently,
since corruption is what the failure looks like.

Three ways out, in ascending order of how much they ask of the user:

- a `build_opt.h` beside each example, which is per-sketch duplication Arduino
  offers no way to share;
- a note in the STM32 section of the README and in each affected example header,
  which is honest but leaves the default broken;
- treat it the way the HWCDC fix was treated and handle it in the library, if
  the queue can be sized from `SLIPEncodedSerial` rather than from the sketch.

Worth deciding before release rather than after the first STM32 bug report. This
is the **third** member of the drop family — after ESP32 HWCDC and the R4 WiFi's
bridged UART — and `BOARDS.md:31–37` now tabulates all three, so the pattern is
documented even though the default is not fixed.

---

## 5. Coverage gap behind all of the above

CI compiles **12 of the 51 example sketches** (`.github/workflows/ci.yml:44–95`).
The 39 outside it include every board-specific `*Oscuino`, all four WiFi
examples, and `CircuitPlaygroundSensors` — i.e. exactly the sketches whose
STATUS comments appear in §3.

Most need no hardware to *compile*. Adding the installable ones to the matrix
would stop this list growing while nobody is at the bench.

---

## 6. Where to make the edit — 7 sketches are generated

These are written by `extras/webserial/generate.mjs` and will be overwritten;
`make check` fails first, so a direct edit is caught rather than silently lost:

```
ESP32S3Oscuino  GemmaOscuino  LilyPadOscuino  M5NanoC6Oscuino
PlaygroundOscuino  RP2040Oscuino  TeensyOscuino        (.ino and .html both)
```

Edit `extras/webserial/boards.json` (board facts and the per-board `note`) or
`extras/webserial/template.ino` / `template.html` (everything shared), then:

```bash
cd extras/webserial && make generate && make all
```

`CircuitPlaygroundSensors`, `AtomJoyOscuino`, `M5DialOscuino`, `PicoMateOscuino`,
`PyBadgeOscuino`, `XiaoC6ExpWiFi` and the `WiFi*` examples are hand-written —
edit those directly.

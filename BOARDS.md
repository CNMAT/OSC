# Hardware record

Every board and chip this library has actually run on, what ran, and what
was found. "Tested" here has a definition: the suite named in each cell was
executed on that physical board, and any loss or throughput claim carries a
named mechanism, per the Method rules in
[test/hardware/README.md](./test/hardware/README.md). Compile-only coverage
is listed separately and never mixed into the measured rows.

The suites, briefly:

* **echo** — `OscEcho` + `echotest.py`: 22 byte-exact round-trips covering
  every OSC type, string/blob edge lengths, bundles and timetags.
* **widths** — `IntWidths` + `widths.py`: all eleven integer spellings
  dispatch to the right OSC tag on that target's actual type widths.
* **probe** — `oscprobe.py`: 7 liveness/framing probes including
  byte-at-a-time delivery and bundles with zero-byte types.
* **bench** — `OscBench` + `bench.py`: the loss-attributing bench (trickle
  gate, one-write bursts, both flood directions, compound echo, lazy-reader
  ring map), 2026-08-11 onward.

## By USB stack family

The stack, not the board, is what determines transport behaviour. Seven
families have been characterised on hardware:

| family | host→device | device→host | verdict |
|---|---|---|---|
| Arduino AVR CDC (32U4) | NAK — no ring to overflow, **measured** | 250 ms give-up; drops all while port closed | clean end to end once the ZLP endpoint wedge ([#112](https://github.com/arduino/ArduinoCore-avr/issues/112)) is worked around, as `SLIPEncodedSerial.h` does: 4400 B in one write and 1100 B against a lazy reader, both lossless |
| Arduino SAMD CDC | NAK | clean | clean end to end — measured on SAMD11 and SAMD51 |
| PJRC teensy3 | NAK | **starves under compound load** — twelve shared 64-byte buffers, 70 ms TX give-up | replies overlapping an inbound burst are silently dropped; each direction alone is clean |
| PJRC teensy4 | NAK | clean | clean end to end, including compound; the bench's reference board |
| TinyUSB (RP2040/RP2350 core) | NAK — refuses to re-arm the endpoint without FIFO space | clean | clean end to end, including compound |
| ESP32 HWCDC (USB-Serial-JTAG) | **drops** — ISR drains the 64-byte FIFO into a 256-byte queue and discards overflow | clean | bursts past ~260 B truncate *with mid-frame corruption* even against a fast-draining sketch; `begin()` now enlarges the queue (`OSC_SLIP_RX_BUFFER`, default 4096) |
| Renesas RA4M1 **bridged UART** (UNO R4 WiFi) | **drops** — no USB at all on this path; the on-board ESP32-S3 bridge terminates flow control, so a full 512-byte UART ring simply overruns | clean | the only non-USB transport here; clean at ordinary rates and against a fast-draining sketch, but a slow reader loses the tail of any burst past its ring |

## Measured boards

### AVR — ATmega32U4 (8-bit, `int` is 16 bits)

| board | chip | ran | found |
|---|---|---|---|
| LilyPad USB | ATmega32U4 | echo 22/22 · widths 11/11 (int=2 long=4 ll=8 double=4) · probe 7/7 · ZlpTest · TxBench | The ZLP receive wedge was isolated here: one 64/128-byte transfer stalls reception forever on the stock core; the library's bank-release fix and the pages' `padAwayZLP()` were both verified on this board. Block-transmit rework measured 2773 → 1348 µs/packet. |
| Arduino Esplora | ATmega32U4 | `EsploraOscuino` listen: 199/199 SLIP frames decoded against an independent decoder | The library's largest packets: 38-message bundles, 400–1316 bytes, over a custom USB descriptor (`0x6666/0x1099`). Flashing needs the Caterina 1200-baud/port-race procedure below. |

### SAMD

| board | chip | ran | found |
|---|---|---|---|
| moddo pinch | ATSAMD11 | echo 22/22 · widths 11/11 · full bench clean (first bench validation: 4.4 KB one-write, 1100 B lazy-reader bursts, ~1830 f/s; the same-day Teensy 4.0 reference run closed the first-run calibration caveat) | Smallest part in the table: `OscBench` builds to 9208 of its 12288 flash bytes. `BootloaderCDC` serial class, own `ARDUINO_MODDO_PINCH` entry in the detection ladder. |
| SparkFun SAMD21 Mini | ATSAMD21 | full bench clean: gate, 50/200-frame one-write ×3 (2272 f/s, 4.4 KB), out ×3, compound ×3, 1100 B lazy-reader ring | **Found a real library bug.** Its variant sets `build.board=SAMD_ZERO`, so `ARDUINO_SAMD_ZERO` was defined and the detection ladder took the Arduino Zero exception and bound `SLIPSerial` to `Serial` — but this board has no programming-port UART, and its variant defines `SERIAL_PORT_MONITOR` and `SERIAL_PORT_USBVIRTUAL` **both** as `SerialUSB`. Every packet went to a UART: the board enumerated, accepted writes and answered nothing, which reads exactly like a dead sketch. The ladder now asks the variant via `SERIAL_PORT_MONITOR`, which is `Serial` on a genuine Zero (so nothing existing moves) and `SerialUSB` here. |
| Adafruit Gemma M0 | ATSAMD21 | probe 7/7 | Also the board that proved `bundleIN` must be file-scope, not `loop()`-local. |
| Adafruit HalloWing M0 Express | ATSAMD21 | echo 22/22 | |
| Adafruit Feather M4 Express | ATSAMD51 | echo 22/22 · widths 11/11 (int=4 long=4 ll=8 double=8) | Doubled as the mic-absent control for the EdgeBadge: same FQBN, flat-zero PDM input, `micOK` correctly false. |
| Adafruit PyBadge / EdgeBadge | ATSAMD51J19 | `PyBadgeOscuino` end to end: screen (SPI1/SERCOM4), 74HC165 button chain, 5 NeoPixels, light, battery, LIS3DH accelerometer, and the EdgeBadge PDM microphone | Mic measured: −64 dBFS quiet floor, 26.6 dB range to speech, clip at `MIC_GAIN` 16 (default now 8, unverified). Mic presence is a runtime signal probe — the SERCOM exists on mic-less badges, so `begin()` succeeding proves nothing. PDM filter is an ISR contract: leave `SERCOM3_0_Handler` undefined and the board hangs in `setup()`. |

### PJRC Teensy

| board | chip | ran | found |
|---|---|---|---|
| Teensy 3.2 | MK20DX256 (M4, 72 MHz) | echo 22/22 · widths 11/11 · bench: separate directions clean ×3 (7142 f/s in) · compound echo B=50/50, D=26–27/50 | The resolved burst-loss claim: receive is perfect; replies are starved by the shared twelve-buffer pool and the 70 ms TX give-up. Loss window opens ~frame 2–3. |
| Teensy 3.6 | MK66FX1M0 (M4F, 180 MHz) | bench: separate directions clean ×3 (16–25k f/s in) · compound B=50/50, D=30–33/50 | Same pool, milder and later (~frame 6–9) — the severity gradient that convicts the pool and clears the instrument. |
| Teensy 4.0 | IMXRT1062 (M7, 600 MHz) | echo 22/22 · widths 11/11 · probe 7/7 · bench all clean incl. compound ×3, 66–100k f/s in | Immune; different buffer architecture. The Method's same-day reference board. |

### RP2040 / RP2350 (TinyUSB)

| board | chip | ran | found |
|---|---|---|---|
| Robotistan Pico Bricks v2.1 | RP2040 (M0+) | `PicoBricksOscuino` end to end: OLED, SHTC3, I²C motor driver, WS2812, LED, button, relay, buzzer, pot, LDR · I²C swept | **The silkscreen is wrong about the OLED** — printed `SDA-GP2 SCL-GP3`, actually **GP4/GP5**; Robotistan's own handbook repeats the error, which comes from their pinout diagram using Pico *physical* pin numbers. Sweep found 0x3C, 0x70, 0x22 on GP4/GP5 and nothing on GP2/GP3 — and 0x70 (SHTC3) + 0x22 (I²C motor driver) identify it as **V2**, which the photo does not: a V2 looks like a V1 until you scan it. Their V2 sketch sets `INPUT_PULLUP` on the button then treats HIGH as pressed; measured, the pin reads LOW under the internal pull-up, so there is an external pull-down and the button is **active HIGH**. GP0 is shared by the IR receiver and `Serial1` TX. To reflash from MicroPython the 1200-baud touch does nothing — send `machine.bootloader()` to its REPL. |
| DFRobot Beetle RP2040 | RP2040 (M0+) | echo 22/22 · widths 11/11 (int=4 long=4 ll=8 double=8) | First RP2040 ever to run this library on hardware. |
| Seeed XIAO RP2350 | RP2350 (M33) | echo 22/22 · widths 11/11 · full bench clean incl. compound ×3 and 1100 B lazy-reader bursts (~3.3k f/s) | Predicted clean in advance from TinyUSB's NAK design; measured exactly so. |

### Renesas RA4M1

| board | chip | ran | found |
|---|---|---|---|
| Arduino UNO R4 WiFi | RA4M1, **bridged UART** (ESP32-S3 bridge) | echo 22/22 · widths 11/11 · bench 2026-08-12: gate, 50/200-frame one-write bursts ×3, out ×3, compound ×3 all clean (~530 f/s on-board); ring map pins at 25 frames / `firstGap@24` ×3 · `UnoR4MatrixOscuino` LED-matrix demo end to end | The only non-native-USB board here: baud rate is real (115200), a mismatch reads as framing noise, and flashes intermittently report success without taking. **The bridge has no end-to-end flow control**, so the core's 512-byte UART ring overruns rather than back-pressuring — see the burst section. Also the only board here to wedge mid-session and need a physical RESET; a reflashed known-good sketch failed the trickle gate until then, which is exactly what that gate is for. |
| Seeed XIAO RA4M1 | RA4M1, native USB | echo 22/22 · widths 11/11 | Same chip, native CDC — the pairing isolates bridge effects from silicon. |

### ESP32 (HWCDC unless noted)

| board | chip | ran | found |
|---|---|---|---|
| Seeed XIAO ESP32-C6 + XIAO Expansion Board | ESP32-C6 (RISC-V, 160 MHz) | `XiaoC6ExpOscuino` end to end: OLED, buzzer, button, LED · I2C bus scanned | **Stock FQBN defaults** — this variant sets `cdc_on_boot=1`, unlike the generic C6 devkit that needs `:CDCOnBoot=cdc`. Bus scan found 0x3C (SSD1306), 0x51 (PCF8563 RTC), 0x57 (EEPROM); SDA=GPIO22/D4, SCL=GPIO23/D5, buzzer D3, button D1 active-LOW. `A3` does not exist on this variant (only A0–A2) though Seeed's wiki names the buzzer pin that way — use `D3`. The boot `/hello` is never seen: the USB device re-enumerates after reset and the host opens the port later, so `/hello` is also an inbound address the client asks for. `XiaoC6ExpWiFi` is the WiFi twin, compiled but not run. |
| Adafruit Feather ESP32-S3 (no PSRAM) | ESP32-S3 (Xtensa LX7) | full bench clean on its TinyUSB default (5000-6250 f/s, 4.4 KB one-write); then rebuilt `:USBMode=hwcdc` for a controlled A/B | The board that generalises the HWCDC finding: same hardware, same sketch, stack as the only variable. TinyUSB clean; HWCDC with `-DOSC_SLIP_RX_BUFFER=0` truncates at **12 frames / `firstGap@11`** — the C6's exact signature; HWCDC with the library default clean. No USB-serial chip, so the first flash needs BOOT+RESET by hand and a plain RESET afterwards to leave download mode. |
| ESP32-C6 devkit | ESP32-C6 (RISC-V, 160 MHz) | echo 22/22 · widths 11/11 · bench: byte ceiling ~264 B (12 × 22 B frames, `firstGap@11`), ×16 queue → ceiling ~4.3 KB, out + compound clean · library default re-verified: flag-free build 50/50 ×3, ring clean to 1100 B, 4400 B tail loss @193 (queue arithmetic) | The HWCDC conviction: the cliff moves with the configured queue, twice — and the `begin()`-baked remedy reproduces the hand-rolled A/B identically on hardware. Needs `:CDCOnBoot=cdc`. |
| ESP32-C3 devkit | ESP32-C3 (RISC-V) | echo 22/22 · widths 11/11 · original 4-board HWCDC boundary | Needs `:CDCOnBoot=cdc`. |
| Adafruit QT Py ESP32-S3 | ESP32-S3 (Xtensa LX7) | echo 22/22 · widths 11/11 · original 4-board HWCDC boundary | Needs `:USBMode=hwcdc,CDCOnBoot=cdc` — its default USB-OTG mode enumerates nothing. First flash needs BOOT+RESET by hand, then a plain RESET to leave download mode. |
| Seeed XIAO ESP32S3 Sense | ESP32-S3, 8 MB PSRAM, OV2640 | echo 22/22 · widths 11/11 · `XiaoS3SenseOscuino` camera: 19/19 valid JPEGs, 320×240, ~4.8 f/s over SLIP | Stock FQBN defaults — carrying the QT Py's options here silences it completely. The sketch's PDM microphone block compiles but has **never run**; it says so in its own header. |
| M5Stack StampS3 | ESP32-S3 | echo 22/22 · widths 11/11 · probe 7/7 | |
| M5Stack NanoC6 | ESP32-C6 | none — board stopped responding before any suite ran | Recorded so the gap is visible. |

## Flashing procedures that cost real time to learn

* **Caterina (32U4: Esplora, LilyPad USB, Leonardo)** — open the port at
  1200 baud with `CLOCAL` cleared so closing drops DTR, then race the
  uploader against the *same port name* returning (~0.75 s). Waiting for a
  "new" port loses; the bootloader reuses the name.
* **Teensy** — upload to the `usb:` protocol port from
  `arduino-cli board list`, not the `/dev/cu.*` tty; targeting the tty
  fails with "Teensy should be selected from teensy ports".
* **UF2 (PyBadge, RP2350)** — double-tap reset mounts the boot volume;
  `INFO_UF2.TXT` names the *bootloader's* board, which is not proof of the
  model (an EdgeBadge here carries a PyBadge bootloader).
* **ESP32-S3 without a serial chip (QT Py)** — first flash: hold BOOT, tap
  RESET, release BOOT; afterwards a plain RESET to run the app. Neither the
  1200-baud touch nor `esptool run` substitutes.
* **Ports move.** Reflashing renumbers `/dev/cu.usbmodem*`; a board plugged
  into a hub can displace another entirely. Identify by USB VID/PID
  (`system_profiler SPUSBDataType`) or `esptool chip_id`, not by port name
  or by what was supposed to be plugged in.
* **Web Serial holds the port.** A connected browser page blocks uploads
  with "No device found"; `lsof /dev/cu.usbmodem*` names the culprit.

## Compiled only — no hardware claim

Built locally across every installed core for the 4.0.0 release (GCC
4.8–16.1): `arduino:avr` (uno, leonardo, esplora), `adafruit:avr:flora8`,
`arduino:sam:arduino_due_x`, `arduino:samd`, `arduino:renesas_uno`,
`adafruit:samd` (gemma_m0 and the TinyUSB-stack variants),
`adafruit:nrf52:feather52840`, `esp32:esp32`, `rp2040:rp2040`,
`teensy:avr:teensy40`, `arduino:zephyr:unoq` (UNO Q), and the moddo fork.
CI re-verifies a six-configuration subset on every push — uno, leonardo,
esplora, mkrzero, esp32, rpipico — plus the host suite under ASan and the
webserial generator; the rest of the list is a release-time local sweep,
not a per-push guarantee.

## Cannot be served

* **Adafruit Trinket (ATtiny85)** — no hardware UART, no `Serial` object in
  its variant, and a programming-only V-USB bootloader: nothing for
  `SLIPEncodedSerial` to bind to. The **Pro Trinket** (ATmega328P) is fine
  over its UART.

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
| stm32duino CDC (STM32F4) | **drops with corruption** — the core's CDC receive queue is 3 × 64 B = **192 bytes** (`CDC_RECEIVE_QUEUE_BUFFER_PACKET_NUMBER`, `#ifndef`-guarded); burst overflow loses mid-frame bytes | clean | bursts past ~200 B truncate with `seqErrs`+`crcErrs` even against a fast drain; the flag at 64 packets (4096 B) makes everything clean — set it in **both** C and C++ flags, the queue lives in a C file. The test instruments carry it via stm32duino's sketch-dir `build_opt.h`, which other cores ignore |
| Renesas RA4M1 **bridged UART** (UNO R4 WiFi) | **drops** — no USB at all on this path; the on-board ESP32-S3 bridge terminates flow control, so a full 512-byte UART ring simply overruns | clean | the only non-USB transport here; clean at ordinary rates and against a fast-draining sketch, but a slow reader loses the tail of any burst past its ring |

## Measured boards

### AVR — ATmega32U4 (8-bit, `int` is 16 bits)

| board | chip | ran | found |
|---|---|---|---|
| LilyPad USB | ATmega32U4 | echo 22/22 · widths 11/11 (int=2 long=4 ll=8 double=4) · probe 7/7 · ZlpTest · TxBench | The ZLP receive wedge was isolated here: one 64/128-byte transfer stalls reception forever on the stock core; the library's bank-release fix and the pages' `padAwayZLP()` were both verified on this board. Block-transmit rework measured 2773 → 1348 µs/packet. |
| Adafruit Circuit Playground (Classic) | ATmega32U4 | full bench 2026-08-13: gate, 50/200-frame one-write ×3 (**4400 B clean**, 478 f/s), out ×3, compound ×3, 1100 B lazy-reader ring all clean | The run that killed the "AVR cuts off at ~378 B" claim: with the library's ZLP workaround in place, a part with no software rx ring at all NAKs its way through eleven times the claimed ceiling. Flashing needs the Caterina procedure below — and on this board the 1200-baud touch does not fire the bootloader; watch the port vanish on a physical reset and flash the instant it returns. |
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
| DeskPi PicoMate | RP2040 (M0+) | `PicoMateOscuino` end to end: LSM6DS3TR-C 0x6A, MMC5603 0x30, SHT30 0x44, LTR-381RGB 0x53 on `Wire1` GP14/15; SSD1315 0x3C alone on `Wire` GP16/17; PDM mic via the core's PIO PDM library; button, encoder, PIR, WS2812, buzzer · both I²C buses found by sweep · mic response +10.3 dB at the buzzer's 4.8 kHz resonance, mean of 3 | Two traps recorded in the sketch: arduino-pico defaults `Wire1` to GP26/GP27 — this board's **button and buzzer** — so a bare `Wire1.begin()` silently reconfigures both; and GP26 is shared by the button and the encoder switch. Chips identified by ID register, not address: 0x53 failed the ADXL345 DEVID check, which is what ruled that part out. |
| Waveshare RP2350-Zero | RP2350 (M33) | full bench 2026-08-18, all clean: gate, 50-frame one-write ×3 (3333 f/s), out 200 ×3, compound ×3, 1100 B lazy-reader ring | No board definition exists for it in the arduino-pico core, and its bootloader identifies only as the generic `Board-ID: RP2350` (`INFO_UF2.TXT`), so it was built as `rp2040:rp2040:rpipico2` — same RP2350, same 4 MB flash, and the pinout differences do not reach the USB path. Flashed the documented RP2 way: hold BOOTSEL, copy the `.uf2` to the mounted volume. It re-enumerates reporting `Pico 2` as its USB product name, which is the FQBN talking, not the hardware — do not use that name to identify the board. |
| Seeed XIAO RP2350 | RP2350 (M33) | echo 22/22 · widths 11/11 · full bench clean incl. compound ×3 and 1100 B lazy-reader bursts (~3.3k f/s) | Predicted clean in advance from TinyUSB's NAK design; measured exactly so. |

### BBC micro:bit — the first non-Arduino firmware

The board runs MicroPython, not this library; what it shares is the wire
contract, served by `extras/python/MicrobitOscuino/` (four generated files —
see below for why four). Transport family: DAPLink KL26Z **bridged UART**,
like the UNO R4 WiFi row — no end-to-end flow control, and the bridge drops
device→host bytes while no host port is open.

| board | chip | ran | found |
|---|---|---|---|
| BBC micro:bit V1.3 | nRF51822 (16 KB RAM) + KL26Z DAPLink 0249 | probe 7/7 (incl. byte-at-a-time delivery) · extension probes: buttons, accelerometer (z read 1024 ≈ 1 g flat on the bench), display via `/s/l`, `/tone/0`, a literal 0x03 payload byte, `/s/q` exit — all pass, 2026-08-26 | Getting here was a ladder of measured failures worth keeping. **Flashing:** stock hexes failed from the MSD drive and from WebUSB alike with DAPLink's `type: target` error until a `pyocd erase --chip` cleared the nRF51 — the part came protected by whatever was on it; the interface update 0234→0249 was necessary but not sufficient. **RAM:** the V1 compiles source on the board; parsing costs ~2.1× the file's bytes and the boot file gets a bigger budget than runtime imports (which get 8752 B minus what earlier files retain — retention measured at ~0.8× source). One 9058-byte file died in MemoryError, so did 8332, so did every two-file split; `main.py` + three ~1 KB modules boots. **Runtime:** `uart.read()` allocates 256 B per call and died mid-probe on the post-compile heap; a preallocated 64 B buffer with `readinto()` plus one `gc.collect()` before the serve loop fixed it. **v1.9.2 dialect:** no `str.encode`/`bytes.decode` (spell them `bytes(s,'utf8')`/`str(b,'utf8')`), no `sys.modules`, a failed import leaves a permanent empty husk module, and a second queued ctrl-C fires into the *next* code executed. `micropython.kbd_intr(-1)` exists and measurably works — an integer argument of 3 puts a raw 0x03 on the wire and the program survives it. **Bridge quirks:** opening the port does *not* reset the target; the CDC sometimes enumerates mute until reopened; an uncaught exception scrolls its message on the LED matrix for tens of seconds during which serial input is ignored (ctrl-C aborts the scroll). |

### Adafruit Fruit Jam — one page, two firmwares

Like the micro:bit entry above, the CircuitPython firmware shares only the
wire contract with this library (`extras/python/FruitJamOscuino/`); unlike
it, the same board also has a hand-written Arduino sketch
(`examples/FruitJamOscuino/`) answering the same address space through the
same generated page.

| board | chip | ran | found |
|---|---|---|---|
| Adafruit Fruit Jam | RP2350B, TLV320DAC3100 codec, DVI on HSTX | CircuitPython 10.2.1: probe 7/7 · fruitjamprobe 11/11 (buttons, NeoPixel echoes, codec beep — heard, DVI text — seen, 0x03 in-frame, /s/q), 2026-08-29. Arduino (arduino-pico 6.0.0, `adafruit_fruitjam`): probe 7/7 · fruitjamprobe 10/10 with DVI text live via the "Adafruit DVI HSTX" library (dvhstx), audio heard, LEDs seen. Swapping firmwares is a UF2 drag each way. **Two library incompatibilities measured on this board, both fatal:** PicoDVI (the RP2040 PIO implementation) pointed at the HSTX pins wedges the boot before USB enumerates; and with dvhstx running, a single `Adafruit_NeoPixel.show()` — which masks all interrupts for ~190 µs against a 31.7 µs scanline IRQ deadline — took down video, audio and the main loop at once (LEDs latched, I2S looping garbage into the speaker, USB alive but the loop gone; convicted by wire-level breadcrumbs landing at `pre-show` and never `post-show`). The fix is Adafruit's own NeoPXL8, whose DMA-fed `show()` masks nothing — and whose `begin()` must run *after* `display.begin()`, which reclocks the system to 126 MHz. | Shipped with a TinyUSB factory demo (PID 0xCAFE, a serial counter) that has no REPL — but it honours the 1200-baud touch, as does CircuitPython, so flashing normally needs no buttons at all. When firmware wedges before USB enumerates the touch has nothing to talk to, and the hardware door is **front button #1, which doubles as BOOT**: hold it, tap reset (measured — that is how the board came back from the PicoDVI experiment below). Speaking of which: PicoDVI's PIO engine pointed at the HSTX pins compiles for RP2350 but **wedges the boot** — the device never enumerates; the attempt is kept in the sketch behind `FRUITJAM_TRY_PIO_DVI`, default off, so Arduino DVI waits for a real HSTX driver. The codec shares its I2C bus with the DVI connector's DDC lines (a monitor's EDID EEPROM at 0x50 and DDC/CI at 0x37 answer a scan), and a wedged `busio` object NACKs every multi-byte write while still ACKing address probes — creating the bus fresh and retrying is what makes codec init reliable; `PERIPH_RESET` is best left alone since it also resets the ESP32. This CircuitPython build does not auto-create `board.DISPLAY`: `picodvi.Framebuffer(320, 240, …, color_depth=8)` brings the console terminal up on the monitor, after which text output is just `print()` — but the lane order is the board's, not the TMDS textbook's (`red_dp=D0P` … `blue_dp=D2P`, matching Adafruit_CircuitPython_FruitJam): wiring blue to D0 "by the book" put DVI's sync stream on the wrong pair and the monitor reported no signal. Audio needs the codec clocked from a **15 MHz PWM on I2S_MCLK** (`configure_clocks(…, mclk_freq=15_000_000)`) — BCLK-referenced PLL config is accepted register-by-register and stays silent — and the speaker/headphone routes are exclusive, with `dac_volume` on a −63..+23 dB scale (−10 dB is an audible demo level; the helper library's default is a whispery −33). Both failures looked identical from software: every probe answered while the room stayed dark and quiet. Measured GPIOs for the Arduino side: CK 12/13, D0 14/15, D1 16/17, D2 18/19, I2S BCLK/WS/DIN/MCLK 26/27/24/25. |

### What the stock-queue overflow actually looks like

The C6 and Feather rows above each quote a truncation count under
`-DOSC_SLIP_RX_BUFFER=0`, and earlier revisions of this file called the
Feather's "the C6's exact signature". Measured properly on 2026-08-17, that
framing was wrong.

Nineteen controlled trials — one 1100-byte burst per trial, a hard reset
before every one so no trial inherits the last one's state, and the
lazy-reader flag explicitly cleared — across an M5Stack AtomS3 (both the
M5Stack 3.3.8 and Espressif 3.3.11 cores) and an M5Stack StampS3 (Espressif
3.3.11): **eighteen ended with the board not answering at all, and one
reported `rx=16/50, seqErrs=4, crcErrs=2, decodeErrs=1, firstGap=12`.**

So the byte ceiling reproduces — the overflow always happens, at the same
place — but what the board does afterwards does not. Sometimes the corruption
leaves the SLIP decoder able to resynchronise and report a truncated count;
more often it swallows the query that follows and the sketch goes silent
until reset. A quoted frame count is therefore a sample from that
distribution, not a property of the part, and two boards printing different
numbers is not evidence that they differ.

None of this is reachable with the library as shipped: `begin()` sets the
queue to 4096 on ESP32 cores, and every HWCDC board here is clean on that
default. It matters only if you compile the fix out.

### STM32 (stm32duino)

| board | chip | ran | found |
|---|---|---|---|
| EC Buying STM32F407VET6 core board | STM32F407VET6 (M4F, 168 MHz) | echo 22/22 · widths 11/11 (int=4 long=4 ll=8 double=8) · probe 7/7 · full bench 2026-08-24 clean with the queue remedy: in 50 ×3 (12,500 f/s), out 200 ×3, compound ×3, 1100 B lazy-reader ring — the library's FIRST STM32 | stm32duino's CDC receive queue is **192 bytes** and DROPS with mid-frame corruption on burst overflow — third drop-family stack after ESP32 HWCDC and the R4 WiFi bridge; see the family table. Board has no BOOT0 button — a header pin strapped to 3V3 enters ROM DFU, but a USB transition log showed the dupont contact failing on most attempts (every "strapped" reset came back in CDC), so the instruments carry an STM32-only `/dfu` OSC route that jumps to the ROM bootloader on command: one hard flash installs it, every reflash after is strapless. Hold the port open ~1 s after sending `/dfu`, or the message dies in the host buffer. |
| Blue Pill clone (STM32F103C6T6) | STM32F103C6 (M3, 32 KB flash) | compile-fit only: OscEcho + CDC = 24,076 B (73%), RAM 46% | NOT run: the F103 ROM boots USART1 only (no USB DFU), the board is unflashed pending an ST-Link or serial adapter, and clone Blue Pills are notorious for a wrong D+ pull-up that breaks CDC enumeration even with correct firmware. Recorded so the gap is visible. |

### Renesas RA4M1

| board | chip | ran | found |
|---|---|---|---|
| Arduino UNO R4 WiFi | RA4M1, **bridged UART** (ESP32-S3 bridge) | echo 22/22 · widths 11/11 · bench 2026-08-12: gate, 50/200-frame one-write bursts ×3, out ×3, compound ×3 all clean (~530 f/s on-board); ring map pins at 25 frames / `firstGap@24` ×3 · `UnoR4MatrixOscuino` LED-matrix demo end to end | The only non-native-USB board here: baud rate is real (115200), a mismatch reads as framing noise, and flashes intermittently report success without taking. **The bridge has no end-to-end flow control**, so the core's 512-byte UART ring overruns rather than back-pressuring — see the burst section. Also the only board here to wedge mid-session and need a physical RESET; a reflashed known-good sketch failed the trickle gate until then, which is exactly what that gate is for. |
| Seeed XIAO RA4M1 | RA4M1, native USB | echo 22/22 · widths 11/11 · bench 2026-08-13: gate, 50/200-frame one-write ×3 (4.4 KB, 2173 f/s), out ×3, compound ×3, 1100 B lazy-reader ring — **all clean** | The pairing paid off: same silicon as the UNO R4 WiFi but over native USB, and it loses nothing where the bridged board pins at 25 frames / 550 B. The 512-byte ceiling is the bridge's, not the RA4M1's. Flashes over DFU; enumerates as VID 0x2886 PID 0x0049 with a real CDC port, unlike a board sitting in `RA USB Boot`. |

### ESP32 (HWCDC unless noted)

| board | chip | ran | found |
|---|---|---|---|
| Seeed XIAO ESP32-C6 + XIAO Expansion Board | ESP32-C6 (RISC-V, 160 MHz) | `XiaoC6ExpOscuino` end to end: OLED, buzzer, button, LED · I2C bus scanned | **Stock FQBN defaults** — this variant sets `cdc_on_boot=1`, unlike the generic C6 devkit that needs `:CDCOnBoot=cdc`. Bus scan found 0x3C (SSD1306), 0x51 (PCF8563 RTC), 0x57 (EEPROM); SDA=GPIO22/D4, SCL=GPIO23/D5, buzzer D3, button D1 active-LOW. `A3` does not exist on this variant (only A0–A2) though Seeed's wiki names the buzzer pin that way — use `D3`. The boot `/hello` is never seen: the USB device re-enumerates after reset and the host opens the port later, so `/hello` is also an inbound address the client asks for. `XiaoC6ExpWiFi` is the WiFi twin, compiled but not run. |
| Adafruit Feather ESP32-S3 (no PSRAM) | ESP32-S3 (Xtensa LX7) | full bench clean on its TinyUSB default (5000-6250 f/s, 4.4 KB one-write); then rebuilt `:USBMode=hwcdc` for a controlled A/B | The board that generalises the HWCDC finding: same hardware, same sketch, stack as the only variable. TinyUSB clean; HWCDC with `-DOSC_SLIP_RX_BUFFER=0` truncated at **12 frames / `firstGap@11`**, matching the C6's number; HWCDC with the library default clean. **That truncation count is one draw, not a signature** — see the note below the table. No USB-serial chip, so the first flash needs BOOT+RESET by hand and a plain RESET afterwards to leave download mode. |
| ESP32-C6 devkit | ESP32-C6 (RISC-V, 160 MHz) | echo 22/22 · widths 11/11 · bench: byte ceiling ~264 B (12 × 22 B frames, `firstGap@11`), ×16 queue → ceiling ~4.3 KB, out + compound clean · library default re-verified: flag-free build 50/50 ×3, ring clean to 1100 B, 4400 B tail loss @193 (queue arithmetic) | The HWCDC conviction: the cliff moves with the configured queue, twice — and the `begin()`-baked remedy reproduces the hand-rolled A/B identically on hardware. Needs `:CDCOnBoot=cdc`. |
| ESP32-C3 devkit | ESP32-C3 (RISC-V) | echo 22/22 · widths 11/11 · original 4-board HWCDC boundary | Needs `:CDCOnBoot=cdc`. |
| Adafruit QT Py ESP32-S3 (N4R2) | ESP32-S3 (Xtensa LX7) | echo 22/22 · widths 11/11 · original 4-board HWCDC boundary · **full bench 2026-08-17 on the stock TinyUSB default, all clean**: gate, 50-frame one-write ×3 (5555 f/s), out 200 ×3, compound ×3, 1100 B lazy-reader ring | The earlier row said this board "needs `:USBMode=hwcdc,CDCOnBoot=cdc` — its default USB-OTG mode enumerates nothing". On the N4R2 in front of me the stock default (`build.usb_mode=0`, TinyUSB) enumerated and benched clean without any option override, so that instruction is not general — it belongs to the earlier unit/core, not to the board. First flash needs BOOT+RESET by hand, then a plain RESET to leave download mode. |
| M5Stack AtomS3 | ESP32-S3 (Xtensa LX7, QFN56 rev v0.2) | full bench 2026-08-17 on stock defaults (**HWCDC**, `build.usb_mode=1`), all clean: gate, 50-frame one-write ×3 (5555 f/s), out 200 ×3, compound ×3, 1100 B lazy-reader ring through 1100 B | Third HWCDC part to run clean on the library's default 4096-byte queue. Pulled from an Atom JoyStick base to test — the K137 base's own USB-C is charge-only (D+/D−/SBU no-connect on `USB1`, and its STM32F030F4P6 has no USB peripheral), so the host cable must go to the AtomS3's own upper Type-C. |
| M5Stack Atom JoyStick K137 (AtomS3) | ESP32-S3 (Xtensa LX7, QFN56) | `AtomJoyOscuino` end to end 2026-08-17: both sticks, four buttons, front button, both battery channels; STM32 firmware version 0x FE reads **2** · full transport bench clean (see AtomS3 row) | **Rest is not 2048**: measured left 2079/2123, right 1973/2123, ±1.5 LSB noise — up to 75 counts off centre, and different per axis, so a client must sample rest rather than assume it. Travel is the full 12-bit range (0..~4090, no deadband). Both battery registers live (4165/4185 mV). **`M5.begin()` does not return on this unit once the Atom is seated in the base** — a sketch containing only `SLIPSerial.begin()` and `M5.begin()` reproduced it, and the same binary streams fine on an M5Dial; out of the base it returns but reports `Display.width()==0`. `AtomJoyOscuino` therefore defaults to no M5Unified at all (plain `Wire` + GPIO41) and runs everywhere. Flash via the AtomS3's OWN upper Type-C; the base's lower connector is charge-only. |
| M5Stack M5Dial (StampS3) | ESP32-S3 (Xtensa LX7) | full bench 2026-08-17 on stock defaults (**HWCDC**), all clean: gate, 50-frame one-write ×3 (5555 f/s), out 200 ×3, compound ×3, 1100 B lazy-reader ring | A StampS3 in a rotary-encoder body with a 240×240 round LCD. `M5.getBoard()` returns 12 (`board_M5Dial`) and `M5.Display` comes up 240×240 — worth knowing because an S3 in an M5 chassis is otherwise indistinguishable from any other S3 at the USB layer: same VID/PID `0x303a/0x1001`, same QFN56 8 MB part in `esptool`. Identify these by asking the firmware, never by port order. |
| LilyGO T-Display-S3 | ESP32-S3 (Xtensa LX7, QFN56 rev v0.2, 8 MB PSRAM) | full bench 2026-08-17 on stock defaults (**HWCDC**, `build.usb_mode=1`), all clean: gate, 50-frame one-write ×3 (5555-6250 f/s), out 200 ×3, compound ×3, 1100 B lazy-reader ring | Fourth HWCDC part clean on the library default. |
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

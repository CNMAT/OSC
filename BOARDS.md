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

The stack, not the board, is what determines transport behaviour. Eight
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
| Silicon Labs **CMSIS-DAP VCOM** (XIAO MG24) | **drops, then wedges** — clean to ~242 B in one write, lossy beyond, and past ~396 B the interface chip stops accepting writes entirely | clean | the only stack here with a *latching* failure: the wedge survives reprogramming and resetting the target, because the CDC endpoint lives in the interface chip, and only a physical replug clears it. Pacing the same bytes is clean, so the constraint is per-write size |
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
| Elecrow All-in-one Starter Kit for Pico 2 (mainboard v1.2) | RP2350A (M33, rev A2, QFN60), **8 MB** flash, MAC-less; chipid `0xec4722e8dbef5ecc` | echo 22/22 · widths 11/11 (int=4 long=4 ll=8 double=8) · probe 7/7 (`RP2040Oscuino` built for `rpipico2`) · full bench 2026-09-04 clean: gate, in 50 one-write ×3 (3333–3571 f/s), out 200 ×3, compound ×3 (50/50 both ways), ring 20 to 1100 B — no loss, so no reference run was owed | **Not a socketed Pico 2.** The USB descriptor says "Pico 2" (2e8a:000f) because the factory firmware was built with the `rpipico2` definition; the kit's own datasheet puts an RP2350A on Elecrow's mainboard with a W25Q64 NOR flash and an APS6404L PSRAM (CS on GP1), and `picotool info -a` in BOOTSEL read **flash size 8192K** where the datasheet's spec table says 4 MB. Built as `rp2040:rp2040:rpipico2` (4 MB assumed, PSRAM unused), which is what Elecrow's lessons select too. **Flashed without a mounted drive**: the 1200-baud touch drops it into BOOTSEL at once, then `picotool load file.uf2 -x` over the boot interface — nine cycles today, each under 10 s; `picotool load -f` could not find a reset interface in the factory firmware. `ElecrowPico2Oscuino` (sensors and actuators; the TFT and touch panel not yet) then ran end to end, `contractprobe --actuate --sound` 45 passed, 0 failed, 2 skipped (the passive `ir` and `diag`), begun with the stream stopped so every bare ask was really asked: light 96–98 raw, 27.3 °C / 47 % RH, 70 cm steady on the rangefinder, gas 181–188, the slide pot 0 → 1020 when moved, |g| 1.017 with z up. **The datasheet's MPU6050 at 0x68 is an ST LSM6DS3TR-C at 0x6B** (WHO_AM_I 0x6A) — the sketch's boot I²C sweep, reported in `/diag`, is what caught it. **Sample the sound module before pinging the rangefinder**: with the ping first, the HC-SR04's 40 kHz burst landed inside the 64-sample window and read as a 15882-rms spike in a quiet room; samples-first showed none in 61 bundles (one run each way, mechanism not separated between acoustic and electrical). Unverified: which physical button is which on the GP27 ladder (factory windows 745/805/865/910 of 1023), the WS2812 colour order (factory sketch: NEO_RGB), the touch pad's and hall sensor's polarity, the remote's protocol, and the motion of the relay, servo and vibration motor, which echoed but were not watched. |

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

### ESP32-C3 SuperMini (hw 466ab) — two units, one with the 0.42" OLED

Generic C3 boards of this layout are sold as **ESP32-C3 SuperMini** (hardware
marking 466ab) and also under seller names such as "EGG"; the example folder
keeps the latter for continuity. Both units measured here share GPIO8 (LED,
active low), GPIO9 (BOOT), 22 digital and 6 analog pins, and the HWCDC stack.

| board | chip | ran | found |
|---|---|---|---|
| SuperMini + 0.42" OLED (sold as "EGG"), MAC 14:63:93:8f:a2:8c | ESP32-C3, single core 160 MHz, 4 MB embedded XMC flash | echo 22/22 · widths 11/11 (int=4 long=4 ll=8) · probe 7/7 · full bench clean: gate, in 50 ×3, out 200 ×3, compound ×3, ring 20 (1100 B lazy-reader burst 50/50) — no same-day reference board ran (the Fruit Jam was displaced from the hub), noted per Method · WiFi joined and 5/5 byte-exact UDP OSC round trips host↔board at RSSI −93 dBm, 2026-08-30 | The bringup trap that cost three flash cycles: this board's bare USB-Serial-JTAG makes esptool's post-flash reset a no-op, parking the chip in ROM download mode — fingerprint and cures now in BRINGUP.md's boot-modes section (`esptool run` is the autonomous exit; BOOT=GPIO9 held while plugging enters download mode manually). The rest of the board was identified by measurement, not folklore: an I2C sweep over every plausible pin pair found exactly one device, 0x3C on SDA=GPIO5/SCL=GPIO6 — the 0.42" 72×40 SSD1306-class OLED, driven in `EggC3Oscuino` by U8g2's dedicated `SSD1306_72X40_ER` constructor — text confirmed legible on the panel, so that constructor's offsets are right for this board. BOOT doubles as the user button (`/btn`). The board LED is a plain **active-low LED on GPIO8** — the silkscreen was unreadable and the WS2812-on-GPIO2 folklore was wrong; it was found by a sweep that announced each candidate pin and level on the OLED until human eyes caught it lighting at "pin 8 LOW". It serves the standard `/s/l`. HWCDC stack, so the library's enlarged receive ring applies — same family as the ESP32-S3 and C6 rows. The WiFi twin (`EggC3WiFi`, UDP 8000 + the XIAO-pattern HTTP bridge with its NUL-safe raw body path) is hardware-verified on a 2.4 GHz network at ~−91 dBm: with the ESP32's default modem power save the link measured 1015 ms ping RTT, 66 % ping loss and HTTP connections timing out while UDP occasionally passed; `WiFi.setSleep(false)` took it to 0 % loss, 55 ms average ping and 10/10 UDP OSC round trips at 36 ms — power save batches traffic to DTIM beacons, fatal for interactive OSC. One spontaneous reboot was observed before that fix and never after it or during a later 3-minute uptime watch; unexplained, noted. |

### Seeed XIAO SAMD21 + Round Display for XIAO

| board | chip | ran | found |
|---|---|---|---|
| Seeeduino XIAO on the Round Display (GC9A01 240×240, CHSC6X touch, BM8563 RTC) | ATSAMD21G18 | echo 22/22 · widths 11/11 · probe 7/7 · full bench clean: gate, in 50 ×3, compound ×3, ring 20 — no same-day reference board ran, noted per Method · display text and fills seen, touch streamed 41 events and follows a finger (eyes-confirmed), RTC set from host time and observed ticking, 2026-08-30 | Seeed's own wiki lists the XIAO SAMD21 as "may not be compatible … due to insufficient memory" — measured, that warning indicts their LVGL/Seeed_GFX stack, not the silicon: their TFT_eSPI fork lit the backlight and never a pixel on this host (its SAMD21 backend also demands their FS library), while **Adafruit's GC9A01A driver draws the panel fine from 32 KB, no framebuffer** — the example uses it, with the wiki's Setup501 pin map (SPI D8/D9/D10, CS=D1, DC=D3, BL=D6, INT=D7). Two more traps recorded in the sketch: the **CHSC6X touch controller only acknowledges I2C while a finger is on the glass**, so a boot-time presence probe reads absent-when-present and must never gate the touch poll (the INT line on D7 is the per-read gate, per Seeed's own driver); and the display's physical two-position switch vetoes the backlight no matter what software does. Touch protocol is a 5-byte read at 0x2E: byte0==0x01 marks a point, x=byte2, y=byte4. The v1.1 board's two DIP switches connect/release XIAO pins (wiki changelog: "Add a switch to A0 and D6"): the D6 one gates the backlight (both directions measured — dark-with-glow when off, and `/d/6 0` blanks the lit panel when on), the A0 one puts the LiPo divider on A0 (measured ~940 counts floating vs ~630 switched-in with no battery fitted). The empty coin-cell holder is the RTC's backup: unbacked, the BM8563 wakes with a garbage 2110-00-01 date — the fingerprint to recognise. |

### Nordic nRF52840 — and the first radio transport that is not WiFi

| board | chip | ran | found |
|---|---|---|---|
| Seeed XIAO nRF52840 Sense | nRF52840 (M4F, 64 MHz), Adafruit/Seeed TinyUSB stack | echo 22/22 · widths 11/11 (int=4 long=4 ll=8) · probe 7/7 · full bench clean: gate, in 50 ×3, out 200 ×3, compound ×3, ring 20 (1100 B lazy-reader burst 50/50) — no same-day reference board ran, noted per Method · `XiaoNrf52Oscuino` over USB: IMU, RGB and battery all answer, 2026-08-30 | First nRF52840 in this table, and it lands in the **TinyUSB NAK-clean family** on fingerprint — compound ×3 clean is the discriminator that separates it from the shared-pool stacks. No new rung was needed in `SLIPEncodedSerial.h`: the Seeed nRF52 core is an Adafruit fork and defines `ARDUINO_NRF52_ADAFRUIT` + `SERIAL_PORT_USBVIRTUAL`, so `BOARD_HAS_USB_SERIAL` resolves already (verified with a `#pragma message` probe). **Build trap:** the Seeed nRF52 platform's packaging recipe shells out to `python`, which a modern macOS does not have — the compile succeeds, produces the `.hex`, and then the *upload* fails hunting a `.zip` that was never built. A `python` → `python3` symlink on PATH is the whole fix. **IMU, by documentation not experiment:** the Sense's LSM6DS3TR-C is not on the `Wire` at D4/D5 — Seeed's own driver remaps `Wire` to `Wire1` behind `TARGET_SEEED_XIAO_NRF52840_SENSE`, a macro the *Bluetooth* core also defines, so the wiki's "use the mbed core for IMU" advice does not force a choice between BLE and motion; this example uses both. The driver also reconfigures the IMU power pin as a **high-drive** output (`NRF_P1->PIN_CNF[8]`, H0H1) before releasing the rail, which hand-rolled `pinMode`/`digitalWrite` register reads do not — those found no IMU at all. Readings sanity-check against physics: 0.99 g on Z with the board flat, gyro at zero when still. **Battery (BQ25101):** charge current is HICHG on D22/P0.13 — HIGH = 50 mA, LOW = 100 mA — and charge state is ~CHG on D23/P0.17, LOW while charging, open-drain so read with a pull-up; the variant names only the first, the second comes from its pin-map comments. Both are wired to `/chg`, which reports the current as **-1 until set**, since at boot the pin is untouched and the board's default is in force. `/bat`'s divider maths (`raw × 3600 × 2 / 1024`) is written to the documented divider but is **still unverified**: with no cell attached the pin floats and read 2496 mV and then 618 mV in successive runs — noise, not a measurement. Attach a LiPo to close that one. **PDM microphone (Sense only):** the Bluetooth core ships its own `PDM` library whose global instance is built from this variant's `PIN_PDM_DIN/CLK/PWR`, so it powers and wires the mic itself — again no need for the mbed core the wiki points PDM users at. Its `DEFAULT_PDM_GAIN` is 20, but on the nRF52 PDM 40 is unity at 0.5 dB a step, so **stock settings run the mic 10 dB below unity** and it barely reacts to a room. Proven by sweeping gain against the quiet-room floor — an instrument check needing no sound source: rms 6 / 29.5 / 62 / 167 at gain 20 / 40 / 60 / 80, i.e. +13.8, +20.3, +28.9 dB against +10, +20, +30 predicted. At the example's gain of 50 a real clap measured **30.7 dB above a floor of 42 rms, with 22.9 dB still in hand before clipping**. The gain is a working point, not a clip calibration — that needs a known loud source this bench lacked, and the sketch says so. First attempt at this measurement failed with the mic apparently deaf: the Mac was muted at volume 0, which is the Method's "suspect the instrument first" earning its keep again. |

**Renamed 2026-09-04**, from `XiaoBLEOscuino`: `BLE` sat in the *board* slot,
naming the nRF52840, while `XiaoC6ExpBLE` and `XiaoMG24BLE` use the same word
for the *transport* — one word meaning two things by position. The board's
advertised BLE name changed with it, so the Web Bluetooth picker now shows
`XiaoNrf52Oscuino`. The convention is written down in
[extras/webserial/README.md](./extras/webserial/README.md).

**OSC over Bluetooth LE.** `examples/XiaoNrf52Oscuino` is the first non-serial,
non-IP transport here, and it needed **no library change at all**: `_SLIPSerial<T>`
was written for `HardwareSerial` and for TCP `Client`, and Bluefruit's `BLEUart`
is an Arduino `Stream` with the same surface, so `_SLIPSerial<BLEUart>` is the
entire BLE transport. The same SLIP-framed OSC 1.0 bytes ride the Nordic UART
Service, and the sketch serves USB and BLE simultaneously, replying to whichever
asked. `XiaoNrf52Oscuino.html` is the Web Bluetooth counterpart of the Web Serial
pages. Verified over the air on 2026-08-30 from Chrome: advertising, connection,
writes in, notifications out, SLIP frames spanning several notifications, and
`/imu` streaming at 50 ms with the display tracking the board as it moved
(that run predates the rename, when the address was spelled `/xb/imu`).

**Two bugs that had to be measured, both silent.** BLEUart sizes its TX
characteristic with `setMaxLen(Bluefruit.getMaxMtu())`, which defaults to
`BLE_GATT_ATT_MTU_DEFAULT` = **23 bytes**, and `BLECharacteristic::notify()`
then does `min16(len, _max_len)` — it **truncates rather than fails**. An OSC
bundle is ~76 bytes, so every reply left the board cut to 23 bytes: inbound
writes worked perfectly (an LED obeyed), no frame ever completed on the
central, and nothing anywhere reported an error.
`Bluefruit.configPrphConn(BLE_GATT_ATT_MTU_MAX, …)` **before** `begin()` is
the cure. The second: this board's RGB LED is **active LOW**, though the
variant header declares `LED_STATE_ON = 1`. Trusting the macro inverted every
channel, so a commanded red arrived as cyan — dim red plus green and blue
driven fully on by their zeros. Settled by sending 255/255/255 (dark) against
0/0/0 (white).

**Host-side note:** macOS refuses CoreBluetooth to a CLI process without
Bluetooth TCC permission — a `bleak` scan dies with SIGABRT and no message, and
no prompt is ever offered — so BLE verification on this bench goes through
Chrome, which holds the permission and asks the user.

| SuperMini, bare (no OLED), MAC 90:70:69:ab:5d:c0 | ESP32-C3, single core 160 MHz, 4 MB | echo 22/22 · widths 11/11 · probe 7/7 · bench clean: gate, in 50 ×3, compound ×3, ring 20 — no same-day reference board ran, noted per Method · blue LED on GPIO8 seen lighting; 2026-09-02 | A second physical unit, and useful precisely because it is **missing the display**: `/display/text` answered 0 and the sketch served everything else untouched, which is the capability design proving it reports absence rather than pretending. Same `/s/d` 22 and `/s/a` 6 as the OLED unit, same active-low GPIO8 LED. It also sharpened the boot-mode note: this board arrived **strap-parked**, having been plugged in with BOOT held, and in that state `esptool run` is powerless — it prints "Hard resetting via RTS pin" and nothing starts. Only a clean power cycle (replug, no button) leaves it. Once out, `esptool run` starts the app normally after every subsequent flash, so the stickiness belongs to the strap-at-power-on, not to flashing. |

### Silicon Labs EFR32MG24 — a new family, and a bridge that wedges

| board | chip | ran | found |
|---|---|---|---|
| Seeed XIAO MG24 (Sense) | EFR32MG24 (M33), 1.5 MB flash, CMSIS-DAP interface chip | echo 22/22 · widths 11/11 (int=4 long=4 ll=8) · gate CLEAN · one-write bursts bisected: ≤242 B clean, 264–374 B lossy, ≥396 B lossy **and wedging** · the same 1100 B **paced** at 5 ms: CLEAN — see below; 2026-09-02 | First Silicon Labs part in this table, and it needed **no new rung** in `SLIPEncodedSerial.h`: `BOARD_HAS_USB_SERIAL` is correctly *not* defined for this core (probed with `#pragma message`), so a sketch falls back to `SLIPEncodedSerial(Serial)` — which is right, because `Serial` here is the UART bridged to the CMSIS-DAP interface chip's VCOM, not native USB. That places it with the UNO R4 in the **bridged-UART** family rather than any USB-stack row. FQBN carries a `protocol_stack` menu (Matter / BLE Arduino / BLE Silabs / None); the ladder above ran with `protocol_stack=none`. `examples/XiaoMG24BLE` adds the radio with `ble_silabs`: **both halves verified** — USB on 2026-09-02 (`/enq`, the standard set, the state packet and the LED), BLE on 2026-09-03 from Chrome through the Web Bluetooth page (it appeared in the picker, `/enq` answered over the air, the LED and the state stream worked). After the addresses were renamed onto `ADDRESSES.md` that same day, the renamed build was flashed and **re-verified over USB**: `oscprobe.py` clean (`/s/d` 19, `/s/a` 19, `/a/0` 352) and `contractprobe.py` 9/9 — `/rate 50` gave 21 `/state` packets in about a second with the sequence strictly increasing, `/rate 0` was followed by zero bytes, and the retired `/mg`, `/mg/led`, `/mg/rate` drew no reply. The BLE half has not been re-run since the rename. The core also ships **ezBLE**, which is already a `Stream` and would have needed no adapter at all, but its data characteristic is READ|WRITE with **no NOTIFY**, so a peripheral cannot push to a central; the example therefore builds a Nordic UART Service by hand with `sl_bt_gattdb_*` (pattern from the core's `ble_spp`) so that the one Web Bluetooth page serves this board too. |

**The wedge, and why the MG24 is not the culprit.** One `bench.py in 50 -1`
(a 1102-byte single write) stalled at 918 bytes, and from then on *every*
host write stalled at **0** bytes — `select()` never reports the port
writable. The evidence chain, because "the board hung" was the obvious and
wrong first guess:

* single-byte writes still succeed, so the OS, cable and port are fine;
* the bench gate fails immediately after a **confirmed** reflash — openocd
  prints "Programming Finished" and halts and resets the core — so the
  freshly reset MG24 application cannot be what is refusing data;
* reprogramming the target does not reset the **interface chip**, which is
  where the CDC endpoint lives.

So the wedge belongs to the CMSIS-DAP bridge, not to the MG24 or to this
library. It is worse than the UNO R4's bridged-UART behaviour, which merely
drops the tail of an oversized burst: this one stops accepting writes
altogether and survives target resets. **The cure is a physical replug**,
the only thing that power-cycles the interface chip — predicted from the
evidence above and then **confirmed**: the trickle gate ran CLEAN
immediately after one. Anyone meeting this will otherwise conclude the
board is dead.

**Measured limits.** The 918-byte figure in the first paragraph is *not* a
working ceiling — it is how much the bridge had swallowed at the moment it
died. Sweeping from clean states gives two distinct thresholds, and the
per-write size is what matters, not the traffic:

| one write | result |
|---|---|
| ≤ **242 B** (11 frames) | clean |
| **264 – 374 B** | **lossy**, but the port survives and the next gate passes |
| ≥ **396 B** (18 frames) | **lossy *and* the bridge wedges** until a physical replug |
| **1100 B total, paced** at 5 ms between frames | **clean**, and clean again afterwards |

So the rule for this board is *keep single writes small*, not *keep
traffic light*: the same 1100 bytes that wedge the bridge in one write pass
without a single loss when spaced out. A buffer of about 256 bytes in the
interface chip would explain both numbers — clean below it, lossy above it,
and a flow-control state machine that gives up somewhere past 1.5× — but
that is an inference from the thresholds, not something read out of the
chip, and it is recorded as such.

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
| M5Stack **M5Capsule** | ESP32-S3 (dual core, 240 MHz), 8 MB embedded GD flash, QSPI PSRAM, MAC c0:4e:30:11:4d:f8 | echo 22/22 · widths 11/11 (int=4 long=4 ll=8) · gate CLEAN · in 50 one-write ×3, out 200 ×3, compound ×3, ring 20 ×3 — all CLEAN; 2026-09-04 | **It beeps at you until you hold its power pin.** Flashed with a plain test sketch it beeped repeatedly, twice per cycle. `GPIO46` is a POWER-HOLD line: M5Unified drives it high as the *first statement* of `begin()`, before the display or anything else, for Capsule, Dial and DinMeter alike. A bare sketch that never calls M5Unified has to do it itself, so `OscEcho`, `IntWidths` and `OscBench` now assert it behind `ARDUINO_M5STACK_CAPSULE`/`_DIAL`/`_DINMETER`. Nothing else was needed: the transport is clean on every rung and wants no new detection rung. Stock FQBN `m5stack:esp32:m5stack_capsule` (the vendor core; `esp32:esp32:m5stack_capsule` also exists) — it sets `cdc_on_boot=1` itself. Other pins, from M5Unified rather than from guessing: button GPIO42, RGB LED GPIO21, SD card on SPI clk 14 / MOSI 12 / MISO 39 / CS 11, PDM mic data 41 / ws 40, internal I2C 8/10 and Port.A I2C 13/15. No `LED_BUILTIN` in the variant, so `BOARD_HAS_LED` is undefined and there is correctly no `/s/l`. Identified by asking: the chip is an ESP32-S3 like four other rows here, and its USB descriptors only say "Espressif JTAG" until a sketch built from this variant runs — the variant sets `USB_PRODUCT "Capsule"`, so after flashing the board names itself. |
| Seeed XIAO ESP32-C3 | ESP32-C3 (RISC-V, 160 MHz), 4 MB flash (mfr 0x46, dev 0x4016), MAC e0:72:a1:1c:bf:f8 | echo 22/22 · widths 11/11 (int=4 long=4 ll=8) · gate CLEAN · in 50 one-write ×3, out 200 ×3, compound ×3, ring 20 (up to 1100 B against a lazy reader) ×3 — all CLEAN; 2026-09-04 | **Stock FQBN defaults**: `esp32:esp32:XIAO_ESP32C3` already sets `build.cdc_on_boot=1`, so do **not** add `:CDCOnBoot=cdc` — that option belongs to the generic `esp32c3` devkit, whose boards.txt defaults it to 0. Native USB-Serial-JTAG, `303a:1001`. Transport is clean on every rung of the ladder and needs no new detection rung. **Identified wrongly at first, which is the lesson:** `chip_id` said ESP32-C3 and the VID/PID said Espressif, so it was taken for the EGG SuperMini and flashed with that demo — wrong pin map, wrong LED, and a capability probe that reported "no display" when it was really looking at the wrong I2C pins. Every native-USB ESP32 enumerates as `303a:1001`; the chip does not identify the carrier. See BRINGUP.md Phase 0, *The chip is not the board*. No same-day reference board ran, noted per Method. No demo sketch or page yet — it has no `boards.json` entry. |
| Seeed XIAO ESP32-C6 + XIAO Expansion Board | ESP32-C6 (RISC-V, 160 MHz) | `XiaoC6ExpOscuino` end to end: OLED, buzzer, button, LED · I2C bus scanned | **Stock FQBN defaults** — this variant sets `cdc_on_boot=1`, unlike the generic C6 devkit that needs `:CDCOnBoot=cdc`. Bus scan found 0x3C (SSD1306), 0x51 (PCF8563 RTC), 0x57 (EEPROM); SDA=GPIO22/D4, SCL=GPIO23/D5, buzzer D3, button D1 active-LOW. `A3` does not exist on this variant (only A0–A2) though Seeed's wiki names the buzzer pin that way — use `D3`. The boot `/enq` is never seen: the USB device re-enumerates after reset and the host opens the port later, so `/enq` is also an inbound address the client asks for. `XiaoC6ExpBLE` is the third transport for this board — the same vocabulary over Bluetooth LE — and its **BLE path is verified over the air** — first on 2026-08-30, then again on **2026-09-04** after the address rename, when it also found a real defect in the adapter. Chunking each bundle into 20-byte notifications 3 ms apart is *inside the connection interval*, so `setValue()` kept overwriting payloads the radio had not yet sent: on the air, bundles arrived truncated or with foreign bytes spliced between the timetag and the first element, and elements came back out of order, while the identical bundles over USB were byte-perfect. `ble_stream.h` now asks what MTU the central actually granted and sends a whole bundle in one notification. The clean run after that: the `/enq` greeting, `/s/l` and `/buzz` echoes, and 12 consecutive `/state` bundles, sequence 2208..2219 with no gaps, board-side `millis` exactly 100 ms apart; `/rate 0` echoed and the stream stopped dead. Note the lesson: "it decoded, so reassembly works" was the *old* claim, and it held only because the corruption was intermittent — the sequence counter is what made the gaps visible. The peripheral lines remain untested: this C6 was bare, so `displayOK` was false and the OLED, buzzer and button addresses answered without driving anything. It is worth reading anyway for what it demonstrates: `_SLIPSerial<T>` is a template over anything Stream-shaped, so on the nRF52840 BLE cost nothing at all because Bluefruit's `BLEUart` already is a Stream. The ESP32 BLE library has no such class, so `ble_stream.h` supplies one in ~60 lines of buffering and the transport line becomes identical: `_SLIPSerial<BLEStream>`. Two unrelated BLE stacks, one abstraction, one set of OSC bytes — and since both advertise the Nordic UART Service, `XiaoNrf52Oscuino.html` drives either board. `XiaoC6ExpWiFi`, the WiFi twin, **ran on 2026-08-30** and closed TODO 3.3's radio list: association, the UDP listener and its reply to `Udp.remoteIP()` (40/40 round trips), and all three HTTP routes including the NUL-carrying raw `POST /osc` body and `Access-Control-Allow-Origin` on both `GET /state` and the OPTIONS preflight. The IP-on-OLED line stays unverified: the C6 was bare for this run, so `/enq` reported `display:false, rtc:false` and the sketch degraded exactly as its capability booleans intend. It also earned a fix, measured A/B three runs each way at RSSI −67: with the ESP32's default modem power save the UDP round trip ran a **112 ms median, 353 ms p90, 819 ms worst** and ping averaged 147–226 ms; `WiFi.setSleep(false)` cut that to a **10 ms median, 41 ms p90, 70 ms worst** with ping at 17–23 ms. No loss either way at this signal strength — the cost is pure latency and jitter from DTIM beacon batching, which at the EGG C3's −91 dBm compounded into outright loss instead. |
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
* **RP2350 without touching the drive** — the 1200-baud touch
  (`stty -f PORT 1200 hupcl`, whose close drops DTR) puts an arduino-pico
  sketch into BOOTSEL immediately, and `picotool load file.uf2 -x` then
  programs it over the boot interface and runs it; `picotool info -a` at
  that point reports the true flash size. This is the route when the
  mounted `RP2350` volume cannot be written to (macOS TCC denies it to a
  CLI process). Measured on the Elecrow kit 2026-09-04, four cycles.
  `picotool load -f`, which asks the *running* sketch to reboot, found no
  reset interface in the factory firmware and does not replace the touch.
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

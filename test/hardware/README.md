# Hardware-in-the-loop probe

Talks SLIP-framed OSC to a board running one of the Oscuino sketches, over USB
CDC. Standard library only — it opens the tty and sets raw mode with `termios`,
so there is nothing to install.

```sh
arduino-cli compile --fqbn adafruit:samd:adafruit_gemma_m0 \
    --upload --port /dev/cu.usbmodemXXXX examples/GemmaOscuino
python3 test/hardware/oscprobe.py /dev/cu.usbmodemXXXX
```

It checks what the host suite structurally cannot: that a real board, behind a
real USB stack, actually answers.

| probe | what it would catch |
|---|---|
| A single frame in one write | the sketch does not respond at all |
| B the same frame one byte at a time, 6 ms apart | inbound state discarded between `loop()` iterations — this is the case that fails when `bundleIN` is declared inside `loop()` |
| C a bundle carrying an `'I'` argument beside a routed message | a zero-byte type making the decoder reject the whole bundle |
| D two frames in a single write | SLIP losing a packet boundary when the next frame is already buffered |
| E `/s/d`, `/s/a` | pin-count clamping |
| F `/a/0` | the ordinary read path |

## Why B matters

With `bundleIN` declared inside `loop()`, this probe returns **zero frames for
every case, including A** — the board answers nothing at all. Hoisting the
bundle to file scope is what makes the sketch work; verified on an Adafruit
Gemma M0 by flashing both variants against the same library.

## The other probes

`echotest.py` — flash `OscEcho/`, which decodes an inbound packet with the
library and re-encodes it with the library. A byte-identical reply means decoder
and encoder agree on the wire format *on target*. 22 cases covering every type,
string and blob lengths 0-5, empty strings and zero-length blobs next to other
arguments, impulse/null mixed with real data, bundles and timetags.

`widths.py` — flash `IntWidths/`, which builds one message from all eleven
integer spellings. Checks which OSC type tag each produced on the target and
reports `sizeof(int/long/long long/double)`. This is the only test of the
integer-dispatch fix on a platform where `int` is 16 bits and `long` is 32.

`stress.py` — up to 50 SLIP frames back to back in a single write, each carrying
its own index, to look for lost packet boundaries.

## Probe F takes an analog address

Not every variant defines `A0` — the M5Stack NanoC6 starts at `A1` — so the
analog read is a parameter, not a constant:

```sh
python3 oscprobe.py /dev/cu.usbmodemXXXX        # defaults to /a/0
python3 oscprobe.py /dev/cu.usbmodemXXXX /a/1   # boards with no A0
```

## The ATmega32U4 receive stall: a zero-length packet the core never releases

`stress.py` found inbound bursts truncated and a 32U4 refusing to receive until
reset. It is not this library, and it is not memory. It has a single, exact
cause.

**Not memory.** `MemDiag/` reports `freeRam()` on every packet: free RAM sits
flat at **2231 bytes** right across the cliff, `minFree` never dips,
`getError()` stays 0, no `ALLOCFAILED`.

**Not this library.** `RawCount/` contains no OSC and no SLIP — just
`Serial.available()`/`Serial.read()` counting bytes — and fails identically.

**The cause.** A USB host terminates any transfer that is an exact multiple of
the endpoint size with a **zero-length packet**. The 32U4 CDC endpoint is 64
bytes, so a 64, 128 or 256 byte write is followed by a ZLP. In
`USBCore.cpp`:

```c
u8 USB_Available(u8 ep) { LockEP lock(ep); return FifoByteCount(); }   // never releases

int USB_Recv(u8 ep, void* d, int len) {
    ...
    if (len && !FifoByteCount())   // len == 0 for a ZLP, so this never fires
        ReleaseRX();
}
```

The bank a ZLP lands in is never released, so the endpoint stalls forever.
Transmit is unaffected — the board goes on reporting while accepting nothing.

Measured on a LilyPad USB, one write at a time from a freshly reset board:

| write | result |
|---|---|
| 63 B, 63 B, 63 B, 100 B, 127 B, 191 B | all received in full, repeatedly |
| **64 B** | received — then **every later read returns 0, permanently** |
| **128 B** | same |

### Fixed in the library

`SLIPEncodedSerial.h` now releases the stuck bank itself, so no replacement core
and no cooperating host is required. `oscReleaseStuckCdcRxBank()` is called from
`_SLIPSerial::available()` and `endofPacket()`, compiles to nothing off the
32U4, and only ever fires when a packet has been received (`RXOUTI`) with
nothing to read (`RWAL` clear) — an empty bank — so it cannot discard data.

Every ATmega32U4 board is affected, the **Arduino Esplora included** — which
matters here, because `examples/EsploraOscuino` takes `/rgb`, `/tone`, `/d/3`
and `/d/11` from a web page and would stall on any inbound write that landed on
a multiple of 64. Confirmed active by compiling a guard probe: Esplora, LilyPad
USB, Leonardo and Circuit Playground 32U4 all report the workaround compiled
in; Uno, Teensy 4.0 and Gemma M0 report it compiled out.

The `__AVR_ATmega32U4__` term in that guard is load-bearing, not decoration:
SAMD cores also define `CDC_RX`, so guarding on `CDC_RX` alone would have
compiled AVR register access into SAMD builds.

Measured on a LilyPad USB with `ZlpTest/`, using **unpadded** writes, the ones
that used to be fatal:

| write | before | after |
|---|---|---|
| 64, 128, 256, 64, 64 B | first one kills reception for good | all received, board stays healthy |

and `stress.py`, which does not pad either, went from losing frames and
wedging to not wedging. (burst figure withdrawn — see the note above the Measured table), so the
before/after counts are not quoted here; the ZLP mechanism above is
established by the register behaviour and the padded/unpadded A/B, not by
those counts.

### Other ways to avoid it

[ATUSB_Core](https://github.com/adammhaile/ATUSB_Core) fixes it properly. Its
core is Teensy-derived, and both `available()` and `read()` detect the stuck
bank and release it:

```c
n = UEBCLX;
if (!n) { i = UEINTX;
  if (i & (1<<RXOUTI) && !(i & (1<<RWAL))) UEINTX = 0x6B; }   // release it
```

`read()` does the same and retries. That is exactly the case stock Arduino
misses. It needs an ICSP programmer, though — it ships no bootloader.

Without changing cores, the host can simply never send a multiple of 64. The
web pages here do that in `padAwayZLP()`: if a SLIP frame's length is a
multiple of 64, append one extra `END` byte. An extra `END` closes an empty
frame, which every SLIP decoder skips, so it costs nothing. Verified by A/B on
one board:

| | |
|---|---|
| 64, 128, 256, 64 B **padded** | all received, board stays healthy |
| 64 B **unpadded**, same board | received — and the next 63 B write returns 0 |

Boards with a real software RX buffer do not wedge the way a 32U4 does: they
may drop under load but recover on the next quiet moment. (burst figure withdrawn — see the note above the Measured table).

## Transmit throughput

`_SLIPSerial` used to hand the port one byte at a time: `write(buffer, size)`
looped over the single-byte `write()`, which called `serial->write(b)` per byte.
On a USB CDC port each of those is a function call with interrupts disabled, an
endpoint select and a FIFO check, and that overhead dominated everything else.
It also returned the result of the *last* write rather than the total, so a
caller checking the count saw 1 for any length.

Escaped bytes are now collected in a small buffer (`OSC_SLIP_TX_BUFFER`, 64 by
default) and handed over with one `serial->write(buf, n)` per block.
`endPacket()` and `flush()` drain it, so nothing is ever held past a packet
boundary. Measured with `TxBench/`, a 60-byte OSC message sent 200 times:

| board | before | after | |
|---|---|---|---|
| ATmega32U4 (LilyPad USB) | 2773 us/packet | **1348 us/packet** | 2.06x |
| Teensy 4.0 | 17 us/packet | 16 us/packet | unchanged |

The 32U4 gain is the whole point; Teensy shows nothing because its core already
buffers writes behind a flush timer, so the per-byte path was never its
bottleneck. At 60 bytes the 32U4 is now near the 1 ms USB full-speed frame, so
it has gone from CPU-bound to frame-bound and there is little left to win
without batching packets, which would cost latency.

Cost: 64 bytes of RAM and about 50 bytes of flash. Set `OSC_SLIP_TX_BUFFER`
before including the header to change it.

Verified byte-exact after the change: `echotest.py` 22/22 and `stress.py`
clean on both an ATmega32U4 and a Teensy 4.0. The Teensy matters
particularly, because its `endPacket()` template specialisation calls
`send_now()` and had to be taught to drain the buffer too — without that it
would have discarded every packet.

## Allocation on small parts — worth knowing, not the cause of the above

Since the cliff turned out not to be memory, this is recorded as a separate
observation rather than a fix. Decoding an N-argument message costs roughly
2N heap operations: one `new OSCData` per argument, plus a
`realloc(data, sizeof(OSCData*) * (dataCount + 1))` that grows the pointer
array by exactly one pointer each time, plus another `malloc` for every string
or blob. `incomingBuffer` grows by `realloc` in 16-byte steps
(`OSCPREALLOCATEIZE`). On a part with 2.5 KB of SRAM and no heap compaction
that is a lot of small blocks and a lot of churn, and it is fragmentation-prone
even though it did not fail here. Growing the pointer array geometrically, and
reserving `incomingBuffer` once, would both be cheap improvements.

## The other probes

`echotest.py` — flash `OscEcho/`, which decodes an inbound packet with the
library and re-encodes it with the library. A byte-identical reply means decoder
and encoder agree on the wire format *on target*. 22 cases covering every type,
string and blob lengths 0-5, empty strings and zero-length blobs next to other
arguments, impulse/null mixed with real data, bundles and timetags.

`widths.py` — flash `IntWidths/`, which builds one message from all eleven
integer spellings. Checks which OSC type tag each produced on the target and
reports `sizeof(int/long/long long/double)`. This is the only test of the
integer-dispatch fix on a platform where `int` is 16 bits and `long` is 32.

`stress.py` — up to 50 SLIP frames back to back in a single write, each carrying
its own index, to look for lost packet boundaries.

## Probe F takes an analog address

Not every variant defines `A0` — the M5Stack NanoC6 starts at `A1` — so the
analog read is a parameter, not a constant:

```sh
python3 oscprobe.py /dev/cu.usbmodemXXXX        # defaults to /a/0
python3 oscprobe.py /dev/cu.usbmodemXXXX /a/1   # boards with no A0
```

## Inbound bursts are truncated, and AVR does not recover

`stress.py` found a hard limit on how much can arrive in one burst. The cutoff
tracks bytes, not frames — on an ESP32-S3, delivered frames fell 16 → 11 → 8 → 4
as frame size grew 18 → 26 → 42 → 74 bytes, with bytes-through pinned near 300.
AVR cuts off around 378 bytes. That is the USB CDC receive buffer overflowing
with no flow control; traffic at normal rates is unaffected, and Teensy's
buffers are large enough that 50 frames never reach it.

On HWCDC (the ESP32 USB-Serial-JTAG peripheral) the ceiling is exact and
reproduces across chips. The burst figures for this are withdrawn pending re-measurement, but the
effect itself is board-side: it is removed by a firmware-only change. **Measured identically on four parts across both instruction sets** -- an
ESP32-C3, an ESP32-C6 (RISC-V), an Adafruit QT Py ESP32-S3 and a Seeed XIAO
ESP32S3 Sense (Xtensa) -- same boundary, same missing indices `[19..]`,
which is what rules out a per-part quirk: it is the core's shared HWCDC
driver and its default 256-byte rx ring, the small overshoot being what the
sketch drains while the burst is still arriving.
`Serial.setRxBufferSize(1024)` before `begin()` takes **both** boards to
50/50 back-to-back frames clean, on the same build otherwise, which pins the
mechanism to the ring rather than to this library. All four runs measured
2026-08-03. Unlike AVR, both recover by themselves: frames after the dropped
tail decode normally.

Worse, on AVR the sketch does not recover: after one overflowing burst every
subsequent well-formed frame is lost until the board is reset. Not yet isolated
— `OscEcho.ino` caps its own buffer at 600 bytes, so this may be the sketch
rather than the library's SLIP state machine. Isolating it needs a sketch that
drives only the SLIP layer.

## Measured

> **The `stress` column is withdrawn pending re-measurement.** `Port.write()`
> in `oscprobe.py` called `os.write()` on a non-blocking fd and discarded the
> return value, which returns a short count once the kernel tty buffer fills
> (1024 bytes on macOS). Every burst figure below was taken with that
> instrument. The bug is fixed, but the numbers have not been re-taken, so
> they are removed rather than left looking authoritative. `echo` and
> `widths` do not route through the suspect path: `widths` is reported by the
> board itself, and `echo` is a byte-exact comparison of small frames.

| board | echo | widths | probe | stress |
|---|---|---|---|---|
| LilyPad USB (ATmega32U4) | 22/22 | 11/11, int=2 long=4 ll=8 double=4 | 7/7 | not re-measured |
| Teensy 4.0 (ARM M7) | 22/22 | 11/11 | 7/7 | not re-measured |
| Teensy 3.2 (ARM M4) | 22/22 | 11/11 | — | not re-measured |
| HalloWing M0 Express (SAMD21) | 22/22 | — | — | not re-measured |
| DFRobot Beetle RP2040 | 22/22 | 11/11, int=4 long=4 ll=8 double=8 | — | not re-measured |
| Seeed XIAO RP2350 (Cortex-M33) | 22/22 | 11/11, int=4 long=4 ll=8 double=8 | — | not re-measured |
| ESP32-C3 (RISC-V) | 22/22 | 11/11 | — | not re-measured |
| QT Py ESP32-S3 (Xtensa, HWCDC) | 22/22 | 11/11 | — | not re-measured |
| Seeed XIAO ESP32S3 Sense (Xtensa, 8 MB PSRAM) | 22/22 | 11/11 | — | not re-measured |
| Gemma M0 (SAMD21) | — | — | 7/7 | not re-measured |
| moddo pinch (SAMD11) | 22/22 | 11/11 | — | not re-measured |
| ESP32-C6 (RISC-V) | 22/22 | 11/11 | — | not re-measured |
| M5Stack StampS3 (Xtensa) | 22/22 | 11/11 | 7/7 | not re-measured |
| Adafruit Feather M4 Express (SAMD51) | 22/22 | 11/11, int=4 long=4 ll=8 double=8 | — | not re-measured |
| LilyPad USB | — | — | — | not re-measured |
| UNO R4 WiFi (RA4M1, bridged UART) | 22/22 | 11/11 | — | not re-measured |
| Seeed XIAO RA4M1 (RA4M1, native USB) | 22/22 | 11/11 | — | not re-measured |
| M5Stack NanoC6 | not run — board stopped responding | | | |

The 2026-08-03 rows (LilyPad stress, Teensy widths, Beetle RP2040, ESP32-C3,
UNO R4 WiFi, QT Py ESP32-S3, XIAO RP2350) were run against the 4.0.0 tree after the SLIP-over-TCP
collapse, so they also stand as the regression evidence for it on real USB
stacks. The Teensy `widths` and RP2040 rows close gaps that were dashes
before: integer dispatch had never been checked on an ARM M7, and no RP2040
had ever run this library on hardware at all.

## The UNO R4 WiFi is the first board here that is not native USB

Every other board in the table above presents a native USB CDC endpoint,
where the line rate is a formality the device ignores. The **UNO R4 WiFi**
is not: it builds with `-DNO_USB`, so the core does `#define Serial _UART1_`
and its `Serial` is a real UART that the on-board ESP32-S3 bridges to the
host. Host and board must therefore agree on a baud rate.

`Port` in `oscprobe.py` set raw mode but never set one, which no board had
ever cared about. On the R4 WiFi that mismatch does not look like a
mismatch — the board replies, and the reply arrives as framing noise
(`EF ED EF ED EC ...`), which reads exactly like a board that is not running
your sketch. `Port.BAUD` now matches the sketches' `begin(115200)`.

Two things follow for anyone testing this board. The hardware sketches had
hardcoded `SLIPEncodedUSBSerial`, which does not exist where
`BOARD_HAS_USB_SERIAL` is undefined, so all five now carry the same
`#ifdef` the examples use. And uploads to this board **intermittently do not
take**: three separate flashes here reported success, wrote no
`Write NNNN bytes to flash` line, and left the previous sketch running.
Re-flashing fixed it every time. Check that the sketch you think is running
actually is before believing a failure — the diagnostic that settled this
one printed `av=8 n=8`, proving the SLIP layer had decoded the frame
correctly all along.

## ESP32 USB CDC defaults differ per board -- read boards.txt, do not guess

Three ESP32 boards here need three different FQBN option sets, and getting it
wrong gives a board that flashes, verifies its hash, and then says nothing at
all -- indistinguishable from a dead sketch.

| board | what works | why |
|---|---|---|
| `esp32:esp32:XIAO_ESP32S3` | **stock defaults, no overrides** | `build.cdc_on_boot=1` already |
| `esp32:esp32:esp32c6` | `:CDCOnBoot=cdc` | `build.cdc_on_boot=0`, so `Serial` is UART0, not USB |
| `esp32:esp32:esp32c3` | `:CDCOnBoot=cdc` | same |
| `esp32:esp32:adafruit_qtpy_esp32s3_n4r2` | `:USBMode=hwcdc,CDCOnBoot=cdc` | its default USB-OTG/TinyUSB mode enumerates nothing here |

Carrying one board's setting to another is what costs the time: passing the
QT Py's `USBMode=hwcdc,CDCOnBoot=cdc` to a XIAO ESP32S3 silences it
completely. Check `grep '^<board>.build.cdc_on_boot' boards.txt` and the menu
order -- the first `menu.CDCOnBoot.*` entry listed is the default.

The fastest way to tell "wrong USB setting" from "broken sketch" is the
vendor's own first check: flash a Blink that also prints. If the LED blinks
and nothing arrives, it is the USB configuration, not your code.

## The QT Py ESP32-S3 needs `USBMode=hwcdc`, and two button presses

This board defaults to `USBMode=default` — USB-OTG via TinyUSB — and at that
setting **nothing reaches the host at all**, not even a bare
`Serial.println()` in a sketch with no OSC in it. Built with
`USBMode=hwcdc,CDCOnBoot=cdc` the same sketch prints immediately. Use:

```sh
arduino-cli compile -b esp32:esp32:adafruit_qtpy_esp32s3_n4r2:USBMode=hwcdc,CDCOnBoot=cdc \
    --upload --port /dev/cu.usbmodemXXXX test/hardware/OscEcho
```

Flashing it the first time needs the manual ROM sequence — hold BOOT, tap
RESET, release BOOT — because the board has no USB-serial chip and so no
DTR/RTS auto-reset circuit for esptool to drive. Neither esptool's reset nor
the 1200-baud touch does anything; verified by watching the port list for 48
seconds across an upload attempt, during which no new port ever appeared.
**After flashing it also needs a plain RESET press** to leave download mode
and run the app; `esptool ... run` was not sufficient. Once it is running an
application, subsequent uploads reset cleanly on their own and need no
buttons.

Both quirks look exactly like a broken library from the host side — silence,
then a board that flashes successfully and still says nothing.

## The Esplora is the largest packet this library produces

`examples/EsploraOscuino` puts the whole board state in one bundle: 38
messages, floats, booleans, strings and ints together. Listening to a board
already running it, **199 of 199 SLIP frames decoded, 0 failures**, against
`oscprobe.decode`, which is written from the OSC 1.0 spec and shares no code
with the library. Frames ran 400 to **1316 bytes** — several times anything
else exercised here, and well past the 512-byte `OSC_MAX_INCOMING` an AVR
would apply on the *inbound* path, which is worth remembering before that cap
is tuned. Measured 2026-08-03 over its custom USB descriptor (`OSC Esplora`,
`0x6666/0x1099`).

## Boards this library cannot serve

The classic **Adafruit Trinket (ATtiny85)** has no hardware UART and its
`tiny8` variant declares no `Serial` object, so `SLIPEncodedSerial` has
nothing to bind to: `adafruit:avr:trinket5` fails to compile with `'Serial'
was not declared in this scope`. Its V-USB bootloader is programming-only,
not CDC, so there is no USB serial route either. The **Pro Trinket**
(ATmega328P) is fine over its UART — `SerialSendMessage` builds at 4766
bytes, 16% of its 28672. Both share the USBtiny bootloader identity
(`0x1781/0x0c9f`), so a plugged-in board cannot be told apart by its USB
descriptor alone.

The stress column records where inbound bursts start being dropped; see the
section above for why that is a property of the board's USB stack.

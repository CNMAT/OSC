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

## Method: how a number gets in here

Every withdrawn figure in this directory's history was the instrument, not
the board: a discarded short-write count, a sketch that was not the one
running, a wrong port, a wrong USB build flag, a watcher process causing the
TX activity it reported. The method below exists so that class of error is
caught before a number is written down, not after it is published.

**Attribute, never assert.** The pipeline has four places to count:

```
host wrote --USB--> board received --sketch--> board sent --USB--> host received
    A                    B                        C                    D
```

`bench.py` holds A and D; `OscBench/` on the board holds B and C, as
sequence-numbered, CRC-carrying frames it counts but never answers, reporting
only when asked (`/b/q`) after traffic has quiesced. A missing frame is then
a *segment*: A→B is host→device (kernel tty, USB, device RX ring, drain
rate); C→D is device→host; B vs C is the sketch. "Lost somewhere" is not a
result.

**The physics sets the default hypothesis.** USB bulk endpoints flow-control
by NAK: a native-CDC device that has not freed a buffer NAKs the host, the
host retries, and nothing is lost — only slowed. Silent host→device loss is
therefore only possible *above* the USB layer, and every confirmed case so
far has been exactly that: a firmware ring the hardware ACKs into regardless
of space (ESP32 HWCDC), or an endpoint bank the core never frees (32U4 ZLP
stall). A "cliff" claim without a nameable mechanism of that kind starts as
an instrument bug until proven otherwise.

**Rules, all learned the expensive way:**

1. *Trickle gate.* `bench.py PORT verify` — 20 frames at 20/s — runs before
   anything fast. If slow traffic is not 100%, the setup is broken and no
   fast number means anything. `bench.py` enforces this itself.
2. *Repeats.* Every condition runs 3×. A cliff that does not reproduce is
   not a cliff.
3. *Calibration.* A loss figure counts only if the same command, same host,
   same day, ran clean on a known-good reference (Teensy 4.0, or any
   native-CDC SAMD). If the reference fails too, the instrument is the
   suspect.
4. *Mechanism required.* A number enters this file only with the mechanism
   named — a ring size, a register, a code path — and ideally an A/B that
   flips it (`setRxBufferSize`, padding, a one-line core patch).
5. *Direction separated.* Host→device (`bench.py in`) and device→host
   (`bench.py out`) are different failure domains and are never summed.

`bench.py PORT ring LAZY_MS` maps a receive ring empirically: bursts of
increasing size in one kernel write against a deliberately slow application;
where the received count stops tracking the burst size is the ring.

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

This is [arduino/ArduinoCore-avr#112](https://github.com/arduino/ArduinoCore-avr/issues/112),
open since 2018 and unfixed upstream; the issue also records the register
ordering a correct fix needs (clear `RXOUTI` before `FIFOCON`). Two wording
precisions that matter in practice: the trigger is a USB *transfer* ending
on an exact multiple of the endpoint size — the kernel coalesces
application writes, and macOS both appends CDC ZLPs and chunks tty output
at 512 bytes, itself a 64-multiple — so "avoid 64-multiple writes" is
host-shaped shorthand, not the invariant. And the fatal multiple is
`USB_EP_SIZE`: 64 on every stock build, but a core built with the
documented 16-byte alternative moves it, which any padding remedy that
hardcodes 64 would miss.

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

## The transmit side has its own loss budget

Host→device is where the burst claims lived, but the researchers' pass over
the cores found the *silent* discards clustered on device→host, where
`Print` swallows errors — every one of these loses data with no indication
beyond `setWriteError()`, which nothing checks:

* **AVR 32U4** — `USB_Send()` busy-waits 250 ms for bank space, then gives
  up and returns -1; and `Serial_::write()` discards everything outright
  while the host has the port closed (`lineState == 0`).
* **Teensy 3.x** — TX gives up after a 70 ms timeout; and RX + TX share one
  pool of twelve 64-byte packet buffers across *all* endpoints, so an
  inbound flood can starve outbound.
* **RP2040 core** — `SerialUSB.write()` blocks about 1 s, then gives up.

An echo test conflates these with receive loss; `bench.py out` measures the
direction alone, and its C-vs-D counters are how a device-TX give-up shows
itself (frames missing from the tail at D with B intact).

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

`bench.py` + `OscBench/` — the loss-attributing bench; see *Method* above.
It replaces `stress.py`, whose design could not say WHERE a frame went
missing and whose write path turned out to be dropping bytes on the host.

`test_host.py` — the harness testing itself, host-only, seconds to run:
codec round-trips, and `write_all()` against an fd that guarantees short
writes, with a negative control proving the old broken pattern fails it.

## Probe F takes an analog address

Not every variant defines `A0` — the M5Stack NanoC6 starts at `A1` — so the
analog read is a parameter, not a constant:

```sh
python3 oscprobe.py /dev/cu.usbmodemXXXX        # defaults to /a/0
python3 oscprobe.py /dev/cu.usbmodemXXXX /a/1   # boards with no A0
```

## The burst claims, sorted by what survives review

Three burst-loss claims were made against the old instrument. Reviewed under
the Method rules, they sort into one confirmed, one reclassified as suspect,
and one withdrawn outright.

**Confirmed — ESP32 HWCDC needs the host to throttle bursts — but the drop
site is the driver, not the peripheral.** The USB-Serial-JTAG hardware
itself flow-controls correctly: the S3 TRM (ch. 33.3.3) says the controller
accepts a packet only "when [it] has a free buffer" — it NAKs the host until
firmware drains its single 64-byte OUT FIFO. The silent loss is one layer
up, in `cores/esp32/HWCDC.cpp`: the receive ISR unconditionally drains those
64 bytes on every packet interrupt and pushes them byte-by-byte into a
FreeRTOS queue with the send result unchecked-on-full — bytes that do not
fit are discarded, and because the FIFO was emptied the hardware happily
takes the next packet. The queue defaults to 256 bytes, created at runtime
in `begin()` (no build-time macro), so total device-side buffering is
~256 + 64 in flight. That matches the measurement: reproduced identically on
four parts across both instruction sets (ESP32-C3, ESP32-C6, QT Py ESP32-S3,
XIAO ESP32S3 Sense), same missing indices `[19..]`, and 19 frames × ~14 SLIP
bytes ≈ 266 bytes ≈ queue + what the sketch drained mid-burst.
`Serial.setRxBufferSize(1024)` on the same build takes 50/50 clean. The
finding also survives the instrument post-mortem on its own: those ~700-byte
bursts sat under the 1024-byte kernel buffer where the short-write bug
engaged. Remedies: enlarge the queue — **before** `begin()`, since resizing
discards buffered data and a mis-ordered call silently reverts to the lossy
default — or pace bursts to what the application drains. ESP-IDF's own
usb_serial_jtag driver has the same unchecked-send pattern, so this is not
Arduino-layer-only.

**Suspect — the "AVR cuts off at ~378 bytes and never recovers".** A 32U4
has no software rx ring at all: reads come straight from the 64-byte
endpoint banks and a full bank NAKs the host, so silent loss should be
impossible there. The reinterpretation this review proposes: the
"no recovery until reset" was the ZLP stall (previous section) wearing a
throughput costume — the kernel coalesces buffered bytes into transfers, and
the first transfer to land on an exact multiple of 64 stalls reception for
good — and the byte figure was the echo sketch's own 600-byte buffer, not
the USB stack. Both are now testable: the library releases the stuck bank
itself, and `bench.py PORT in 50 -1` attributes any loss to a segment. No
AVR burst number stands until that has been run.

**Unresolved — "Teensy 3.x loses 4 of 50 burst frames."** The original
*attribution* ("smaller USB buffers") is withdrawn: PJRC's RX path NAKs like
every native CDC stack, and — by the same arithmetic that defends the HWCDC
finding — a ~700-byte burst sat under the host's 1024-byte buffer, so the
short-write bug cannot explain this one either. But that cuts both ways,
and same-day references on the same instrument ran 50/50 clean, which by
this file's own calibration rule points at the board, not the tool. Two
candidate mechanisms remain, one per side, and `bench.py`'s B-vs-D counters
decide between them: (a) instrument — `stress.py`'s fixed drain window
counted late echoes as lost (see the post-mortem note above); (b) device —
Teensy 3.x serves all endpoints from one shared pool of twelve 64-byte
packet buffers (`NUM_USB_BUFFERS`, usb_desc.h), so an echo test's inbound
flood and outbound replies compete for the same twelve buffers, and its TX
side gives up silently after a 70 ms timeout. If a Teensy 3.x bench run
shows B=50, D=46 with the tail missing, the loss is real and lives in
device TX; B=D=50 retires it as the drain window. Neither has been run.

## Measured

> **The `stress` column is withdrawn and its instrument retired — for two
> defects, not one.** First, `Port.write()` discarded the short count
> `os.write()` returns on a non-blocking fd; the macOS tty output queue is
> a flat 1024 bytes (`TTYCLSIZE`, xnu tty.c), and the engagement condition
> is burst *plus current queue occupancy* over 1024 — against a device that
> is NAKing (say, a ZLP-wedged 32U4) it engages at any write size, so the
> contamination is wider than "bursts over 1024". Second, `stress.py`
> counted replies inside a fixed drain window (1.2 s + 4 ms/frame), so a
> slow echoer's late frames were scored as lost. `bench.py` retires both:
> `write_all()` loops on the short count (regression-tested with a negative
> control in `test_host.py`), and the board reports its own counters after
> quiescence instead of racing a window. `echo` and `widths` never routed
> through either defect: `widths` is reported by the board itself, and
> `echo` is a byte-exact comparison of small frames.

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
| Adafruit EdgeBadge (SAMD51) | — | — | — | not re-measured |
| UNO R4 WiFi (RA4M1, bridged UART) | 22/22 | 11/11 | — | not re-measured |
| Seeed XIAO RA4M1 (RA4M1, native USB) | 22/22 | 11/11 | — | not re-measured |
| M5Stack NanoC6 | not run — board stopped responding | | | |

The EdgeBadge's PDM microphone is exercised by `examples/PyBadgeOscuino`, not
by these suites. Measured there, at `MIC_GAIN` 16 over 30 s of speech and
taps: a quiet-room floor of rms 20 (-64 dBFS) holding steady to within
1 count between events, rising to rms 427 (-48.5 dBFS) on speech -- 26.6 dB
of range -- with the loudest transient pinning `peak` at exactly 32767, which
is the driver clipping. The same code on a SAMD51 with no microphone on those
pins gives a flat zero and reports `micOK` false.

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

The stress column stays empty until a board has been measured with
`bench.py` under the Method rules — trickle gate, 3× repeats, same-day
reference calibration, mechanism named.

## Primary sources for the per-stack flow-control facts

Researched 2026-08-11, each verified against the source named:

* AVR 32U4: `ArduinoCore-avr` `USBCore.cpp` (release-only-when-drained,
  `EP_DOUBLE_64`), `CDC.cpp` (no software ring),
  [issue #112](https://github.com/arduino/ArduinoCore-avr/issues/112) (ZLP wedge).
* ESP32 HWCDC: `arduino-esp32` `cores/esp32/HWCDC.cpp` (ISR drain +
  unchecked queue send, 256-byte runtime defaults), ESP32-S3 TRM ch. 33.3.3
  (hardware NAKs until the FIFO is freed).
* Teensy: `PaulStoffregen/cores` `teensy3/usb_dev.c` (NAK via BDT
  disarm when the pool runs dry), `usb_desc.h` (`NUM_USB_BUFFERS` 12),
  `usb_serial.c` (`TX_PACKET_LIMIT` 8, 70 ms give-up); PJRC's own serial
  docs state delivery is reliable regardless of rate.
* TinyUSB (Adafruit SAMD, RP2040): `cdc_device.c`
  `_prep_out_transaction()` refuses to re-arm the OUT endpoint unless a
  full endpoint-buffer of FIFO space is free — NAK, no drop;
  `CFG_TUD_CDC_RX_BUFSIZE` 256 in both cores' `tusb_config`.
* macOS host: xnu `tty.c`/`tty_subr.c` (`TTYCLSIZE` 1024 flat clist,
  `IO_NDELAY` short-write path) — the measured 1024-byte short write is
  that constant.

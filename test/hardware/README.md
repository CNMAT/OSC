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

## Inbound bursts: an ATmega32U4 platform limit, not a library one

`stress.py` found that inbound bursts are silently truncated and that on a
32U4 the receive direction then stays dead until reset. That looked like an
OSC or SLIP bug. It is not. Measured on a LilyPad USB (ATmega32U4):

**It is not memory.** An instrumented echo reporting `freeRam()` on every
packet shows free RAM flat at **2231 bytes** across the cliff — `minFree`
never dips, `getError()` stays 0, no `ALLOCFAILED`. The board simply stops
receiving while memory is untouched.

**It is not this library.** The same failure reproduces with a sketch that
contains no OSC and no SLIP at all — just `Serial.available()` / `Serial.read()`
in a tight loop, counting bytes:

| written | cumulative received |
|---|---|
| 100 | 100 |
| +300 | 400 |
| +378 | 778 |
| +500 | 1162 — only 384 of 500 arrived |
| +1000 | report truncated mid-line |
| +4000 | 1162 — nothing further, ever |

**The stall is permanent and one-directional.** 2 s and 5 s of idle do not
clear it; the host's own `write()` eventually returns EAGAIN because the device
has stopped draining the endpoint. Transmit keeps working throughout — the
board went on reporting its byte count while accepting none. Only a reset
recovers it.

**Why.** Arduino's 32U4 CDC has no software receive ring buffer. `Serial_::available()`
and `Serial_::read()` in `CDC.cpp` call `USB_Available(CDC_RX)` and
`USB_Recv(CDC_RX)` directly, so the only receive buffer is the hardware USB
endpoint FIFO. Once the host outruns the sketch, that endpoint can be left in a
state it never comes out of. Draining harder does not help: the byte-counting
sketch above does nothing else and still loses data.

### What this means for using the library

Keep inbound bursts small and paced by replies rather than by a timer. The
request/response pattern the Oscuino examples use is naturally safe: 25 frames
(350 bytes) written in one go were received intact, and 20 frames as separate
writes were too. Blasting 50 frames (700 bytes) was not. Treat roughly 350
bytes in flight as the practical ceiling on a 32U4, and wait for the reply
before sending more.

Boards with a real software RX buffer do not show this. Teensy 4.0 took 50
frames back to back with nothing lost. ESP32 truncates at its buffer size —
about 300 bytes, measured by varying frame size until only the byte count
stayed constant — but recovers on the next quiet moment rather than stalling.

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

Worse, on AVR the sketch does not recover: after one overflowing burst every
subsequent well-formed frame is lost until the board is reset. Not yet isolated
— `OscEcho.ino` caps its own buffer at 600 bytes, so this may be the sketch
rather than the library's SLIP state machine. Isolating it needs a sketch that
drives only the SLIP layer.

## Measured

| board | echo | widths | probe | stress |
|---|---|---|---|---|
| LilyPad USB (ATmega32U4) | 22/22 | 11/11, int=2 long=4 ll=8 double=4 | 7/7 | — |
| Teensy 4.0 | 22/22 | — | 7/7 | 0 frames lost up to 50 |
| Gemma M0 (SAMD21) | — | — | 7/7 | — |
| ESP32-C6 (RISC-V) | 22/22 | 11/11 | — | — |
| M5Stack StampS3 (Xtensa) | 22/22 | 11/11 | 7/7 | cliff at ~300 B |
| LilyPad USB | — | — | — | cliff at ~378 B, then wedged |
| M5Stack NanoC6 | not run — board stopped responding | | | |

The stress column records where inbound bursts start being dropped; see the
section above for why that is a property of the board's USB stack.

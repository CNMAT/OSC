# Web Serial examples — generator

Browser talks OSC to a board over USB, with no server, no npm install and no
network. The page is plain HTML with an inline OSC 1.0 codec and a SLIP layer;
the board runs the CNMAT OSC library. SLIP is already what
`SLIPEncodedSerial` puts on the wire, so nothing on the firmware side is
special-cased for the browser.

```
browser page  ⇄  OSC 1.0 packet  ⇄  SLIP frame  ⇄  USB serial  ⇄  this library
```

Each board gets a matched pair in `examples/`:

```
examples/GemmaOscuino/GemmaOscuino.ino      ← build and upload this
examples/GemmaOscuino/GemmaOscuino.html     ← open this
```

The `.html` sits inside the sketch folder deliberately. Arduino ignores it when
compiling, and it travels with the sketch when someone copies the folder out of
the library — which is exactly when a page that depended on a shared script two
directories up would break.

The generator also renders firmware for boards that run Python rather than
compiled sketches — a `boards.json` entry with a `firmware` field pairs a page
with `template-microbit.py` or `template-circuitpython.py` instead of
`template.ino`, and lands in `extras/python/<Id>Oscuino/` so the Arduino IDE
never lists a sketch-less folder. Same wire contract, same drift check, same
page. See `extras/python/README.md`.

## Boards

| Example | Board | FQBN | Build |
|---------|-------|------|-------|
| `GemmaOscuino` | Adafruit Gemma M0 | `adafruit:samd:adafruit_gemma_m0` | 22212 B |
| `PlaygroundOscuino` | Circuit Playground Express | `adafruit:samd:adafruit_circuitplayground_m0` | 23224 B |
| `LilyPadOscuino` | LilyPad Arduino USB | `arduino:avr:LilyPadUSB` | 19468 B |
| `TeensyOscuino` | Teensy 4.0 / 4.1 | `teensy:avr:teensy40` | builds |
| `RP2040Oscuino` | Raspberry Pi Pico | `rp2040:rp2040:rpipico` | 66236 B |
| `ESP32S3Oscuino` | ESP32-S3 | `esp32:esp32:esp32s3` | 329121 B |
| `MicrobitOscuino` | BBC micro:bit (MicroPython) | interpreted, `extras/python/` | n/a |
| `CircuitPythonOscuino` | CircuitPython (Adafruit boards) | interpreted, `extras/python/` | n/a |
| `FruitJamOscuino` | Adafruit Fruit Jam | both: CircuitPython in `extras/python/`, hand-written sketch in `examples/` | 97788 B |

The six Arduino rows verified with `arduino-cli compile` against their listed
FQBN. The python rows have no build: the interpreter is the build, and
`extras/python/test_host.py` is what proves their codecs before hardware does.

## Address space

The standard Oscuino set, so these sketches also answer the CNMAT Max/MSP
patches and any existing `Serial*` example's client. All traffic is an
`OSCBundle` in both directions.

| Address | Effect |
|---------|--------|
| `/d/<pin>` | `digitalRead` → `/d/<pin> <int>` |
| `/d/<pin>/u` | `digitalRead` with pullup → `/d/<pin>/u <int>` |
| `/d/<pin> <int>` | `digitalWrite` |
| `/d/<pin> <float>` | `analogWrite`, 0.0 … 1.0 |
| `/a/<pin>` | `analogRead` → `/a/<pin> <int>` |
| `/a/<pin> <int>` \| `<float>` | write on the matching digital pin |
| `/tone/<pin> <freq> [<ms>]` | square wave; no argument stops it |
| `/s/m` `/s/d` `/s/a` | micros, digital pin count, analog pin count |
| `/s/l <int>` | set `LED_BUILTIN` |

Boards with hardware beyond pins answer in the capability vocabulary of
[`ADDRESSES.md`](../../ADDRESSES.md) — `/rgb`, `/display/text`, `/buzz`, `/btn`,
`/imu`, and the rest — named by what the thing *is* rather than by which board
it is on. `test/test-namespace.mjs` fails on any address outside that document
and `make test` runs it, so a new board cannot quietly invent a dialect.

Each sketch announces `/enq <ExampleName>` once, from `setup()`, and answers
`/enq` again whenever asked — with a bundle naming itself and then one
`/enq/<capability>` line per thing it actually has, carrying that thing's shape
(`/enq/rgb 5`, `/enq/display 128 64`, `/enq/imu 6`). Absence is silence: a
capability the board lacks is simply missing from the bundle. On a native-USB
board `setup()` runs when the board powers up, not when the browser opens the
port, so the boot announcement is usually lost — which is why asking is the
reliable path.

### One page for every board

`oscuino.html` is generated from the same template with no board identity at
all, and builds itself from whatever the board announces: it sends `/enq`,
reads the `/enq` lines, and shows a panel per capability, hiding the rest. It
speaks three transports — Web Serial, Web Bluetooth (Nordic UART Service) and
HTTP (`POST /osc`, `GET /state`, `GET /enq`) — over one SLIP and OSC codec, so
the same page drives a board over USB, over the air, or across the network.

The per-board pages remain, generated from the same template with that board's
chips, USB filters and notes; they are the friendlier front door, and
`oscuino.html` is the one that works on a board it has never heard of.
`test/test-panels.mjs` cross-checks the capability table in `ADDRESSES.md`
against the page's panels, so a capability added to the contract and forgotten
in the page fails the suite.

Cap-touch and NeoPixel variants are deliberately absent — they need external
libraries, and an example that will not compile without a second install is a
poor front door. The existing `SerialOscuinoGemmaM0` and
`SerialOscuinoAdaFruitPlayGroundExpresswithBundles` examples still cover those.

## Using it

Web Serial needs a **secure context**. `https://` or `http://localhost` only —
double-clicking the file gives a `file://` origin where the page renders but
`navigator.serial` is missing and Connect is disabled.

```bash
cd examples/GemmaOscuino && python3 -m http.server 8000
```

then open `http://localhost:8000/GemmaOscuino.html`. Chrome/Edge 89+,
Firefox 151+, or Chrome for Android 149+.

Each page filters the browser's port chooser to its own board's USB
vendor/product ids, so you get one entry instead of every serial device on the
machine. Clones and bootloader PIDs will not match — tick **show all ports** to
drop the filter rather than concluding the board is dead.

## Regenerating

Every `<Id>Oscuino` pair — in `examples/` and in `extras/python/` — is
**generated**. Editing one directly will be overwritten and `make check` will
fail first.

```bash
cd extras/webserial
make generate     # write the pairs
make check        # fail if any generated file drifted from the template
make test         # codec bytes + per-board page wiring
make all          # check + test
```

| File | Role |
|------|------|
| `boards.json` | the board table — the only file you edit to add a board |
| `template.html` | the page, with `{{KEY}}` holes |
| `template.ino` | the sketch, same |
| `template-microbit.py` | MicroPython firmware for the micro:bit, same |
| `template-circuitpython.py` | CircuitPython firmware, same |
| `template-boot.py` | the boot.py that enables CircuitPython's data channel |
| `render.mjs` | substitution, shared by generate and check so neither can disagree |
| `generate.mjs` | writes the pairs |
| `check.mjs` | re-renders in memory, diffs against disk, reports the first differing line |
| `test/test-codec.mjs` | OSC bytes against hand-computed sequences, and SLIP at every chunk boundary |
| `test/test-pages.mjs` | per-board: script parses, identity matches, chips match, filters are numbers |

### Why N copies of the codec

Each page carries its own ~250-line codec. That is a real duplication, accepted
because self-containment is the property that makes these useful — they work
from a USB stick at a bench with no network, and they survive being copied out
of the library. `check.mjs` is what keeps the duplication honest: the copies
cannot drift silently, because a template change that was never regenerated
fails the check, and a hand-edited page fails it too.

### Adding a board

Add an entry to `boards.json` and run `make generate`. Nothing else needs
touching. Fields: `id` (becomes `<id>Oscuino`), `name`, `mcu`, `fqbn`,
`nativeUSB`, `usbFilters`, `chips`, `note`; optional `pinClamp` for variants
whose `NUM_*_PINS` macros overstate what is routed to pads, and `tone: false`
for a core with no `tone()`. A `firmware` field (`microbit` or
`circuitpython`) renders the entry against the matching python template into
`extras/python/` instead; `fqbn` stays empty and `usbProductId` may be
omitted from a filter to match a whole vendor.

Three fields exist for boards whose sketch is written by hand:

* `handwritten: true` — generate the page only, never the `.ino`. Boards with
  real peripherals (a display, a radio, an IMU) have sketches no template can
  produce, but they still want a generated page; without this the generator
  would overwrite the sketch and `make check` would demand it back.
* `sketch` — the sketch folder name, when it is not `<id>Oscuino`. The WiFi and
  BLE twins use it, since three entries share one board and one `id` cannot.
* `transport` — `serial` (default), `ble` or `http`: which transport the page
  selects on load. It is a starting position, not a restriction; the page
  offers whichever ones the browser supports.

`handwritten` and `firmware` are mutually exclusive — a python firmware is
always generated — and the generator says so rather than picking one.

## The one non-obvious firmware detail

`endofPacket()` must be called **before** `available()` on every pass of the
receive loop. Inside `SLIPEncodedSerial`, `available()` drives the SLIP state
machine, and calling it while that machine sits on a packet-terminating `END`
with more bytes buffered behind resets the state to `CHAR` — silently eating the
packet boundary. The stock examples get the ordering right but block in the
outer `while`; `pollOSC()` in these sketches returns instead, so a sketch with
other work to do in `loop()` keeps doing it.

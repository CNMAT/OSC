# The Oscuino address space

One namespace for every board, named by **capability, not by board**. A page
that knows this contract can drive any Oscuino sketch in this repository
without knowing which board it is talking to — it asks `/enq`, reads back
the capability list, and shows panels for what is actually there.

This holds for every sketch and firmware in the repository — the classic
`SerialOscuino*` pair and the tutorial examples included, not only the demos
with pages. It replaced a per-board convention (`/pybadge`, `/cpx`, `/pb`, `/xb`,
`/egg`, …) in which the same idea was spelled up to four ways (`/led`,
`/pixel`, `/pixels`, `/rgb`; `/screen/text`, `/display/text`, `/oled`, `/t`;
`/tone`, `/buzz`, `/beep`) and every board needed its own page.
`extras/webserial/test/test-namespace.mjs` fails CI on any address outside
this document, so the drift cannot come back quietly.

Conventions: `<i>` int32, `<f>` float32, `<s>` string, `<b>` blob;
`[x]` optional; `…` repeats. Every request that reads something answers on
the same address. Boards send a `/enq` bundle at boot and again whenever
asked, because the boot one is usually lost to USB enumeration.

## Core — every sketch

| address | direction | meaning |
|---|---|---|
| `/enq` | ask | reply with the bundle below |
| `/enq <s>` | reply | sketch name, then one `/enq/…` per capability present |
| `/enq/<name> [<i>…]` | reply | one line per capability the board has, with its shape (counts, sizes). `/enq` is the enquiry; these are its answers. |
| `/d/<pin> [<i>\|<f>]` | both | write: int = level, float 0..1 = PWM; no arg: read → `/d/<pin> <i>` |
| `/d/<pin>/u` | ask | read with pull-up → `/d/<pin>/u <i>` |
| `/a/<pin>/u` | ask | analog read with pull-up, where the core has one → `/a/<pin>/u <i>` |
| `/a/<pin> [<i>\|<f>]` | both | no arg: analog read → `/a/<pin> <i>`; with arg: write, as `/d` |
| `/tone/<pin> <i> [<i>]` | write | tone on a pin: hz, ms; 0 stops |
| `/c/<pin>` | ask | capacitance sense on a pin (AVR) → `/c/<pin> <i>` |
| `/s/m` | ask | `/s/m <i>` micros |
| `/s/d`, `/s/a` | ask | `/s/d <i>` digital pin count, `/s/a <i>` analog pin count |
| `/s/q` | write | Python firmwares only: exit to the REPL |
| `/rate <i>` | write | streaming period in ms; 0 stops. Echoed. |
| `/heartbeat <i>` | write | stream at least this often even if nothing changed; 0 disables |
| `/deadband <i>` | write | analog change needed before a stream sends |
| `/state <i> <i>` | stream | sequence, millis — the heartbeat of a stream |

A stream is a bundle sent every `/rate` ms containing `/state` and the
readings of whatever capabilities the board streams. Nothing is streamed
under a board's name. An actuator may be streamed too, to report the state
it is in.

**A capability's addresses usually live under its own root** — `rgb` owns
`/rgb…`, `display` owns `/display/…`. Three do not, and deliberately: `led`
and `volts` keep the `/s/l` and `/s/v` of the 2012 set so existing clients
keep working, and `net` announces an address (`/display/net`) that belongs to
the display it draws on. The `/enq` line is what a page needs; the address is
what the wire needs, and they are allowed to differ when history says so.

**Absence is silence.** A capability the board does not have is simply
missing from the `/enq` bundle, and a request to it answers *nothing*.
No sentinel values: `/imu -1` is not "no IMU", it is a malformed `/imu`,
because the contract says those arguments are floats.

**Recording a rename.** A sketch header that says which addresses it used to
speak — "renamed onto ADDRESSES.md on 2026-09-03 (`/screen/text` →
`/display/text`, …)" — is provenance, not a surviving dialect, exactly like
the What-moved table below. It is the one place an old name may still appear,
and `test-namespace.mjs` does not read comments.

## Capabilities

`/enq/<name>` in the enq bundle announces each of these. The parameters
after the name tell the page the shape.

| capability | `/enq` params | requests | replies / streams |
|---|---|---|---|
| **rgb** — addressable colour LEDs | `<i>` count | `/rgb <r> <g> <b>` all; `/rgb/<n> <r> <g> <b>` one; `/rgb/pixels <r g b>…` a whole frame; `/rgb/bright <i>` 0..255 | echoed |
| **display** — any display | `<i> <i>` width height (pixels or characters) | `/display/text <s>…` lines; `/display/big <s>`; `/display/clear`; `/display/fill <r> <g> <b>`; `/display/rect <x> <y> <w> <h> <r> <g> <b>`; `/display/circle <x> <y> <r> <r> <g> <b>`; `/display/bl <i>` backlight/brightness 0..255; `/display/invert <i>`; `/display/pixels <b>` raw frame; `/display/scroll <i>` ms per step; `/display/pause <i>` | `/display/text <i>` lines drawn; `/display/frame <b>` the frame shown |
| **buzz** — onboard speaker or buzzer | — | `/buzz <i> [<i>] [<i>]` hz, ms, volume 0..255; 0 hz stops | echoed |
| **btn** — buttons | `<i>` count | `/btn` | `/btn <i>…` one per button, 1 = pressed |
| **imu** — motion | `<i>` axes: 3 accel, 6 accel+gyro | `/imu` | `/imu <f>…` g, then deg/s |
| **mic** — microphone | — | `/mic`; `/mic/gain <i>` | `/mic <i> <i> [<i> <b>]` rms, peak, full scale 0..32767; boards with a scope add the sample rate and a waveform blob |
| **led** — the board's plain on/off LED | — | `/s/l <i>` | echoed. The address stays in the `/s` space because it is the 2012 Oscuino vocabulary that existing CNMAT Max patches speak; the capability only says whether the board HAS one. There is no other LED address — colour LEDs are **rgb** |
| **volts** — supply voltage | — | `/s/v` | `/s/v <f>` volts (was `/s/s`). Same reasoning as **led** for the address |
| **light** — ambient light | — | `/light` | `/light <i>` raw |
| **temp** — temperature | — | `/temp` | `/temp <f>` °C (was `/t` → `/s/t`). Degrees or nothing: a board whose sensor is uncalibrated must say so and not announce `/enq/temp` |
| **hum** — relative humidity | — | `/hum` | `/hum <f>` percent |
| **bat** — battery | `<i>` count (default 1) | `/bat` | `/bat <i>…` millivolts, one per pack |
| **chg** — charger | — | `/chg [<i>]` set mA | `/chg <i> <i>` charging?, mA (-1 = untouched default) |
| **touch** — touchscreen | `<i> <i>` width height | `/touch/map <i> <i> <i>` swapXY mirrorX mirrorY | `/touch <i> <i> [<i>]` x y [gesture], streamed while down |
| **cap** — capacitive touch pads | `<i>` count | `/cap`; on a board with acquisition set-up (below): `/cap/lib [<s>]` which library measures; `/cap/<p> [<i> \| <i>…]` one value for every pad, one per pad, or none to ask; `/cap/<n>/<p> <i>` pad n only; `/cap/send [<i>]` and `/cap/recv [<i>…]` a send-and-receive library's pins; `/cap/pins`; `/cap/config`; `/cap/defaults` | `/cap <i>…` one reading per pad; `/cap/lib <s>` echoed, and after a change the board sends its `/enq` bundle again; `/cap/<p> <i>…` every pad's setting, echoed after any set-up request; `/cap/send [<i>]`, `/cap/recv <i>…` echoed; `/cap/pins <i>…` each pad's Arduino pin; `/cap/config` and `/cap/defaults` answer `/cap/lib`, the measuring library's `/cap/<p>` lines, its `/cap/send` and `/cap/recv` where it has them, then `/cap/pins` |
| **joy** — joystick(s) | `<i>` axes | `/joy` | `/joy <i>…` axes |
| **pot** — potentiometer / slider | `<i>` count | `/pot` | `/pot <i>…` |
| **enc** — rotary encoder | — | `/enc`; `/enc/zero` | `/enc <i> <i>` position, delta |
| **rtc** — real-time clock | — | `/rtc [<y> <mo> <d> <h> <mi> <s>]` | `/rtc <i>×6` |
| **rfid** — tag reader | — | `/rfid/diag` | `/rfid <T\|F> <s>` present, UID hex |
| **cam** — camera | `<i> <i>` width height | `/cam/size <i>`; `/cam/quality <i>`; `/stream <i>` | `/cam <b>` a JPEG frame |
| **motor**, **servo**, **relay** — actuators | `<i>` count | `/motor/<n> <i> <i>` speed dir; `/servo/<n> <i>` angle; `/relay/<n> <i>` | echoed |
| **dist** — a rangefinder | — | `/dist` | `/dist <f>` centimetres; absent from a stream tick that got no echo |
| **gas** — a gas or smoke sensor | — | `/gas` | `/gas <i>` raw counts. Raw or nothing: an MQ-series cell is uncalibrated |
| **ir** — an infrared remote receiver | — | `/ir` the last code | `/ir <i>` the raw 32-bit code, sent once when a code arrives |
| **i2c** — an I2C bus a client can switch on and test | — | `/i2c [<i>]` ask, or 1 on and 0 off; `/i2c/scan`; `/i2c/pull` | `/i2c <i>` echoed, with `/i2c/pull`; `/i2c/scan <i>…` each 7-bit address from 0x08 to 0x77 that answered a one-byte read; `/i2c/pull <i> <i>` SDA then SCL, 1 where something outside the board held the line high against the board's own pull-down, read when the bus was switched on; a board sends `/i2c/pull` with nothing after the address before its bus was first switched on, and always if its own pull-ups are on both lines, where that reading could tell nothing. A board whose bus shares pins with something else says what stops while it is on (below) |
| **wii** — a Wii extension controller on that bus | `<s>` its kind | `/wii`; `/wii/raw`; `/wii/id` | `/wii <i|f>…` its fields, in its kind's order (below); `/wii/raw <i>…` the bytes the board read, sent with the `/wii` from that same read; `/wii/id <s> <i>×6` the kind and the six identity bytes it reported, nothing after the address when none is there. Announced only while one is plugged in; the board sends its `/enq` bundle again when one comes or goes |
| **net** — a network transport | `<s> <i> <i> [<s> <s>]` ip, rssi, port, then name and mac on a board that has a name (the WiFi twins send the first three). Sent in the enq bundle, so `/enq` to the network's broadcast address finds every board | `/display/net` show the network panel on the display; `/net/dest <s> [<i>]` where the stream goes, ip and port, `"0.0.0.0"` = whoever last spoke; `/net/name <s>`; `/net/save` write the settings; `/net/join <s> [<s>]` ssid, password: save and restart onto that network; `/net/setup` restart onto the board's own setup network; `/net/forget` factory settings | `/net/dest <s> <i>` and `/net/name <s>` echoed |
| **diag** — a board's own diagnostics | — | — | `/diag <s>…` free text, never parsed. Announced by `/enq/diag` like anything else |

### cap set-up

Where a board's pads have acquisition settings a client may change, they sit
under `/cap/<p>`, in the order below, as `/mic/gain` and `/touch/map` sit under
their capabilities. A board whose pads have no set-up, the Circuit
Playground's among them, answers none of these. A value outside its set
changes nothing, and the echo then shows what is still in force.

A board may measure its pads with more than one library, one at a time:
`/cap/lib <s>` chooses, by the library's own name, and `/cap/lib` alone asks.
`XiaoSamd21Oscuino` offers `FreeTouch` (Adafruit's driver for the SAMD21's
PTC), `FastTouch` (the pin's charge time through its own pull-up) and
`CapacitiveSensor` (a send pin driving receive pins through external
resistors). The pad count and `/cap/pins` change with the library, which is
why the board sends its `/enq` bundle again after a change. Each parameter
below belongs to one library, keeps its value while another measures, and
counts its pads in that library's own order.

| `<p>` | library | values | the setting |
|---|---|---|---|
| `oversample` | FreeTouch | 1 2 4 8 16 32 64 | samples accumulated per reading; `/cap` stays on one scale, the sum divided by this |
| `resistor` | FreeTouch | 0 20 50 100 | series resistor, kΩ |
| `freq` | FreeTouch | 0 none, 1 hop, 2 spread, 3 spread-median | frequency mode |
| `hop` | FreeTouch | 1..16 | sample delay, written as hop − 1 when `freq` is not 0 |
| `compcap` | FreeTouch | 0..16383 | compensation capacitor code |
| `intcap` | FreeTouch | 0..63 | integration capacitor code |
| `samples` | CapacitiveSensor | 1..255 | charge cycles summed per reading |
| `timeout` | CapacitiveSensor | 1..60000 | the library's timeout, nominally ms; a reading that times out is the library's own −2 |

FastTouch has no settings. CapacitiveSensor's pads are its receive pins:
`/cap/send <i>` chooses the send pin and `/cap/recv <i>…` the receive pins,
one pad each, and until both are chosen it has no pads. `/cap/defaults`
returns the measuring library to its defaults, which for CapacitiveSensor
means no pins.

`<n>` is the pad's index among its library's pads, so `/cap/3/intcap 10` sets
FreeTouch's fourth pad. Incoming OSC patterns apply: `/cap/[0-1]/hop 5` sets
two pads.

### wii fields

`/wii` sends, for the kind `/wii/id` and `/enq/wii` name, one value per field
in the order below: an int, or a float where the table says so. On
`XiaoSamd21Oscuino` and `KB2040Oscuino`, NintendoExtensionCtrl 0.8.3 reads the
extension, and each field is one of its getters, named as the library names it:
every public, const, no-argument getter the kind's class declares that
returns a number or a boolean, in its header's order, and for the turntable
then those of its `left` and `right` platters, from the class both are made
from. Not `getExpectedType()`, which every class declares and which returns
the library's `ExtensionType`, nor the getters every class inherits from
`ExtensionController`. A range is the library's comment on that getter, or
its type's; a boolean reads 1 while pressed, or while true. A type the
library does not know is `Unknown`, and its `/wii` is the six bytes the
library read for it.

On `XiaoSamd21Oscuino` the bus is D4 (SDA) and D5 (SCL), which are also touch
pins: while `/i2c` is 1 the board measures no pads and announces no
`/enq/cap`, and `/i2c 0` gives the pins back to the library `/cap/lib` names.

On `KB2040Oscuino` the bus is the STEMMA QT port, GPIO12 (SDA) and GPIO13
(SCL), the RP2040's I2C0, which nothing else on the board uses: `/i2c 0` only
stops the bus, and the stream then carries `/state` alone.

| kind | library class | `/wii` fields, in order, with ranges |
|---|---|---|
| `Nunchuk` | `Nunchuk` | `joyX` 0..255, `joyY` 0..255, `accelX` 0..1023, `accelY` 0..1023, `accelZ` 0..1023, `buttonC` 0..1, `buttonZ` 0..1, `rollAngle` -180..180 float, `pitchAngle` -180..180 float |
| `Classic` | `ClassicController` | `getHighRes` 0..1, `leftJoyX` 0..255, `leftJoyY` 0..255, `rightJoyX` 0..255, `rightJoyY` 0..255, `dpadUp` 0..1, `dpadDown` 0..1, `dpadLeft` 0..1, `dpadRight` 0..1, `buttonA` 0..1, `buttonB` 0..1, `buttonX` 0..1, `buttonY` 0..1, `triggerL` 0..255, `triggerR` 0..255, `buttonL` 0..1, `buttonR` 0..1, `buttonZL` 0..1, `buttonZR` 0..1, `buttonStart` 0..1, `buttonSelect` 0..1, `buttonPlus` 0..1, `buttonMinus` 0..1, `buttonHome` 0..1 |
| `Guitar` | `GuitarController` | `joyX` 0..63, `joyY` 0..63, `strum` 0..1, `strumUp` 0..1, `strumDown` 0..1, `fretGreen` 0..1, `fretRed` 0..1, `fretYellow` 0..1, `fretBlue` 0..1, `fretOrange` 0..1, `whammyBar` 0..31, `touchbar` 0..31, `touchGreen` 0..1, `touchRed` 0..1, `touchYellow` 0..1, `touchBlue` 0..1, `touchOrange` 0..1, `buttonPlus` 0..1, `buttonMinus` 0..1 |
| `Drums` | `DrumController` | `joyX` 0..63, `joyY` 0..63, `drumRed` 0..1, `drumBlue` 0..1, `drumGreen` 0..1, `cymbalYellow` 0..1, `cymbalOrange` 0..1, `bassPedal` 0..1, `buttonPlus` 0..1, `buttonMinus` 0..1, `velocityAvailable` 0..1, `velocityID` 0..31, `velocity` 0..7, `velocityRed` 0..7, `velocityBlue` 0..7, `velocityGreen` 0..7, `velocityYellow` 0..7, `velocityOrange` 0..7, `velocityPedal` 0..7 |
| `Turntable` | `DJTurntableController` | `turntable` -64..62, `buttonGreen` 0..1, `buttonRed` 0..1, `buttonBlue` 0..1, `effectDial` 0..31, `crossfadeSlider` 0..15, `buttonEuphoria` 0..1, `joyX` 0..63, `joyY` 0..63, `buttonPlus` 0..1, `buttonMinus` 0..1, `left.connected` 0..1, `left.turntable` -32..31, `left.buttonGreen` 0..1, `left.buttonRed` 0..1, `left.buttonBlue` 0..1, `right.connected` 0..1, `right.turntable` -32..31, `right.buttonGreen` 0..1, `right.buttonRed` 0..1, `right.buttonBlue` 0..1 |
| `uDraw` | `uDrawTablet` | `penX` 0..4095, `penY` 0..4095, `penPressure` 0..511, `buttonLower` 0..1, `buttonUpper` 0..1, `penDetected` 0..1 |
| `Drawsome` | `DrawsomeTablet` | `penX` 0..65535, `penY` 0..65535, `penPressure` 0..4095, `penDetected` 0..1 |

The turntable's ranges are from the library's code, not its comment: each
platter's `turntable()` is five bits and a sign, and the combined one is their
sum, where the comment on it says "6 bits, -30-29". A platter's `connected`
reads 1 only while it turns or one of its buttons is held: the library
remembers a platter only in `getTurntableConfig()`, which `/wii` does not call
(its DJTurntable.cpp; no turntable was tried). How the board uses the
library, and what was measured, is in
`examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino` under "WII EXTENSIONS".
`examples/KB2040Oscuino/KB2040Oscuino.ino` uses it the same way, and its
"WII EXTENSIONS" says what it carries from that board without measuring it
on its own.

## What moved

| was | now | note |
|---|---|---|
| `/led`, `/pb/led`, `/mg/led` | `/s/l` | the standard address already did this |
| `/egg/b`, `/fj/b` | `/btn` | |
| `/pixel`, `/pix`, `/pb/rgb`, `/pm/rgb`, `/fj/led`, `/xb/rgb` | `/rgb`, `/rgb/<n>` | |
| `/pixels` | `/rgb/pixels` | |
| `/bright` | `/rgb/bright` | |
| `/screen/*`, `/pb/oled`, `/pm/oled`, `/joy/screen`, `/egg/t`, `/fj/t`, `/rd/t`, `/mb/t`, `/matrix/text` | `/display/text` | |
| `/screen/box` | `/display/rect` | |
| `/screen/backlight`, `/matrix/bright` | `/display/bl` | |
| `/rd/fill` | `/display/fill` | |
| `/matrix/pixels`, `/frame` | `/display/pixels`, `/display/frame` | |
| `/matrix/rate`, `/matrix/pause` | `/display/scroll`, `/display/pause` | a scroll speed is not a stream rate |
| bare `/tone` (no pin), `/beep`, `/fj/beep`, `/pb/buzz`, `/pm/buzz` | `/buzz` | `/tone/<pin>` is unchanged and means a pin |
| `/xb/imu`, `/mb/a` | `/imu` | floats: g, deg/s — micro:bit converts from milli-g |
| `/xb/mic`, `/xb/gain` | `/mic`, `/mic/gain` | |
| `/xb/bat`, `/xb/chg` | `/bat`, `/chg` | |
| `/rd/touch` | `/touch` | |
| `/rd/rtc` | `/rtc` | |
| `/pb/motor`, `/pb/servo`, `/pb/relay` | `/motor/<n>`, `/servo/<n>`, `/relay/<n>` | |
| `/*/rate`, `/mic/rate`, `/cam/rate` | `/rate` | one rate per board |
| `/pybadge`, `/hallowing`, `/esplora`, `/cpx`, `/xiao`, `/dial`, `/joy` (blob), `/pb`, `/pm`, `/xc6`, `/mg`, `/egg` | `/state` + per-capability messages in one bundle | a page renders the capabilities, not the board |
| `/enq <name> <bool>…` | `/enq <name>` + `/enq/…` | booleans became presence; shapes became params |
| `/t` → `/s/t`, `/s/s` (SerialOscuino) | `/temp`, `/s/v` | the 2012 sketches join the same vocabulary |
| `/egg/net` | `/display/net` | |

## Why capability, not board

The pin-oriented core (`/d`, `/a`, `/s`) always worked this way, and it is
why one generated page has served thirty boards. The board-named layer grew
by accretion, one demo at a time, each inventing a prefix. The cost showed up
the day the same Web Bluetooth page was asked to drive three chip families:
it could not, because each spoke a private dialect for the same LED.

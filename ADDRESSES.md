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
| **cap** — capacitive touch pads | `<i>` count | `/cap` | `/cap <i>…` one reading per pad |
| **joy** — joystick(s) | `<i>` axes | `/joy` | `/joy <i>…` axes |
| **pot** — potentiometer / slider | `<i>` count | `/pot` | `/pot <i>…` |
| **enc** — rotary encoder | — | `/enc`; `/enc/zero` | `/enc <i> <i>` position, delta |
| **rtc** — real-time clock | — | `/rtc [<y> <mo> <d> <h> <mi> <s>]` | `/rtc <i>×6` |
| **rfid** — tag reader | — | `/rfid/diag` | `/rfid <T\|F> <s>` present, UID hex |
| **cam** — camera | `<i> <i>` width height | `/cam/size <i>`; `/cam/quality <i>`; `/stream <i>` | `/cam <b>` a JPEG frame |
| **motor**, **servo**, **relay** — actuators | `<i>` count | `/motor/<n> <i> <i>` speed dir; `/servo/<n> <i>` angle; `/relay/<n> <i>` | echoed |
| **net** — a network transport | `<s> <i> <i> [<s> <s>]` ip, rssi, port, then name and mac on a board that has a name (the WiFi twins send the first three). Sent in the enq bundle, so `/enq` to the network's broadcast address finds every board | `/display/net` show the network panel on the display; `/net/dest <s> [<i>]` where the stream goes, ip and port, `"0.0.0.0"` = whoever last spoke; `/net/name <s>`; `/net/save` write the settings; `/net/join <s> [<s>]` ssid, password: save and restart onto that network; `/net/setup` restart onto the board's own setup network; `/net/forget` factory settings | `/net/dest <s> <i>` and `/net/name <s>` echoed |
| **diag** — a board's own diagnostics | — | — | `/diag <s>…` free text, never parsed. Announced by `/enq/diag` like anything else |

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

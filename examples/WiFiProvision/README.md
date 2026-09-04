# WiFiProvision — scope report

Bring-up for OSC over WiFi, on the x-OSC model: the board is its own network
until it has been told which network to join and where to send OSC. This
report says what the model is, what was built to test it, what was measured,
and what is still a decision. Written 2026-09-04; everything measured was
measured that day on the M5Stack Capsule that was on the bench.

## 1. The model: what an x-OSC does

From the x-OSC User Manual v0.4 (x-io Technologies, May 2014), section by
section, and what this example does with each point.

| x-OSC | WiFiProvision |
|---|---|
| Ships in **ad hoc mode**: an open network called `x-OSC`, settings at `http://169.254.1.1` | Ships on an open **setup network** named `OSCMCU-XXXX` after its MAC, settings at `http://192.168.4.1/` (the stack's own AP address). A captive portal opens the page on a phone by itself; x-OSC predates that being common |
| **Infrastructure mode**: SSID, Open / WPA / WPA2, DHCP or static IP | **Join** mode: SSID and password, DHCP. Static IP left out (every stack has `WiFi.config()`; it is a form field away) |
| OSC settings: host IP (default `255.255.255.255`, broadcast), outgoing port 8000, incoming port 9000, "send bundles", address prefix | Destination `ip:port` (default `0.0.0.0:9000`, "whoever last spoke to me"), listen port 8000, stream every 500 ms by default. No prefix: this library's `/enq` already names the board |
| **Save Settings** reboots the device with the new settings; join takes about 30 s | Save reboots. Every mode change is a reboot, on every stack — see §3. Join measured at 4 s |
| **Ping button**: press → broadcast `/ping ip mac version`; hold 3 s → toggle mode; hold 8 s → factory reset | Optional `WIFI_PROVISION_BUTTON`: 3 s toggles mode, 8 s forgets. Discovery needs no button: broadcast `/enq` and every board answers |
| Host can broadcast `/ping` to make every x-OSC announce itself | `/enq` to `255.255.255.255:8000`, and every board answers with `/enq/net` — measured 10/10 answered on a good link, 1/5 on a −87 dBm one (§4) |
| Status LED: cyan ad hoc / yellow infrastructure, flashing until up; `/led/rgb` overrides | `BOARD_HAS_LED`: slow blink setup, fast blink joining, steady joined; `/s/l` takes it over |
| No USB | A USB serial console: `help`, `ssid`, `pass`, `dest`, `join`, `setup`, `forget`. The bench's way in, and the only one on a board with no button |
| Settings only via the web page; `/osc/remote/ip`, `/osc/remote/port`, `/osc/local/port` over OSC | The page, the console, and `/net/…` over OSC all edit one settings struct |
| Inputs / outputs / serial / IMU settings pages | Out of scope here: that is what the Oscuino sketches and `ADDRESSES.md` are |

Section 8 of the manual notes x-OSC address patterns are case-insensitive and
floats and ints are interchangeable; this example keeps the library's strict
typing.

## 2. What was built

Three files, no library changes:

* `WiFiProvision.ino` — the example: settings, the state machine, a settings page,
  a captive-portal DNS, the widened `/enq/net` and the `/net/…` verbs, the
  serial console.
* `wifi_stack.h` — the only file that knows which core it is on: the WiFi
  include ladder from the stock `WiFi*` examples, plus the four things they
  never needed — start an access point, its address, whether anyone is on
  it, and a settings store — and a restart, a "radio, do not sleep", a MAC.
* `test/hardware/wifiprovision.py` — the bench harness: 37 checks over the USB
  console, UDP and HTTP, without this Mac ever leaving its own network.

The portable choice that makes it one code path: the HTTP server, the form
parser and the DNS responder are written against `WiFiServer` / `WiFiClient`
/ `WiFiUDP`, which every Arduino WiFi stack presents, instead of the
`WebServer` and `DNSServer` libraries only the ESP and Pico cores ship. The
whole HTTP side — request parser, form decoding, the page and its
handlers — is 300 lines including the HTML; the DNS responder is 30.

**Persistent settings.** Yes, credentials and the destination are written to
flash and survive power cycles and — measured on the Capsule — a reflash of
the sketch. There is no one cross-platform Arduino API for this, so
`wifi_stack.h` puts four behind one `storeLoad` / `storeSave` pair:

| store | boards | notes |
|---|---|---|
| `Preferences` (NVS) | every ESP32 core | wear-levelled, its own partition; `arduino-cli upload` does not touch it — the Capsule came back joined after a reflash |
| `EEPROM` with `begin()`/`commit()` | ESP8266, Pico W / Pico 2 W | a flash sector emulating EEPROM; erased by a full-chip erase, not by an upload |
| `EEPROM` plain (data flash) | UNO R4 WiFi, Portenta C33 | the RA4M1/RA6M5 data flash, separate from code flash; `update()` writes only what changed |
| `WiFiStorage` | WiFiNINA boards (Nano 33 IoT, MKR 1010, Nano RP2040 Connect) | a file on the NINA module's own flash: survives reflashing the host MCU |
| `FlashStorage` | WiFi101 boards (MKR1000, Feather M0 WiFi) | a page of the sketch's flash: **erased by every upload**, so re-provision after reflashing |

`EEPROM.h` alone is nearly universal (it exists on AVR, ESP8266, ESP32, RP2040,
Renesas, STM32, Teensy) but with two dialects and no SAMD version at all, which
is why the shim exists. Only the ESP32 backend has run on hardware.

## 3. How it behaves

```
boot ─ settings valid and mode = join and no "setup once" flag?
        yes → try the network for 30 s ──joined──▶ JOINED: OSC on UDP, page at http://<ip>/
                                        └─failed─▶ set "setup once", save, reboot
        no  → SETUP NETWORK: open AP, DNS catch-all, page at http://192.168.4.1/, OSC on UDP too
                 ├─ someone saves the form / types `join` / sends /net/join → save, reboot
                 └─ here because a join failed, and nobody has been on the AP for 3 min → clear flag, reboot
JOINED: link gone for 30 s → reboot
```

Every transition is a reboot. On an ESP32 one could switch modes in place; on
WiFiNINA / WiFiS3 / WiFi101 the radio firmware's half-torn-down states differ
from one another, and a reboot has none of them on any stack. The cost is two
settings writes per failed-join cycle (a router that is off for an hour costs
about 50 writes); the NVS and data-flash stores are rated far beyond that, the
ESP8266 sector-erase emulation less so.

The setup network is **open**, as x-OSC's is: it exists for the minute it takes
to type credentials in, and a password on it would have to be printed
somewhere first, which is the problem being solved. Anyone in range during
that minute could rewrite the settings; that is the x-OSC trade-off too.

## 4. Measured

Board: M5Stack Capsule (ESP32-S3, MAC `c0:4e:30:11:4d:f8`), esp32 core 3.3.11,
FQBN `esp32:esp32:m5stack_capsule`, built with `-DWIFI_PROVISION_POWER_HOLD=46
-DWIFI_PROVISION_BUTTON=42`. Network: a multi-node "mesh" with 2.4 GHz nodes on
channels 1, 6 and 11. Host: this Mac, on the same network by WiFi.

**The harness, seven runs.** Run 1 (before the fixes below): 34/37. Run 2,
bench build with the setup network kept up beside the joined one so its DNS
and redirect could be reached over the LAN (`-DWIFI_PROVISION_KEEP_AP`): **37/37**.
Run 3, plain build: **31/31** (the six captive-portal checks need the bench
flag). Run 4, plain build with `--long`: **33/33** — alone on the fallback
network with the right SSID saved meanwhile, the board rebooted to retry
after 173 s and joined. Run 5, the final source reflashed: **31/31**, and the
first `show` after the reflash said "joined": the settings had survived it.
Runs 6 and 7 are the decided vocabulary (`WiFiProvision`, `OSCMCU-XXXX`, the
wider `/enq/net`, 500 ms default): run 6 found two harness bugs and no sketch
ones, run 7 was **31/31** and is the last column. Run 8 was not the harness:
Adrian joined `OSCMCU-4DF8` from his phone, the captive-portal sheet opened
the settings page by itself, and he saved from it. The board rebooted, joined
at −59 dBm, streamed to the destination in the form at the 100 ms he set
(31 packets in 3 s, median period 100.1 ms, seen at this Mac), answered `/enq`
in 9 ms, took its setup network off the air, and resolved as
`OSCMCU-4DF8.local`. The numbers from the harness runs:

| what | run 2 | run 3 | run 7 |
|---|---|---|---|
| join, from the serial `join` to the joined banner | 4 s | 4 s | 4 s |
| UDP ask round trip, 20 asks (`/net` then, `/enq` now) | median 9.0 ms, worst 108.9 ms | median 11.4 ms, worst 110.7 ms | median 10.1 ms, worst 107.6 ms |
| `/state` stream at 100 ms, 3 s | 31 packets, 0 gaps, median period 100.2 ms | 31, 0 gaps, 100.0 ms | 31, 0 gaps, 100.6 ms |
| stream at 50 ms after a form save, 2 s | 41 packets, 0 gaps, 50.0 ms | 41, 0 gaps, 50.0 ms | 41, 0 gaps, 49.9 ms |
| ask to the directed broadcast, 10 single asks | 10/10, 6–13 ms | 10/10, 6–18 ms | 10/10, 6–16 ms |
| ask to `255.255.255.255`, 10 single asks | 10/10, 6–16 ms | 9/10, 7–23 ms | 10/10, 6–9 ms |
| settings page | 1747 B, one request | same | 1748 B |
| wrong SSID → back on the setup network | within the 30 s window, both runs | |
| password in the page | never (the field is blank; a blank field keeps the stored one) | |

**Sizes**, `arduino-cli` 1.5.1, the cores listed in BOARDS.md plus
`esp8266:esp8266` 3.1.2 installed for this:

| board | FQBN | flash | RAM |
|---|---|---|---|
| M5Stack Capsule | `esp32:esp32:m5stack_capsule` | 936,080 B (71 % of 1,310,720) | 50,432 B |
| XIAO ESP32-C6 | `esp32:esp32:XIAO_ESP32C6` | 1,047,682 B (79 %) | 47,360 B |
| … without mDNS (`-DWIFI_PROVISION_NO_MDNS`) | | 1,007,952 B (76 %) | 44,912 B |
| … without the HTTP OSC bridge (`-DWIFI_PROVISION_HTTP_OSC=0`) | | 1,046,340 B (79 %) | 46,880 B |
| *baseline:* `WiFiEcho`, UDP only, same chip | | 974,246 B (74 %) | 43,384 B |
| *comparison:* `XiaoC6ExpWiFi` twin (WebServer, OLED) | | 1,072,946 B (81 %) | 46,592 B |
| XIAO ESP32-C3 | `esp32:esp32:XIAO_ESP32C3` | 1,039,915 B (79 %) | 41,124 B |
| Nano ESP32 | `arduino:esp32:nano_nora` (core 2.0.18) | 785,585 B (24 % of 3 MB) | 60,352 B |
| NodeMCU (ESP8266) | `esp8266:esp8266:nodemcuv2` | 294,984 B (28 %), **IRAM 60,723 of 65,536 (92 %)** | 32,256 B (40 %) |
| Pico W | `rp2040:rp2040:rpipicow` | 384,864 B (18 %) | 74,892 B (28 %) |
| Pico 2 W | `rp2040:rp2040:rpipico2w` | 378,812 B (9 %) | 76,328 B (14 %) |
| UNO R4 WiFi | `arduino:renesas_uno:unor4wifi` | 89,204 B (34 %) | 12,680 B (38 %) |
| Portenta C33 | `arduino:renesas_portenta:portenta_c33` | 201,168 B (9 %) | 47,972 B (9 %) |
| Nano 33 IoT | `arduino:samd:nano_33_iot` | 58,596 B (22 %) | 5,976 B (18 %) |
| MKR WiFi 1010 | `arduino:samd:mkrwifi1010` | 59,428 B (22 %) | 5,404 B (16 %) |
| MKR1000 | `arduino:samd:mkr1000` | 68,364 B (26 %) | 9,732 B (29 %) |
| Feather M0 WiFi | `adafruit:samd:adafruit_feather_m0` | 72,424 B (27 %) | not reported by that core |

So on the C6 the whole provisioning layer — access point, DNS, HTTP, page,
settings store, the `net` addresses — is 73 KB over a bare UDP echo, and 40 KB
of that is mDNS. It is 25 KB *smaller* than the existing WiFi twin, which uses
the core's WebServer. The ESP8266 IRAM figure is the one to watch: 92 % full
before any sketch of substance, so that board wants `-DWIFI_PROVISION_NO_MDNS`
and has not run.

**Three things the bench found that reading would not have:**

1. *The MAC is zero before the driver is up.* `WiFi.macAddress()` on esp32
   3.3.11 returned `00:00:2c:00:00:00` until the station driver started, and
   the board named itself `OSCMCU-0000`. `esp_read_mac()` reads the efuse at
   any time; `wifi_stack.h` uses it on ESP32. A board that names its own
   network after its MAC must read the MAC before starting the network.
2. *Fast scan picks a weak node.* The ESP32 driver's default is to join the
   first access point that answers to the SSID, not the strongest. On this
   three-node network that put the Capsule on −78, −80 and −87 dBm across
   three joins, with broadcast asks answered 3/5 (directed) and 1/5
   (limited): broadcast frames go out at the basic rate with no
   acknowledgement or retry, so a weak link loses them long before it loses
   unicast. With `WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL` the next
   twelve joins across runs 2–5 read between −55 and −78 dBm, median −62.5
   (−66, −73, −64, −78, −61, −56, −63, −62, −69, −57, −55, −55), and broadcast
   answered 10/10, 10/10, 10/10 and 9/10. Same board position, same network,
   not a controlled A/B (the bench flag also changed between runs), but the
   mechanism is documented and the figures are what was seen. The scan costs
   a couple of seconds at join; the join still measured 4 s end to end.
3. *A second `WiFi.begin()` while joining is an error on ESP32.* Re-asking
   every 10 s (which the blocking module stacks need) logs
   `wifi:sta is connecting, cannot set config` on the ESP-IDF driver and
   stretched the give-up from 30 to 40 s. The retry is now only compiled for
   the non-ESP stacks.

Also measured, since HTTP is served inside `loop()` and the stream waits
while a request is: at a 20 ms stream, 4 s with no HTTP traffic gave 200
packets, inter-arrival median 20.1 ms, p99 42.7 ms, max 57.3 ms; with `GET /`
fetched every 0.4 s (12 fetches, median 88 ms each) the same stream gave
0 lost packets, median 20.0 ms, p99 94.3 ms, max 137.5 ms. So a settings-page
hit delays the stream by up to about 120 ms and drops nothing — the tick is
late, not skipped. The twins' `WebServer::handleClient()` has the same shape.

## 5. Portability, honestly

| stack | boards | AP | AP address | clients? | store | compiled | ran |
|---|---|---|---|---|---|---|---|
| esp32 (Espressif core 3.3.11) | Capsule, XIAO C3/C6/S3, devkits… | `softAP` | `softAPIP` | `softAPgetStationNum` | Preferences | yes | **Capsule, 37/37 + 31/31** |
| esp32 (Arduino's 2.0.18 fork) | Nano ESP32 | same | same | same | Preferences | yes | no |
| ESP8266 3.1.2 | NodeMCU, D1 mini… | `softAP` | `softAPIP` | same | EEPROM + commit | yes | no (no board here) |
| arduino-pico 6.0.0 | Pico W, Pico 2 W | `softAP` | `softAPIP` | same | EEPROM + commit | yes | no |
| WiFiS3 (renesas_uno 1.6.0) | UNO R4 WiFi | `beginAP` | `softAPIP` | `status()==WL_AP_CONNECTED` | EEPROM | yes | no — board is on the shelf |
| WiFiC3 (renesas_portenta 1.6.0) | Portenta C33 | `beginAP` | `localIP` | same | EEPROM | yes, after using `available()` (its `accept()` is protected) | no |
| WiFiNINA 2.1.1 | Nano 33 IoT, MKR 1010, Nano RP2040 Connect | `beginAP` | `localIP` | same | WiFiStorage | yes | no |
| WiFi101 0.16.1 | MKR1000, Feather M0 WiFi | `beginAP` | `localIP` | same | FlashStorage | yes, after not putting its `uint32_t localIP()` in a ternary | no |
| Zephyr (`arduino:zephyr_main`) | Giga R1, Portenta H7, Nano RP2040 Connect | `beginAP` | ? | ? | `Storage` lib | **not attempted** | no |
| mbed (`arduino:mbed_*`) | Giga, Portenta H7, Nicla | `beginAP` | ? | ? | KVStore | **not attempted** — cores not installed | no |

Open questions the compile cannot answer, one per stack family, each a
15-minute bench item with the board in hand:

* WiFiS3 / WiFiNINA / WiFi101: does `WiFiUDP` receive on the access point
  (the OSC listener and the DNS responder both need it), and does DHCP hand a
  phone an address? Their `AP_SimpleWebServer` examples show TCP only.
* WiFiNINA: `WiFiStorage` write-then-read of a 130-byte file, and whether
  `WiFi.beginAP()` after a failed `WiFi.begin()` needs the driver re-initialised.
* ESP8266: the IRAM budget with a real sketch on top.
* ~~All: a phone actually joining the setup network and getting the portal
  sheet.~~ Done, run 8: the sheet opened on Adrian's phone and the save from
  it took. The harness had proved the DNS answer and the 302 over the LAN;
  the detection itself needed a phone, and got one.

## 6. Decisions — the naming table, as decided

Reviewed 2026-09-04. The first four rows changed from the proposal; the
rest were kept as proposed. Everything below is now what the code, the
harness and `ADDRESSES.md` say.

| name | what | decision |
|---|---|---|
| `WiFiProvision` | the example, the harness (`wifiprovision.py`), the `WIFI_PROVISION_*` flags | **renamed** from `WiFiSetup`: the industry's word |
| `OSCMCU-XXXX` | default name, setup-network SSID, mDNS host | **renamed** from `oscuino-XXXX`; four MAC hex digits because a room may hold several boards |
| `/enq/net <ip> <rssi> <port> [<name> <mac>]` | the `net` capability's `/enq` line | **widened** instead of adding a `/net` ask: `/enq` was already "who are you", so one broadcast `/enq` is the whole discovery protocol (x-OSC `/ping`). The twins keep sending the first three; `ADDRESSES.md` now says so, and its `/cap/net` spelling is gone |
| rate default 500 ms | stream on from the first boot | **changed** from 0: a board that says nothing until asked is a board you cannot find |
| `/net/dest`, `/net/name`, `/net/save`, `/net/join`, `/net/setup`, `/net/forget` | the verbs, under one root | kept. `/net/dest` and `/net/name` are echoed; `/net/dest` is runtime until `/net/save`, which is the one explicit write |
| `8000` / `9000` | listen / destination ports | kept: 8000 is what the twins listen on |
| `0.0.0.0` | "whoever last spoke" | kept: how the twins behave |
| 30 s / 3 min | join window / fallback retry | kept |
| open setup network | | kept for now; §8 is the assessment of what it exposes and what would close it |
| serial `ssid`, `pass`, `dest`, `port`, `rate`, `name`, `join`, `setup`, `save`, `forget`, `show`, `help` | the console | kept |

## 7. Where this could go

* **Into the twins.** `XiaoC3WiFi`, `EggC3WiFi` and `XiaoC6ExpWiFi` would lose
  `arduino_secrets.h` and gain the page; the shims in `wifi_stack.h` are what
  they would share, which argues for moving that header into the library
  (there is precedent: `SLIPEncodedSerial.h`, `OSCBufferedPrint.h`). The
  `Capture` Print that three sketches now define to encode a bundle into a
  buffer wants the same treatment.
* **Into the page.** `extras/webserial/oscuino.html`'s HTTP transport already
  speaks `GET /enq`, `GET /state`, `POST /osc`, and this example serves them;
  a laptop on the board's setup network can drive it with zero
  infrastructure. A `net` panel could show name, mac and the destination and
  offer the `/net/…` verbs.
* **Into CI.** `WiFiProvision` compiles for sixteen FQBNs across seven stacks; the
  matrix in `.github/workflows/ci.yml` covers none of the WiFi examples today
  (TODO.md §5).
* **Onto the shelf boards.** The UNO R4 WiFi is the one that would settle the
  largest open question (a module stack's AP-mode UDP) and it is already in
  BOARDS.md.

## 8. Lowering the chance of the WiFi password being stolen

What the current design exposes, and what it does not:

* The password is **never readable back**: not on the page (the field is
  blank and a blank keeps the stored one), not on the console (`show` says
  only whether one is set), not over OSC. The harness asserts the first.
* It crosses the air **once in cleartext**: the `POST /save` from a phone on
  the open setup network, or a `/net/join` over UDP. On the open network
  anyone in radio range with a sniffer sees it. Once joined, the same POST
  travels inside the WPA2 network the password protects, which is fine.
* Anyone in range can **join the open network and rewrite the settings**
  during the setup window — point the stream elsewhere, forget the board.
  They cannot extract the password that way.
* Anyone with **the board in hand and a USB cable** reads the flash: every
  store in §2 holds the password in plaintext. ESP32 flash encryption
  exists, is not portable, and gets in the way of development.

The levers, cheapest first:

1. **Give the setup network a WPA2 key of its own.** Every stack takes one:
   `softAP(ssid, key)` on the ESP and Pico cores, `beginAP(ssid, key)` on
   WiFiS3, WiFiNINA, WiFi101 and WiFiC3. A per-board random key generated on
   first boot and stored with the settings, printed on the USB console and
   drawn on the display where the board has one, means the POST is
   encrypted on the air and a stranger cannot join at all. The captive
   portal still opens the page once the phone is on. The cost is typing that
   key once per board, and the key's quality: the ESP32 and RP2040 have
   hardware random sources; the SAMD21 and the module stacks would have to
   mix timing and analog noise, which is weaker and would be said so. This
   closes the sniffing and the rewriting threats together and is the change
   I would make, with a compile-time switch to get the open, x-OSC-style
   network back for boards where the console is out of reach.
2. **Drop the password from OSC.** `/net/join <ssid> <pass>` is the only
   path that puts it in a UDP datagram; the page and the console can carry
   the join instead. One line to remove, nothing else changes.
3. **Provision onto a network you are willing to have on a dozen
   microcontrollers.** A guest or show SSID, not the one your laptop uses.
   This is the largest lever of all, because it is the only one that also
   covers the plaintext copy in flash, and it costs nothing in code.
4. **Bound the exposure in time**: take the setup network down after some
   minutes with no client and bring it back on a button or a `setup` on the
   console. Cheap, but it makes a never-configured board vanish, so it is a
   poor fit on its own.
5. **HTTPS on the board** is not the answer: only the ESP8266 core ships a
   TLS server, a self-signed certificate breaks the captive-portal flow with
   a browser warning, and it protects nothing the WPA2 key does not.

Recommended: 1 + 2 now, 3 as the written rule for the WiFi examples. None
of this is implemented yet; the question was asked, so this is the answer.

## Running it

```sh
arduino-cli compile -b esp32:esp32:m5stack_capsule \
  --build-property "compiler.cpp.extra_flags=-DWIFI_PROVISION_POWER_HOLD=46 -DWIFI_PROVISION_BUTTON=42" \
  --upload -p /dev/cu.usbmodemXXXX examples/WiFiProvision
python3 test/hardware/wifiprovision.py /dev/cu.usbmodemXXXX --secrets examples/XiaoC3WiFi/arduino_secrets.h
```

Add `--captive` for a build with `-DWIFI_PROVISION_KEEP_AP` (ESP32 only), `--long`
to wait out the three-minute retry. The secrets file is read for the SSID and
password to type into the board and nothing from it is printed.

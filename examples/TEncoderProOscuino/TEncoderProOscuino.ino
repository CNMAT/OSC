/*
 * TEncoderProOscuino — Oscuino over SLIP-encoded USB serial, for LilyGO T-Encoder-Pro
 * -----------------------------------------------------------------------------
 * HAND-WRITTEN SKETCH — only TEncoderProOscuino.html beside it is generated.
 * It started as a generator output and was taken out of the template when the
 * display and touch were wired, for a measured reason: arduino-cli discovers
 * libraries by SCANNING a sketch's #include lines, before and regardless of
 * any preprocessor gating. A <Arduino_GFX_Library.h> in the shared template,
 * even inside #ifdef, would make every one of the eleven generated boards
 * refuse to build on a machine without that library. The same measurement is
 * why the RGB block keeps Adafruit_NeoPixel behind -DOSC_RGB_USE_NEOPIXEL.
 *
 * Board : LilyGO T-Encoder-Pro (ESP32-S3-R8)
 * FQBN  : esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB
 * Needs : "GFX Library for Arduino" (moononournation) >= 1.6.3, and SensorLib
 *         (lewisxhe). Both come from the Arduino library index — deliberately,
 *         because the vendor's own copies are a fork and an unindexed tree.
 *
 * No board definition exists in any installed core, so it is built as the
 * generic esp32s3 with the four options the vendor's README specifies
 * (https://github.com/Xinyuan-LilyGO/T-Encoder-Pro) -- omit them and the PSRAM
 * mode and partition table are wrong. That generic variant also declares an RGB
 * LED and an LED_BUILTIN this board does not have, hence OSC_NO_RGB and
 * OSC_NO_LED: a board must not announce hardware it lacks.
 *
 * PINS, from the vendor's libraries/Mylibrary/pin_config.h rather than a README
 * summary: knob phases A=IO1 B=IO2, key=IO0, buzzer=IO17, I2C SDA=IO5 SCL=IO6,
 * touch INT=IO9 RST=IO8, screen CS=IO10 SCLK=IO12 RST=IO4 EN=IO3 with QSPI data
 * on IO11/IO13/IO7/IO14.
 *
 * THERE ARE TWO HARDWARE REVISIONS and pin_config.h is a build-time switch
 * between them: DXQ120MYB2416A is an SH8601 panel with CHSC5816 touch (0x2E),
 * TFD12MASBCTB4_V0_07 a CO5300 panel with CST816 touch (0x15). The vendor's
 * examples/GFX/GFX.ino builds Arduino_SH8601 under a `// DXQ120MYB2416A`
 * comment while pin_config.h has that macro commented OUT -- so their headline
 * display example does not drive the panel their own default selects. The one
 * CO5300 construction in the whole vendor repository is inside Lvgl_CIT.ino's
 * TFD12MASBCTB4_V0_07 branch.
 *
 * So this sketch trusts NEITHER. The two touch controllers answer at different
 * I2C addresses, so setup() sweeps the bus before constructing anything and
 * lets the address pick the pair: Arduino_OLED is the common base of both
 * panels (same nine-argument constructor) and TouchDrvInterface of both touch
 * drivers. What it found is on /diag. MEASURED HERE 2026-09-06, three hard
 * resets, same answer each time: "i2c: 0x2E rev:chsc5816/sh8601" -- the unit on
 * this bench is the SH8601/CHSC5816 revision, the one the vendor's default has
 * commented out.
 *
 * WIRED UP: knob (/enq/enc), key (/enq/btn), the AMOLED (/enq/display), the
 * touch panel (/enq/touch) and the bus sweep (/enq/diag). Each of the first
 * four announces only on evidence -- the panel on begin()'s return, the touch
 * on an ACK -- so a board of the other revision announces the other pairing.
 * NOT WIRED: the IO17 buzzer (/buzz).
 *
 * WHAT THIS BOARD DOES NOT HAVE, recorded because this comment claimed
 * otherwise until 2026-09-06: no IMU, no RTC, no PMU, no haptic engine. Those
 * four came from the driver directory names inside the vendor's Arduino_DriveBus
 * library -- a generic multi-board library -- and not from anything about this
 * board. The corrected schematic's parts list is U1 Type-C, U2 LDO, U3
 * ESP32-S3, U4 encoder, U5 W25Q128, Q1, D1/D2, D3, a speaker, SW1, SW2, X1 and
 * the connectors; the README's module list is MCU, screen+touch, encoder,
 * buzzer. The README's prose also promises a "vibration motor" that appears in
 * no schematic and that owners discuss ADDING in the vendor's issue #2.
 *
 * KNOB VERIFIED BY MOTION 2026-09-06: turned by hand, 216 samples, position
 * swept 0..24, first motion counted UP -- so clockwise increases, taking the
 * instruction (clockwise first) at its word. At rest the pins read A=1, B=0.
 *
 * THE KEY IS STILL UNSEEN, and the documentation now says why that is not a
 * contradiction. The corrected schematic draws the encoder U4 with three
 * electrical terminals -- A(1), C(2), B(3), its two SHELL pads unconnected --
 * so the press is NOT an encoder terminal. It is a separate tact switch SW1,
 * pin 1 to GND and pin 2 to net KEY, with R8 100K to 3.3V and C14 100nF, and
 * net KEY lands on the ESP32-S3's GPIO0. Four vendor sources agree on IO0
 * (schematic, README pin table, pin_config.h, product-page pinmap). What no
 * vendor document states is what mechanically closes SW1; the only source that
 * speaks to it is a customer review on LilyGO's own product page saying the
 * button is pressed by clicking the SCREEN, and only "in the very center".
 * Every window here pressed the knob body instead. IO0 is also the BOOT
 * strapping pin, so it must never be sampled as a boot-time gate.
 *
 * Pair this with TEncoderProOscuino.html, sitting next to this file. Serve that page
 * over http://localhost or https:// (Web Serial refuses a file:// origin), click
 * Connect, pick the board. No server process and no npm install.
 *
 * ADDRESSES — the standard Oscuino set, so this sketch also answers the CNMAT
 * Max/MSP patches and the other Serial* examples' clients.
 *
 *   /d/<pin>            digitalRead            -> /d/<pin> <int>
 *   /d/<pin>/u          digitalRead w/ pullup  -> /d/<pin>/u <int>
 *   /d/<pin> <int>      digitalWrite
 *   /d/<pin> <float>    analogWrite, 0.0 .. 1.0
 *   /a/<pin>            analogRead             -> /a/<pin> <int>
 *   /a/<pin> <int>      digitalWrite on the matching digital pin
 *   /a/<pin> <float>    analogWrite, 0.0 .. 1.0
 *   /tone/<pin> <freq> [<ms>]   square wave; no argument stops it
 *   /s/m                micros                 -> /s/m <int>
 *   /s/d                digital pin count      -> /s/d <int>
 *   /s/a                analog pin count       -> /s/a <int>
 *   /s/l <int>          set LED_BUILTIN        -> /s/l <int>
 *                       (only on boards with one — see BOARD_HAS_LED
 *                        in OSCBoards.h; absent, not faked, without)
 *   /rgb <r> <g> <b>    every pixel            -> /rgb <r> <g> <b>
 *   /rgb/<n> <r> <g> <b>  one pixel            -> /rgb/<n> <r> <g> <b>
 *   /rgb/bright <int>   0..255                 -> /rgb/bright <int>
 *                       (announced as /enq/rgb <count>; see the RGB block
 *                        below for when this exists at all)
 *
 * Everything travels as an OSCBundle in both directions, which is what the
 * stock Oscuino clients expect. Tick "bundle" in the companion page.
 */

#define OSC_NO_RGB        // no colour LED; the generic esp32s3 variant claims one
#define OSC_NO_LED        // and no plain LED either -- LED_BUILTIN is that same phantom pixel
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>

// OSCBoards.h defines BOARD_HAS_USB_SERIAL and thisBoardsSerialUSB for boards
// with native USB. Selecting through the macro rather than naming a port keeps
// this example working when a variant calls its USB CDC something unexpected.
#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

// This variant's NUM_*_PINS macros match its pads; nothing to clamp.

// A user button, when boards.json names its pin. Guessing one is not
// harmless -- the pin is an input on one board and a bus line on the next --
// so a board that does not declare it simply has no /btn, and the generic
// /d/<pin> read still works for anyone who knows the wiring.
#define BOARD_BUTTON_PIN 0
#define BOARD_BUTTON_ACTIVE_LOW 1

// A rotary encoder, when boards.json names its two phase pins. Same rule as
// the button: an undeclared board simply has no /enc.
#define BOARD_ENCODER_A 1
#define BOARD_ENCODER_B 2

// -----------------------------------------------------------------------------
// The panel and the touch controller. Pin numbers are the vendor's own
// pin_config.h names, kept spelled the same so a reader can diff the two files.
// -----------------------------------------------------------------------------
#define SCREEN_CS     10
#define SCREEN_SCLK   12
#define SCREEN_SDIO0  11
#define SCREEN_SDIO1  13
#define SCREEN_SDIO2   7
#define SCREEN_SDIO3  14
#define SCREEN_RST     4
#define SCREEN_EN      3        // the README's pin table calls IO3 "VCI EN":
                                // the panel's analogue supply, not a backlight
                                // (an AMOLED has none). HIGH before begin().
#define SCREEN_W     390
#define SCREEN_H     390

#define IIC_SDA        5
#define IIC_SCL        6
#define TOUCH_RST      8
#define TOUCH_INT      9
#define CST816_ADDR 0x15        // pin_config.h line 15; SensorLib's
#define CHSC5816_ADDR 0x2E      // CST816_SLAVE_ADDRESS agrees. The other
                                // revision's controller sits at 0x2E, which is
                                // how /diag tells the two boards apart.

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TouchDrv.hpp>

static const unsigned long BAUD = 115200;   // ignored on native USB, but Web Serial still demands a value

static OSCBundle bundleOUT;

// -----------------------------------------------------------------------------
// "/12" for pin 12, without pulling in sprintf. Returns a pointer into a static
// buffer, so use the result before calling again.
// -----------------------------------------------------------------------------
static char *numToOSCAddress(int pin) {
  static char s[10];
  int i = 9;
  s[i--] = '\0';
  do {
    s[i--] = "0123456789"[pin % 10];
    pin /= 10;
  } while (pin && i);
  s[i] = '/';
  return &s[i];
}

// Builds "<prefix></pin>[suffix]" into out. prefix is 2 chars, so 12 is ample.
static void pinAddress(char *out, const char *prefix, int pin, const char *suffix) {
  strcpy(out, prefix);
  strcat(out, numToOSCAddress(pin));
  if (suffix) strcat(out, suffix);
}

// -----------------------------------------------------------------------------
// The onboard colour LED, where there is one AND something here can drive it.
//
// OSCBoards.h answers the first half: BOARD_HAS_RGB, plus which of the four
// spellings the core used. This asks the second half, because a pixel with no
// reachable driver is still absence as far as ADDRESSES.md is concerned:
//
//   BOARD_RGB_CORE_DRIVEN  the esp32 core drives it itself through
//                          rgbLedWrite() — no library, no dependency, on by
//                          default because it costs a reader nothing
//   BOARD_RGB_NEOPIXEL     needs Adafruit_NeoPixel, so it is OFF unless you
//                          build with -DOSC_RGB_USE_NEOPIXEL
//
// The NeoPixel half is opt-in to keep this README's own rule: "an example that
// will not compile without a second install is a poor front door". Turning it
// on is one flag; leaving it off costs only that those boards announce no rgb
// capability, which the contract already calls absence.
//
// It cannot instead be made automatic on whether the library happens to be
// installed. __has_include(<Adafruit_NeoPixel.h>) was tried and is always false
// here, measured: arduino-cli discovers libraries by scanning a sketch's
// includes, so an include hidden behind the test is never seen, the library
// never joins the include path, and the test answers no however the machine is
// set up. A build flag is honest about the choice; __has_include only looked
// like it was.
//
// DotStar and discrete LEDR/LEDG/LEDB boards announce nothing: the first wants
// another library again, and the second's polarity is per-board and untested
// here — several of those parts are wired active-LOW, which would invert every
// colour. Announcing a capability we cannot honour is worse than silence.
// -----------------------------------------------------------------------------
#if defined(BOARD_RGB_CORE_DRIVEN)
#define OSC_RGB 1
#define OSC_RGB_COUNT 1
#elif defined(BOARD_RGB_NEOPIXEL) && defined(OSC_RGB_USE_NEOPIXEL)
#include <Adafruit_NeoPixel.h>
#define OSC_RGB 1
#ifdef NEOPIXEL_NUM
#define OSC_RGB_COUNT NEOPIXEL_NUM
#else
#define OSC_RGB_COUNT 1
#endif
static Adafruit_NeoPixel oscPixels(OSC_RGB_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

#ifdef OSC_RGB
// Zero-initialised, so the board starts dark and known rather than showing
// whatever the bootloader left in the pixel.
static uint8_t oscRgb[OSC_RGB_COUNT][3];
static uint8_t oscRgbBright = 255;

static void oscRgbShow() {
#if defined(BOARD_RGB_CORE_DRIVEN)
  // rgbLedWrite() has no brightness of its own, so scale on the way out.
  rgbLedWrite(RGB_BUILTIN, (uint8_t)((int)oscRgb[0][0] * oscRgbBright / 255),
                           (uint8_t)((int)oscRgb[0][1] * oscRgbBright / 255),
                           (uint8_t)((int)oscRgb[0][2] * oscRgbBright / 255));
#else
  oscPixels.setBrightness(oscRgbBright);
  for (int i = 0; i < OSC_RGB_COUNT; i++)
    oscPixels.setPixelColor(i, oscPixels.Color(oscRgb[i][0], oscRgb[i][1], oscRgb[i][2]));
  oscPixels.show();
#endif
}

static void oscRgbBegin() {
#ifdef NEOPIXEL_POWER
  // Several boards gate the pixel's supply; NEOPIXEL_POWER_ON names the active
  // level where the variant bothers to say, since it is not always HIGH.
  pinMode(NEOPIXEL_POWER, OUTPUT);
#ifdef NEOPIXEL_POWER_ON
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
#else
  digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
#endif
#ifndef BOARD_RGB_CORE_DRIVEN
  oscPixels.begin();
#endif
  oscRgbShow();
}

// Three ints, clamped. Anything else is a malformed message and answers
// nothing, per ADDRESSES.md — no sentinels.
static bool oscRgbArgs(OSCMessage &msg, uint8_t *out) {
  if (msg.size() < 3 || !msg.isInt(0) || !msg.isInt(1) || !msg.isInt(2)) return false;
  for (int i = 0; i < 3; i++) {
    int32_t v = msg.getInt(i);
    out[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
  }
  return true;
}
#endif

// -----------------------------------------------------------------------------
// routes
// -----------------------------------------------------------------------------

// "/d/<pin>" — write with an int, analogWrite with a float, read with neither.
void routeDigital(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < NUM_DIGITAL_PINS; pin++) {
    int matched = msg.match(numToOSCAddress(pin), addrOffset);
    if (!matched) continue;

    char addr[12];
    if (msg.isInt(0)) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, msg.getInt(0) > 0 ? HIGH : LOW);
    } else if (msg.isFloat(0)) {
      float v = msg.getFloat(0);
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      pinMode(pin, OUTPUT);
      analogWrite(pin, (int)(v * 255.0f + 0.5f));
    } else if (msg.fullMatch("/u", matched + addrOffset)) {
      pinMode(pin, INPUT_PULLUP);
      pinAddress(addr, "/d", pin, "/u");
      bundleOUT.add(addr).add((intOSC_t)digitalRead(pin));
    } else {
      pinMode(pin, INPUT);
      pinAddress(addr, "/d", pin, NULL);
      bundleOUT.add(addr).add((intOSC_t)digitalRead(pin));
    }
    return;
  }
}

// "/a/<pin>"
void routeAnalog(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < NUM_ANALOG_INPUTS; pin++) {
    int matched = msg.match(numToOSCAddress(pin), addrOffset);
    if (!matched) continue;

    char addr[12];
    if (msg.isInt(0)) {
      pinMode(analogInputToDigitalPin(pin), OUTPUT);
      digitalWrite(analogInputToDigitalPin(pin), msg.getInt(0) > 0 ? HIGH : LOW);
    } else if (msg.isFloat(0)) {
      float v = msg.getFloat(0);
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      pinMode(analogInputToDigitalPin(pin), OUTPUT);
      analogWrite(analogInputToDigitalPin(pin), (int)(v * 255.0f + 0.5f));
    } else {
      pinAddress(addr, "/a", pin, NULL);
      bundleOUT.add(addr).add((intOSC_t)analogRead(pin));
    }
    return;
  }
}

// "/tone/<pin> <freq> [<duration ms>]" — no argument stops the tone.
void routeTone(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < NUM_DIGITAL_PINS; pin++) {
    if (!msg.match(numToOSCAddress(pin), addrOffset)) continue;

    unsigned int freq = 0;
    if (msg.isInt(0))        freq = (unsigned int)msg.getInt(0);
    else if (msg.isFloat(0)) freq = (unsigned int)msg.getFloat(0);

    if (freq == 0) noTone(pin);
    else if (msg.isInt(1))   tone(pin, freq, msg.getInt(1));
    else                     tone(pin, freq);
    return;
  }
}

// "/s/..." — system queries and the built-in LED.
void routeSystem(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/m", addrOffset)) bundleOUT.add("/s/m").add((intOSC_t)micros());
  if (msg.fullMatch("/d", addrOffset)) bundleOUT.add("/s/d").add((intOSC_t)NUM_DIGITAL_PINS);
  if (msg.fullMatch("/a", addrOffset)) bundleOUT.add("/s/a").add((intOSC_t)NUM_ANALOG_INPUTS);
#ifdef BOARD_HAS_LED
  if (msg.fullMatch("/l", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, v > 0 ? HIGH : LOW);
    bundleOUT.add("/s/l").add((intOSC_t)v);
  }
#endif
}

// /state and /rate, the core streaming pair (ADDRESSES.md). A pin-only board
// has nothing to stream but a heartbeat, so the default is 0 -- silent until
// a client asks -- rather than chattering at every browser that connects.
// /state still answers on request at any time, and the sequence number is
// what makes a dropped packet visible instead of merely late.
static int32_t  seq      = 0;
static uint32_t reportMs = 0;            // 0 = not streaming

#ifdef BOARD_ENCODER_A
// Quadrature, polled from loop(). A hand-turned knob moves far slower than
// this loop runs, and polling keeps the block portable -- attachInterrupt's
// pin-to-interrupt mapping is not the same on every core this template builds
// for. The table is the standard one: index the previous two-bit state and the
// current one, and it yields -1, 0 or +1. Detented encoders usually produce
// four counts per click; /enc reports raw counts and lets the client decide.
static int32_t encPos = 0, encLast = 0;
static uint8_t encPrev = 0;

static void encPoll() {
  const uint8_t now = (uint8_t)((digitalRead(BOARD_ENCODER_A) << 1)
                              |  digitalRead(BOARD_ENCODER_B));
  if (now == encPrev) return;
  static const int8_t step[16] = { 0, -1,  1,  0,
                                   1,  0,  0, -1,
                                  -1,  0,  0,  1,
                                   0,  1, -1,  0 };
  encPos += step[(encPrev << 2) | now];
  encPrev = now;
}

static void addEnc() {
  bundleOUT.add("/enc").add((intOSC_t)encPos).add((intOSC_t)(encPos - encLast));
  encLast = encPos;                 // delta is "since the last time you asked"
}

void routeEnc(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/zero", addrOffset)) {
    encPos = 0; encLast = 0;
    addEnc();
    return;
  }
  addEnc();
}
#endif

#ifdef BOARD_BUTTON_PIN
static void addBtn() {
  const int raw = digitalRead(BOARD_BUTTON_PIN);
  bundleOUT.add("/btn").add((intOSC_t)(BOARD_BUTTON_ACTIVE_LOW ? raw == LOW : raw == HIGH));
}

// /enq/btn promises a client can ASK, not merely that it rides in the stream.
void routeBtn(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addBtn();
}
#endif

static void addState() {
  bundleOUT.add("/state").add((intOSC_t)seq).add((intOSC_t)millis());
#ifdef BOARD_BUTTON_PIN
  addBtn();                      // beside /state, as the hand-written twins do
#endif
}

void routeState(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addState();
}

void routeRate(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (msg.isInt(0)) {
    const int32_t v = msg.getInt(0);
    // 0 STOPS. Clamping it to a minimum would make "be quiet" stream faster,
    // which is exactly the bug found on the C6 twin on 2026-09-04.
    reportMs = (v <= 0) ? 0 : (uint32_t)constrain(v, 20, 2000);
  }
  bundleOUT.add("/rate").add((intOSC_t)reportMs);
}

#ifdef OSC_RGB
// "/rgb <r> <g> <b>" — every pixel at once.
void routeRgbAll(OSCMessage &msg) {
  uint8_t c[3];
  if (!oscRgbArgs(msg, c)) return;
  for (int i = 0; i < OSC_RGB_COUNT; i++) {
    oscRgb[i][0] = c[0]; oscRgb[i][1] = c[1]; oscRgb[i][2] = c[2];
  }
  oscRgbShow();
  bundleOUT.add("/rgb").add((intOSC_t)c[0]).add((intOSC_t)c[1]).add((intOSC_t)c[2]);
}

// "/rgb/bright <int>" — 0..255, applied to whatever is already showing.
void routeRgbBright(OSCMessage &msg) {
  if (msg.size() < 1 || !msg.isInt(0)) return;
  int32_t b = msg.getInt(0);
  oscRgbBright = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
  oscRgbShow();
  bundleOUT.add("/rgb/bright").add((intOSC_t)oscRgbBright);
}

// "/rgb/<n> <r> <g> <b>" — one pixel, numbered like /d/<pin> above. Reached by
// route() rather than dispatch(), which is why "/rgb" and "/rgb/bright" also
// arrive here: neither matches a numeral, so both fall through harmlessly.
void routeRgbOne(OSCMessage &msg, int addrOffset) {
  for (int i = 0; i < OSC_RGB_COUNT; i++) {
    if (!msg.match(numToOSCAddress(i), addrOffset)) continue;
    uint8_t c[3];
    if (!oscRgbArgs(msg, c)) return;
    oscRgb[i][0] = c[0]; oscRgb[i][1] = c[1]; oscRgb[i][2] = c[2];
    oscRgbShow();
    char addr[16];
    pinAddress(addr, "/rgb", i, NULL);
    bundleOUT.add(addr).add((intOSC_t)c[0]).add((intOSC_t)c[1]).add((intOSC_t)c[2]);
    return;
  }
}
#endif

// -----------------------------------------------------------------------------
// The 390x390 CO5300 AMOLED, over QSPI.
//
// Every constructor argument below was compiled before it was written down. The
// vendor's own example is not a safe model twice over: it builds Arduino_SH8601
// (the OTHER revision's panel), and it is written against LilyGO's fork of
// Arduino_GFX 1.5.0, which upstream 1.5.0 is not -- upstream gained SH8601 only
// at 1.6.3, and the fork adds Display_Brightness()/SetContrast() that upstream
// spells setBrightness()/setContrast(). Two more differences bite anyone
// copying the vendor's line into a current library: the `bool ips` argument is
// gone from the constructor (ten arguments against nine is a hard overload
// error, not a silent misbind), and the unprefixed colour aliases BLACK/WHITE
// were dropped in favour of RGB565_BLACK/RGB565_WHITE.
// -----------------------------------------------------------------------------
// Arduino_OLED is the common base of both panels -- same nine-argument
// constructor, and it is where setBrightness()/setContrast() are declared pure
// virtual -- so one pointer serves either revision.
static Arduino_DataBus *gfxBus = NULL;
static Arduino_OLED    *gfx    = NULL;
static bool     displayOk = false;
static int16_t  dispW = 0, dispH = 0;
static const char *panelName = "none";

// Which revision this actually is, decided by the I2C bus in setup() before
// anything is constructed. See the sweep below.
static bool has0x15 = false, has0x2E = false;

// 8x8 glyphs at the size Arduino_GFX scales them by; used to centre text.
static const int GLYPH_W = 6, GLYPH_H = 8;

static uint16_t rgb565(int32_t r, int32_t g, int32_t b) {
  r = constrain(r, 0, 255); g = constrain(g, 0, 255); b = constrain(b, 0, 255);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void displayBegin() {
  // IO3 first. The README's pin table calls it "VCI EN" -- the panel's supply,
  // not a backlight -- and the vendor raises it before touching the bus.
  pinMode(SCREEN_EN, OUTPUT);
  digitalWrite(SCREEN_EN, HIGH);

  gfxBus = new Arduino_ESP32QSPI(SCREEN_CS, SCREEN_SCLK, SCREEN_SDIO0,
                                 SCREEN_SDIO1, SCREEN_SDIO2, SCREEN_SDIO3);
  // The panel cannot be asked what it is -- this QSPI bus is write-only in
  // both drivers -- so the TOUCH CONTROLLER names the revision and the panel
  // follows it. That is the vendor's own pairing, from the two lines of
  // pin_config.h: DXQ120MYB2416A is SH8601 + CHSC5816 (0x2E),
  // TFD12MASBCTB4_V0_07 is CO5300 + CST816 (0x15).
  if (has0x15) {
    gfx = new Arduino_CO5300(gfxBus, SCREEN_RST, 0 /* rotation */,
                             SCREEN_W, SCREEN_H, 0, 0, 0, 0);
    panelName = "co5300";
  } else {
    gfx = new Arduino_SH8601(gfxBus, SCREEN_RST, 0 /* rotation */,
                             SCREEN_W, SCREEN_H, 0, 0, 0, 0);
    panelName = "sh8601";
  }
  // begin() RETURNS A BOOL, and both its failures are silent ones: the SPI bus
  // failing to initialise, and a DMA-capable allocation failing. Ignoring it
  // leaves the sketch drawing into a dead bus behind a dark screen, which looks
  // exactly like a panel that is simply off. Announce nothing unless it worked.
  displayOk = gfx->begin(40000000);
  if (!displayOk) return;

  // Ask the driver for the geometry rather than announce the constant we fed
  // it: pin_config.h's 390x390 sits under a comment naming the OTHER revision.
  dispW = gfx->width();
  dispH = gfx->height();
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
}

// Centres up to msg.size() lines. Replies with the count, which is what
// ADDRESSES.md specifies and what test/hardware/contractprobe.py waits 0.6 s
// for before failing a board that announced a display it cannot drive.
static void displayText(OSCMessage &msg, uint8_t scale) {
  char s[64];
  const int lineH = GLYPH_H * scale + 6;
  int n = 0;
  for (int i = 0; i < msg.size(); i++) if (msg.isString(i)) n++;

  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(scale);
  gfx->setTextColor(RGB565_WHITE);

  int y = (dispH - n * lineH) / 2;
  if (y < 0) y = 0;
  int drawn = 0;
  for (int i = 0; i < msg.size(); i++) {
    if (!msg.isString(i)) continue;
    msg.getString(i, s, sizeof(s));
    int x = (dispW - (int)strlen(s) * GLYPH_W * scale) / 2;
    if (x < 0) x = 0;
    gfx->setCursor(x, y + drawn * lineH);
    gfx->print(s);
    drawn++;
  }
  bundleOUT.add("/display/text").add((intOSC_t)drawn);
}

void routeDisplay(OSCMessage &msg, int addrOffset) {
  if (!displayOk) return;                 // absence is silence, at run time too

  if (msg.fullMatch("/text", addrOffset)) { displayText(msg, 3); return; }
  if (msg.fullMatch("/big",  addrOffset)) { displayText(msg, 6); return; }

  if (msg.fullMatch("/clear", addrOffset)) {
    gfx->fillScreen(RGB565_BLACK);
    bundleOUT.add("/display/text").add((intOSC_t)0);
    return;
  }
  if (msg.fullMatch("/fill", addrOffset)) {
    if (msg.size() >= 3)
      gfx->fillScreen(rgb565(msg.getInt(0), msg.getInt(1), msg.getInt(2)));
    return;
  }
  if (msg.fullMatch("/rect", addrOffset)) {
    if (msg.size() >= 7)
      gfx->fillRect(msg.getInt(0), msg.getInt(1), msg.getInt(2), msg.getInt(3),
                    rgb565(msg.getInt(4), msg.getInt(5), msg.getInt(6)));
    return;
  }
  if (msg.fullMatch("/circle", addrOffset)) {
    if (msg.size() >= 6)
      gfx->fillCircle(msg.getInt(0), msg.getInt(1), msg.getInt(2),
                      rgb565(msg.getInt(3), msg.getInt(4), msg.getInt(5)));
    return;
  }
  if (msg.fullMatch("/bl", addrOffset)) {
    // An AMOLED has no backlight; the contract's "backlight/brightness" is the
    // panel's own brightness register, which is what setBrightness writes.
    if (msg.isInt(0)) gfx->setBrightness((uint8_t)constrain(msg.getInt(0), 0, 255));
    return;
  }
  if (msg.fullMatch("/invert", addrOffset)) {
    if (msg.isInt(0)) gfx->invertDisplay(msg.getInt(0) != 0);
    return;
  }
}

// -----------------------------------------------------------------------------
// The CST816 touch panel, and the bus sweep that says which board this is.
//
// pin_config.h switches between two revisions whose touch controllers answer at
// DIFFERENT addresses -- CST816 at 0x15, CHSC5816 at 0x2E. So the bus settles
// which revision is on the bench, and neither a comment nor a build flag has to
// be trusted. begin() returning false IS the absence test: it is a NACK at
// 0x15, not a driver opinion.
// -----------------------------------------------------------------------------
static TouchDrvCST816     touchCst;
static TouchDrvCHSC5816   touchChsc;
static TouchDrvInterface *touchDev = NULL;
static bool     touchOk   = false;
static bool     touchDown = false;
static int16_t  touchX = 0, touchY = 0;

// EDGE COUNTERS, because a host cannot poll fast enough to catch a press.
//
// Three windows in one session watched /touch and /btn over OSC at about
// 1.5 Hz and saw nothing, and a uniform negative like that says as much about
// the sampler as the switch. A press is momentary; a COUNT is not. These latch
// every edge since boot, so whoever is at the bench can act whenever they like
// and a single /diag read minutes later still answers "did it ever happen?"
// That is the same lesson as test/hardware/pinhunt.py's sampling fault, fixed
// on the right side of the wire this time: do not sample for a momentary
// event, latch it.
static uint32_t touchSeen = 0, btnSeen = 0;
static int16_t  touchFirstX = -1, touchFirstY = -1;
static char     busNote[80] = "";

// Runs FIRST, before either driver is constructed, because everything else
// keys off it. Written into /diag rather than asserted in a comment: what
// actually ACKed on this board, on this power-up.
static void i2cSweep() {
  // RELEASE THE CONTROLLER'S RESET FIRST. A part held in reset does not ACK,
  // and this cost a wrong answer: the first boot after flashing found 0x2E and
  // the next found an empty bus, because IO8 happened to still be high from the
  // previous firmware and then was not. Sweeping before driving TOUCH_RST asks
  // the bus a question the bus cannot answer. The hold and release times are
  // the ones SensorLib's own CST816 driver sets in beforeBegin().
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(30);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);

  Wire.begin(IIC_SDA, IIC_SCL);
  char *p = busNote;
  p += sprintf(p, "i2c:");
  for (uint8_t a = 1; a < 0x78 && (p - busNote) < 56; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) continue;
    p += sprintf(p, " 0x%02X", a);
    if (a == CST816_ADDR)   has0x15 = true;
    if (a == CHSC5816_ADDR) has0x2E = true;
  }
  sprintf(p, " rev:%s", has0x15 ? "cst816/co5300"
                      : has0x2E ? "chsc5816/sh8601" : "unknown");
}

static void touchBegin() {
  if (has0x15) {
    touchDev = &touchCst;
    touchDev->setPins(TOUCH_RST, TOUCH_INT);
    touchOk = touchCst.begin(Wire, CST816_ADDR, IIC_SDA, IIC_SCL);
  } else if (has0x2E) {
    touchDev = &touchChsc;
    touchDev->setPins(TOUCH_RST, TOUCH_INT);
    touchOk = touchChsc.begin(Wire, CHSC5816_ADDR, IIC_SDA, IIC_SCL);
  }
  if (touchOk) {
    // The vendor reads this pin directly rather than trusting the driver to,
    // so make sure it is an input before anything gates on it.
    pinMode(TOUCH_INT, INPUT);
    touchDev->setMaxCoordinates(SCREEN_W, SCREEN_H);
    touchDev->setSwapXY(false);
    touchDev->setMirrorXY(false, false);
  } else {
    touchDev = NULL;
  }
}

// TOUCH_INT IS THE GATE, not an optimisation. Reading the controller whenever
// you feel like it returns whatever was last latched, and this board proves it:
// polled unconditionally, the CHSC5816 reported a point at (182, 191) that
// never changed across 891 samples with nothing near the glass -- a permanent
// phantom finger, which the contract probe duly saw as /touch being "streamed
// while down" forever. The vendor's own examples/CHSC5816/CHSC5816.ino reads
// only inside `if (digitalRead(TOUCH_INT) == LOW)`, and that is the contract:
// the controller asserts IO9 when it has something to say.
static void touchPoll() {
  if (!touchOk) return;
  if (digitalRead(TOUCH_INT) != LOW) {   // idle high: nothing latched
    touchDown = false;
    return;
  }
  const TouchPoints &p = touchDev->getTouchPoints();
  if (p.hasPoints()) {
    touchX = p.getPoint(0).x;
    touchY = p.getPoint(0).y;
    if (!touchDown) {                     // rising edge only
      touchSeen++;
      if (touchFirstX < 0) { touchFirstX = touchX; touchFirstY = touchY; }
    }
    touchDown = true;
  } else {
    touchDown = false;
  }
}

// The key's own edge counter. /btn still reports the instantaneous level, which
// is what the contract asks for; this only remembers that an edge happened.
static void btnPoll() {
  static bool was = false;
  const bool now = (digitalRead(BOARD_BUTTON_PIN) == LOW);   // active low
  if (now && !was) btnSeen++;
  was = now;
}

static void addTouch() {
  bundleOUT.add("/touch").add((intOSC_t)touchX).add((intOSC_t)touchY);
}

void routeTouch(OSCMessage &msg, int addrOffset) {
  if (!touchOk) return;
  if (msg.fullMatch("/map", addrOffset)) {
    if (msg.size() >= 3) {
      touchDev->setSwapXY(msg.getInt(0) != 0);
      touchDev->setMirrorXY(msg.getInt(1) != 0, msg.getInt(2) != 0);
    }
    bundleOUT.add("/touch/map")
      .add((intOSC_t)(msg.size() >= 3 ? msg.getInt(0) : 0))
      .add((intOSC_t)(msg.size() >= 3 ? msg.getInt(1) : 0))
      .add((intOSC_t)(msg.size() >= 3 ? msg.getInt(2) : 0));
    return;
  }
  addTouch();
}

void routeDiag(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  bundleOUT.add("/diag").add(busNote);
  bundleOUT.add("/diag").add(panelName);
  char seen[64];
  sprintf(seen, "seen touch:%lu at %d,%d key:%lu",
          (unsigned long)touchSeen, touchFirstX, touchFirstY,
          (unsigned long)btnSeen);
  bundleOUT.add("/diag").add(seen);
}

// The greeting of ADDRESSES.md: the sketch name, then one /enq line per
// capability actually present. This template drives pins, so the only thing
// it can claim is the plain LED -- and only where the variant has one.
// OSCBoards.h defines BOARD_HAS_LED from LED_BUILTIN, so a board like the
// XIAO ESP32-C3, whose only LED belongs to its battery charger, announces
// nothing here and stays silent on /s/l. Absence is silence.
static void addEnq() {
  bundleOUT.add("/enq").add("TEncoderProOscuino");
#ifdef BOARD_HAS_LED
  bundleOUT.add("/enq/led");
#endif
#ifdef OSC_RGB
  bundleOUT.add("/enq/rgb").add((intOSC_t)OSC_RGB_COUNT);
#endif
#ifdef BOARD_BUTTON_PIN
  bundleOUT.add("/enq/btn").add((intOSC_t)1);
#endif
#ifdef BOARD_ENCODER_A
  bundleOUT.add("/enq/enc");
#endif
  // Both of these are conditional on the hardware ANSWERING, not on this being
  // the board we think it is: the panel on begin()'s return, the touch panel on
  // an ACK at 0x15. A CHSC5816 revision announces no touch and stays silent on
  // it, which is what "absence is silence" means when one board id covers two
  // revisions.
  if (displayOk) bundleOUT.add("/enq/display").add((intOSC_t)dispW).add((intOSC_t)dispH);
  if (touchOk)   bundleOUT.add("/enq/touch").add((intOSC_t)dispW).add((intOSC_t)dispH);
  bundleOUT.add("/enq/diag");
}

void routeEnq(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addEnq();
}

// -----------------------------------------------------------------------------

void setup() {
  SLIPSerial.begin(BAUD);
#ifdef BOARD_HAS_LED
  pinMode(LED_BUILTIN, OUTPUT);
#endif
#ifdef BOARD_BUTTON_PIN
  pinMode(BOARD_BUTTON_PIN, BOARD_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
#endif
#ifdef BOARD_ENCODER_A
  pinMode(BOARD_ENCODER_A, INPUT_PULLUP);
  pinMode(BOARD_ENCODER_B, INPUT_PULLUP);
  encPrev = (uint8_t)((digitalRead(BOARD_ENCODER_A) << 1) | digitalRead(BOARD_ENCODER_B));
#endif
#ifdef OSC_RGB
  oscRgbBegin();
#endif
  i2cSweep();        // first: it decides which panel and which touch driver
  displayBegin();
  touchBegin();

  // Native-USB boards enumerate after begin(); give the host a moment, then
  // greet, so the browser log shows something the instant it connects. The
  // boot greeting is usually lost anyway (the host opens the port later), so
  // /enq is an INBOUND address too -- see routeEnq above.
  delay(300);
  addEnq();
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

// Non-blocking receive. Returns true once a whole packet sits in bundleIN.
//
// ORDERING MATTERS: endofPacket() must be called BEFORE available() on every
// pass. Inside SLIPEncodedSerial, available() drives the SLIP state machine, and
// when it is called while that machine sits on the packet-terminating END with
// more bytes already buffered behind it, the state resets to CHAR — silently
// eating the packet boundary. The stock examples get this ordering right but
// block in the outer while; this version returns instead, so a sketch that also
// has work to do in loop() keeps doing it.
static bool pollOSC(OSCBundle &bundleIN) {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;              // nothing buffered — try later
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t)c);  // read() returns -1 on SLIP error
    }
  }
  return true;
}

// Must outlive a single pass through loop(). pollOSC() returns as soon as the
// serial buffer runs dry, which for anything but a very short frame happens
// part-way through one. When this was declared inside loop() the half-filled
// bundle was destroyed on every such return, so a packet only ever arrived if
// the whole frame happened to be buffered in one go.
static OSCBundle bundleIN;

void loop() {
  if (pollOSC(bundleIN)) {
    if (!bundleIN.hasError()) {
      bundleIN.route("/d", routeDigital);
      bundleIN.route("/a", routeAnalog);
      bundleIN.route("/tone", routeTone);
      bundleIN.route("/s", routeSystem);
      bundleIN.route("/enq", routeEnq);
      bundleIN.route("/state", routeState);
      bundleIN.route("/rate", routeRate);
#ifdef OSC_RGB
      bundleIN.dispatch("/rgb", routeRgbAll);
      bundleIN.dispatch("/rgb/bright", routeRgbBright);
      bundleIN.route("/rgb", routeRgbOne);
#endif
#ifdef BOARD_BUTTON_PIN
      bundleIN.route("/btn", routeBtn);
#endif
#ifdef BOARD_ENCODER_A
      bundleIN.route("/enc", routeEnc);
#endif
      bundleIN.route("/display", routeDisplay);
      bundleIN.route("/touch", routeTouch);
      bundleIN.route("/diag", routeDiag);
    }
    bundleIN.empty();
  }

  // Only transmit when a route actually produced something. Sending an empty
  // bundle every pass would flood the port at loop speed and drown the replies
  // you care about.
#ifdef BOARD_ENCODER_A
  encPoll();
#endif
  touchPoll();
  btnPoll();

  static uint32_t lastReport = 0;
  const uint32_t now = millis();
  if (reportMs != 0 && now - lastReport >= reportMs) {
    lastReport = now;
    seq++;
    addState();
    // "streamed while down", per ADDRESSES.md -- paced by /rate like the rest
    // of the stream, and silent the moment the finger lifts, so a client can
    // tell a release from a stalled stream by the absence of the address.
    if (touchDown) addTouch();
  }

  if (bundleOUT.size() > 0) {
    SLIPSerial.beginPacket();
    bundleOUT.send(SLIPSerial);
    SLIPSerial.endPacket();
    bundleOUT.empty();
  }
}

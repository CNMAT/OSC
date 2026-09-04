// Adafruit HalloWing M0 Express — screen, speaker, sensors over OSC.
//
//   arduino-cli compile -b adafruit:samd:adafruit_hallowing \
//       --upload --port /dev/cu.usbmodemXXXX examples/HallowingOscuino
//
// Needs: Adafruit GFX, Adafruit ST7735 and ST7789, Adafruit LIS3DH,
//        Adafruit NeoPixel, Adafruit FreeTouch.
//
// Open HallowingOscuino.html to drive it. Chrome or Edge, served over
// http://localhost — Web Serial does not work from a file:// URL.
//
// Addresses -- the repository-wide contract in ADDRESSES.md, named by
// capability rather than by board:
//
//   /enq                    answered by a bundle: /enq "HallowingOscuino"
//                             then one /enq/<capability> per thing present:
//                             /enq/display 128 128, /enq/buzz, /enq/rgb 1,
//                             /enq/light, /enq/cap 4, and /enq/imu 3 only
//                             when the LIS3DH answered at boot
//   /display/text "a" ["b"]   two text lines; echoed as /display/text <lines>
//   /display/fill r g b       background colour; echoed
//   /display/bl 0..255        backlight; echoed
//   /buzz hz [ms] [vol]       a note on the DAC speaker, default 150 ms; echoed
//                             as /buzz hz ms. The contract's third int is the
//                             volume 0..255, which on this board is the DAC
//                             swing either side of mid-scale, default 120;
//                             the page sends hz and ms and takes that default
//   /rgb r g b, /rgb/0 r g b  the single NeoPixel; /rgb/bright 0..255; echoed
//   /s/l 0|1                  the red LED; echoed
//   /rate ms                  stream period, 0 stops; echoed
//   /light, /cap, /imu        read once now, answered on the same address
//
// Stream, every /rate ms (default 60), one bundle sampled in one pass so
// every reading in it belongs to the same instant:
//
//   /state <seq> <millis>
//   /light <i>                raw 10-bit ADC on A1
//   /cap <i> <i> <i> <i>      FreeTouch readings for pads A2..A5
//   /imu <f> <f> <f>          g, LIS3DH at +-4 g (absent when not found)
//
// Pin numbers are from Adafruit's own pinout page for this board, not
// guessed: TFT reset 37, DC 38, CS 39, backlight 7; light sensor A1;
// LIS3DH on I2C; speaker on A0, which is the DAC; four capacitive pads on
// A2..A5; NeoPixel D8; red LED D13.
//
// STATUS: verified on the board when first added (commit 6db8786,
// 2026-08-10): the sensor report arrived at ~16 Hz with the accelerometer
// live (az -7088 raw, lying flat), four pads reading, the light sensor
// tracking, and the board kept reporting after screen, pixel, LED and tone
// commands. The audit passes of 2026-08-16 (commit 5d3e177, /enq made
// inbound) and 2026-08-17 (commit 2654c9c, backlight rescaled to 10 bits,
// non-blocking receive) recorded no re-run. Addresses renamed onto
// ADDRESSES.md on 2026-09-03 (the board's own screen, tone, pixel and LED
// names -> /display/text, /display/fill, /display/bl, /buzz, /rgb and
// /rgb/0, /s/l; the board-named sensor blob -> /state + /light + /cap +
// /imu; the accelerometer boolean in the hello -> /enq/imu presence); that
// build is compile-checked and has not been re-run on the board.

#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_FreeTouch.h>
#include <Wire.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define TFT_RST   37
#define TFT_DC    38
#define TFT_CS    39
#define TFT_LITE   7
#define LIGHT_PIN A1
#define SPKR_PIN  A0        // DAC0 — the class-D amp input
#define NEOPIX_PIN 8

#define TFT_W 128
#define TFT_H 128

Adafruit_ST7735   tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_LIS3DH   lis;
Adafruit_NeoPixel pixel(1, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);

// FreeTouch is how a SAMD21 reads a capacitive pad; there is no touchRead()
// as on an ESP32 or a Teensy 3.
static const uint8_t TOUCH_PINS[4] = { A2, A3, A4, A5 };
Adafruit_FreeTouch touch[4] = {
  Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A4, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A5, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
};

static bool     accelOK  = false;
static uint32_t reportEvery = 60;       // ms; 0 = stream off
static int32_t  seq = 0;
static char     line1[24] = "HallowingOscuino";
static char     line2[24] = "waiting for OSC";

/* ---------------------------------------------------------------- outbound */

// Everything the board says goes out as a bundle: the /enq answer, the
// echoes, and the stream. Handlers add to it; flushOut() sends whatever has
// accumulated after a dispatch pass or a stream tick.
static OSCBundle bundleOUT;

static void flushOut() {
  if (bundleOUT.size() == 0) return;
  SLIPSerial.beginPacket(); bundleOUT.send(SLIPSerial); SLIPSerial.endPacket();
  bundleOUT.empty();
}

static void addLight() {
  bundleOUT.add("/light").add((intOSC_t) analogRead(LIGHT_PIN));
}
static void addCap() {
  OSCMessage &m = bundleOUT.add("/cap");
  for (int i = 0; i < 4; i++) m.add((intOSC_t) touch[i].measure());
}
static void addImu() {                          // g, at the +-4 g range set below
  lis.read();
  bundleOUT.add("/imu").add(lis.x_g).add(lis.y_g).add(lis.z_g);
}

/* ------------------------------------------------------------------ screen */

static void redraw(uint16_t bg) {
  tft.fillScreen(bg);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(4, 6);   tft.print(line1);
  tft.setTextSize(2);
  tft.setCursor(4, 26);  tft.print(line2);
}

/* ----------------------------------------------------------------- speaker */

// The speaker hangs off the DAC, so a square wave is just two DAC levels.
// tone() does not drive a DAC pin on SAMD, and analogWrite() to A0 is a
// single DC level, so the note is generated here. Blocking, deliberately:
// the notes are short and it keeps the example readable.
static void beep(uint16_t freq, uint16_t ms, uint8_t vol) {
  if (!freq || !ms) return;
  const uint32_t halfUs = 500000UL / freq;
  const uint32_t cycles = ((uint32_t) ms * 1000UL) / (halfUs * 2);
  const int hi = 512 + (vol * 2);        // 10-bit DAC, idle at mid-scale
  const int lo = 512 - (vol * 2);
  for (uint32_t i = 0; i < cycles; i++) {
    analogWrite(SPKR_PIN, hi); delayMicroseconds(halfUs);
    analogWrite(SPKR_PIN, lo); delayMicroseconds(halfUs);
  }
  analogWrite(SPKR_PIN, 512);            // rest at mid-scale, not at 0
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {          // /display/text "a" ["b"]
  int lines = 0;
  if (m.isString(0)) { m.getString(0, line1, sizeof line1); lines++; }
  if (m.size() > 1 && m.isString(1)) { m.getString(1, line2, sizeof line2); lines++; }
  redraw(ST77XX_BLACK);
  bundleOUT.add("/display/text").add((intOSC_t) lines);
}
static void routeFill(OSCMessage &m) {          // /display/fill r g b
  if (m.size() < 3) return;
  const uint8_t r = constrain(m.getInt(0), 0, 255);
  const uint8_t g = constrain(m.getInt(1), 0, 255);
  const uint8_t b = constrain(m.getInt(2), 0, 255);
  redraw(tft.color565(r, g, b));
  bundleOUT.add("/display/fill").add((intOSC_t) r).add((intOSC_t) g).add((intOSC_t) b);
}
static void routeBacklight(OSCMessage &m) {     // /display/bl 0..255
  if (m.size() >= 1 && m.isInt(0)) {
    // analogWriteResolution(10) in setup() is GLOBAL on SAMD, not per-pin:
    // it rescales this PWM write too, so an unscaled 0-255 here topped out
    // at 255/1023 -- about a quarter of full brightness. Map to 10 bits.
    const int v = constrain(m.getInt(0), 0, 255);
    analogWrite(TFT_LITE, (v << 2) | (v >> 6));   // 255 -> 1023
    bundleOUT.add("/display/bl").add((intOSC_t) v);
  }
}
static void routeBuzz(OSCMessage &m) {          // /buzz hz [ms] [vol]; vol is the DAC swing
  if (m.size() < 1 || !m.isInt(0)) return;
  const uint16_t f  = constrain(m.getInt(0), 0, 8000);
  const uint16_t ms = (m.size() > 1 && m.isInt(1)) ? constrain(m.getInt(1), 0, 2000) : 150;
  const uint8_t  v  = (m.size() > 2 && m.isInt(2)) ? constrain(m.getInt(2), 0, 255) : 120;
  beep(f, ms, v);
  bundleOUT.add("/buzz").add((intOSC_t) f).add((intOSC_t) ms);
}
static void setPixel(OSCMessage &m, const char *echo) {   // r g b
  if (m.size() < 3) return;
  const int r = constrain(m.getInt(0), 0, 255);
  const int g = constrain(m.getInt(1), 0, 255);
  const int b = constrain(m.getInt(2), 0, 255);
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
  bundleOUT.add(echo).add((intOSC_t) r).add((intOSC_t) g).add((intOSC_t) b);
}
// One NeoPixel, so "all of them" and "number 0" are the same LED.
static void routeRgbAll(OSCMessage &m) { setPixel(m, "/rgb"); }     // /rgb r g b
static void routeRgb0(OSCMessage &m)   { setPixel(m, "/rgb/0"); }   // /rgb/0 r g b
static void routeRgbBright(OSCMessage &m) {     // /rgb/bright 0..255
  if (m.size() >= 1 && m.isInt(0)) {
    const int v = constrain(m.getInt(0), 0, 255);
    pixel.setBrightness(v); pixel.show();
    bundleOUT.add("/rgb/bright").add((intOSC_t) v);
  }
}
static void routeLed(OSCMessage &m) {           // /s/l 0|1
  if (m.size() >= 1 && m.isInt(0)) {
    const int v = m.getInt(0) ? 1 : 0;
    digitalWrite(LED_BUILTIN, v ? HIGH : LOW);
    bundleOUT.add("/s/l").add((intOSC_t) v);
  }
}
static void routeRate(OSCMessage &m) {          // /rate <ms>, 0 stops
  if (m.size() >= 1 && m.isInt(0)) {
    const int r = m.getInt(0);
    reportEvery = (r <= 0) ? 0 : constrain(r, 20, 5000);
    bundleOUT.add("/rate").add((intOSC_t) reportEvery);
  }
}
static void routeLight(OSCMessage &) { addLight(); }               // /light
static void routeCap(OSCMessage &)   { addCap(); }                 // /cap
// Absence is silence (ADDRESSES.md): with no LIS3DH there is no /enq/imu in
// the enq bundle and this answers nothing, rather than inventing a -1 that
// claims to be a float.
static void routeImu(OSCMessage &) { if (accelOK) addImu(); }      // /imu

/* -------------------------------------------------------------------- main */

// The boot /enq is very nearly always lost: the board resets, its USB
// device re-enumerates, and the host opens the port some hundreds of
// milliseconds later, by which time setup() has long finished. Measured on
// this repo's ESP32 and SAMD boards -- a probe opening the port straight
// after flashing never once caught it. So /enq is also an INBOUND address
// and the page asks for it on connect.
//
// The answer is the name followed by one /enq/<capability> per thing this
// board can prove it has, with the shape as parameters. The accelerometer
// line is only there when the LIS3DH answered at boot.
static void addEnq() {
  bundleOUT.add("/enq").add("HallowingOscuino");
  bundleOUT.add("/enq/display").add((intOSC_t) TFT_W).add((intOSC_t) TFT_H);
  bundleOUT.add("/enq/buzz");
  bundleOUT.add("/enq/rgb").add((intOSC_t) 1);
  bundleOUT.add("/enq/light");
  bundleOUT.add("/enq/cap").add((intOSC_t) 4);
  if (accelOK) bundleOUT.add("/enq/imu").add((intOSC_t) 3);
}

static void routeEnq(OSCMessage &) { addEnq(); }

void setup() {
  SLIPSerial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TFT_LITE, OUTPUT);
  analogWrite(TFT_LITE, 255);           // still 8-bit here; resolution(10) is below

  analogWriteResolution(10);            // the DAC is 10-bit
  analogWrite(SPKR_PIN, 512);           // idle at mid-scale so it does not click

  tft.initR(INITR_144GREENTAB);         // 128x128 panel on this board
  tft.setRotation(2);
  redraw(ST77XX_BLACK);

  pixel.begin(); pixel.setBrightness(40); pixel.show();

  for (int i = 0; i < 4; i++) touch[i].begin();

  accelOK = lis.begin(0x18) || lis.begin(0x19);   // Adafruit does not state the
  if (accelOK) lis.setRange(LIS3DH_RANGE_4_G);    // address; try both

  addEnq(); flushOut();               // nearly always lost; the page asks again
}

// Non-blocking receive, the extras/webserial/template.ino pattern. Two rules,
// both learned the hard way there: endofPacket() must be called BEFORE
// available() on every pass (available() drives the SLIP state machine and can
// eat a packet boundary if it runs first), and the pump must RETURN rather
// than block when the buffer runs dry -- an unplug mid-frame, or one lost
// byte, would otherwise wedge loop() forever, taking the outbound reports
// with it. The message is filled across several loop() passes, which is why
// it lives at file scope instead of inside loop().
static OSCMessage inMsg;

static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;              // nothing buffered -- try later
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) inMsg.fill((uint8_t) c);    // read() returns -1 on SLIP error
    }
  }
  return true;
}

void loop() {
  static uint32_t last = 0;

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/display/text", routeText);
      inMsg.dispatch("/display/fill", routeFill);
      inMsg.dispatch("/display/bl",   routeBacklight);
      inMsg.dispatch("/buzz",         routeBuzz);
      inMsg.dispatch("/rgb",          routeRgbAll);
      inMsg.dispatch("/rgb/0",        routeRgb0);
      inMsg.dispatch("/rgb/bright",   routeRgbBright);
      inMsg.dispatch("/s/l",          routeLed);
      inMsg.dispatch("/rate",         routeRate);
      inMsg.dispatch("/light",        routeLight);
      inMsg.dispatch("/cap",          routeCap);
      inMsg.dispatch("/imu",          routeImu);
      inMsg.dispatch("/enq",        routeEnq);
    }
    inMsg.empty();
    flushOut();
  }

  const uint32_t now = millis();
  if (!reportEvery || now - last < reportEvery) return;
  last = now;

  // One bundle, sampled in one pass, so every reading in it belongs to the
  // same instant: /state first, then one message per capability streamed.
  bundleOUT.add("/state").add((intOSC_t) seq++).add((intOSC_t) now);
  addLight();
  addCap();
  if (accelOK) addImu();
  flushOut();
}

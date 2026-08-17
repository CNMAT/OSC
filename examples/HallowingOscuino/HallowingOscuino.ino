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
// Pin numbers are from Adafruit's own pinout page for this board, not
// guessed: TFT reset 37, DC 38, CS 39, backlight 7; light sensor A1;
// LIS3DH on I2C; speaker on A0, which is the DAC; four capacitive pads on
// A2..A5; NeoPixel D8; red LED D13.

#include <OSCMessage.h>
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
static uint32_t reportEvery = 60;
static char     line1[24] = "HallowingOscuino";
static char     line2[24] = "waiting for OSC";

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

static void routeText(OSCMessage &m) {          // /screen/text "a" "b"
  if (m.isString(0)) m.getString(0, line1, sizeof line1);
  if (m.size() > 1 && m.isString(1)) m.getString(1, line2, sizeof line2);
  redraw(ST77XX_BLACK);
}
static void routeFill(OSCMessage &m) {          // /screen/fill r g b
  if (m.size() < 3) return;
  const uint8_t r = constrain(m.getInt(0), 0, 255);
  const uint8_t g = constrain(m.getInt(1), 0, 255);
  const uint8_t b = constrain(m.getInt(2), 0, 255);
  redraw(tft.color565(r, g, b));
}
static void routeBacklight(OSCMessage &m) {     // /screen/backlight 0..255
  if (m.size() >= 1 && m.isInt(0)) {
    // analogWriteResolution(10) in setup() is GLOBAL on SAMD, not per-pin:
    // it rescales this PWM write too, so an unscaled 0-255 here topped out
    // at 255/1023 -- about a quarter of full brightness. Map to 10 bits.
    const int v = constrain(m.getInt(0), 0, 255);
    analogWrite(TFT_LITE, (v << 2) | (v >> 6));   // 255 -> 1023
  }
}
static void routeTone(OSCMessage &m) {          // /tone freq [ms] [vol]
  if (m.size() < 1 || !m.isInt(0)) return;
  const uint16_t f  = constrain(m.getInt(0), 0, 8000);
  const uint16_t ms = (m.size() > 1 && m.isInt(1)) ? constrain(m.getInt(1), 0, 2000) : 150;
  const uint8_t  v  = (m.size() > 2 && m.isInt(2)) ? constrain(m.getInt(2), 0, 255) : 120;
  beep(f, ms, v);
}
static void routePixel(OSCMessage &m) {         // /pixel r g b
  if (m.size() < 3) return;
  pixel.setPixelColor(0, pixel.Color(constrain(m.getInt(0), 0, 255),
                                     constrain(m.getInt(1), 0, 255),
                                     constrain(m.getInt(2), 0, 255)));
  pixel.show();
}
static void routeLed(OSCMessage &m) {           // /led 0|1
  if (m.size() >= 1 && m.isInt(0)) digitalWrite(LED_BUILTIN, m.getInt(0) ? HIGH : LOW);
}
static void routeRate(OSCMessage &m) {          // /rate <ms>
  if (m.size() >= 1 && m.isInt(0)) reportEvery = constrain(m.getInt(0), 20, 5000);
}

/* -------------------------------------------------------------------- main */

// The boot /hello is very nearly always lost: the board resets, its USB
// device re-enumerates, and the host opens the port some hundreds of
// milliseconds later, by which time setup() has long finished. Measured on
// this repo's ESP32 and SAMD boards -- a probe opening the port straight
// after flashing never once caught it. So /hello is also an INBOUND address
// and the page asks for it on connect.
static void sendHello() {
  OSCMessage hello("/hello");
  hello.add("HallowingOscuino").add(accelOK);
  SLIPSerial.beginPacket(); hello.send(SLIPSerial); SLIPSerial.endPacket();
}

static void routeHello(OSCMessage &) { sendHello(); }

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

  sendHello();          // nearly always lost; the page asks again
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
      inMsg.dispatch("/screen/text",      routeText);
      inMsg.dispatch("/screen/fill",      routeFill);
      inMsg.dispatch("/screen/backlight", routeBacklight);
      inMsg.dispatch("/tone",             routeTone);
      inMsg.dispatch("/pixel",            routePixel);
      inMsg.dispatch("/led",              routeLed);
      inMsg.dispatch("/rate",             routeRate);
      inMsg.dispatch("/hello",       routeHello);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (now - last < reportEvery) return;
  last = now;

  // One message, sampled in one pass, so every reading in it belongs to the
  // same instant -- the same shape as EsploraOscuino and XiaoS3SenseOscuino.
  int16_t ax = 0, ay = 0, az = 0;
  if (accelOK) { lis.read(); ax = lis.x; ay = lis.y; az = lis.z; }

  OSCMessage m("/hallowing");
  m.add((intOSC_t) analogRead(LIGHT_PIN));
  for (int i = 0; i < 4; i++) m.add((intOSC_t) touch[i].measure());
  m.add((intOSC_t) ax).add((intOSC_t) ay).add((intOSC_t) az);
  m.add(accelOK);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

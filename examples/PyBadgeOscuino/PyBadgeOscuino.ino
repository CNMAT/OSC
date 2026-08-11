// Adafruit PyBadge M4 Express — the whole badge over OSC.
//
//   arduino-cli compile -b adafruit:samd:adafruit_pybadge_m4 \
//       --upload --port /dev/cu.usbmodemXXXX examples/PyBadgeOscuino
//
// Needs only: Adafruit GFX, Adafruit ST7735 and ST7789, Adafruit NeoPixel,
// Adafruit LIS3DH. No Arcada — the one thing that library really knows is
// how the eight buttons are wired, and those three pins are below, so there
// is no reason to drag in its dependency tree for a plain Arduino sketch.
//
// Pin numbers taken from Adafruit's own board definition
// (Adafruit_Arcada_Library/Boards/Adafruit_Arcada_PyBadge.h), not guessed:
// TFT CS 44, DC 45, RST 46, backlight 47, 160x128 landscape; speaker on A0
// with its amplifier enable on 51; five NeoPixels on 8; light sensor A7;
// battery divider A6; buttons on a 74HC165 shift register, clock 48, data 49,
// latch 50.
//
// Open PyBadgeOscuino.html to drive it. Chrome or Edge, served over
// http://localhost — Web Serial does not work from a file:// URL.

#include <OSCMessage.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LIS3DH.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// PDM microphone. The EdgeBadge (Adafruit's TensorFlow Lite badge) is a PyBadge
// with a PDM MEMS mic added; a plain PyBadge or PyBadge LC has none.
//
// Both are safe. Leave this at 1 on either board: if no mic answers, begin()
// or configure() fails, micOK stays false, no /mic is ever sent, and the page
// hides its microphone panel. Set it to 0 to drop the code entirely and save
// the flash on a board you know has no mic.
//
// Pins are the variant's own "SPI for PDM mic" block (SERCOM3): clock SCK2 = 5,
// data MISO2 = 6.
//
// KNOWN LIMITATION, measured on an EdgeBadge: Adafruit_ZeroPDM does not work
// on this board and micOK comes back false. That library drives PDM through
// the SAMD's I2S peripheral -- its begin() returns false unless the variant
// defines I2S pin macros such as PIN_PA10G_I2S_SCK0 -- and pybadge_m4's
// variant.h declares I2S_INTERFACES_COUNT 0. The EdgeBadge instead clocks the
// mic through a SERCOM in SPI mode, which is what that "SPI for PDM mic"
// comment is telling us, and wants Adafruit_ZeroPDMSPI. That library is not in
// the Library Manager index, so it is not used here.
//
// The board is unharmed by this: begin() fails, micOK stays false, no /mic is
// sent and the page hides its panel -- the same path a plain PyBadge with no
// microphone takes. Verified on hardware: 60 /pybadge frames in 3 s, no /mic.
#ifndef BADGE_HAS_PDM_MIC
#define BADGE_HAS_PDM_MIC 1
#endif

#if BADGE_HAS_PDM_MIC
#include <Adafruit_ZeroPDM.h>
#define PDM_CLK_PIN 5
#define PDM_DATA_PIN 6
static Adafruit_ZeroPDM pdm(PDM_CLK_PIN, PDM_DATA_PIN);
static bool micOK = false;
#define SCOPE_POINTS 96
static uint32_t pdmbuf[64];
#else
static const bool micOK = false;
#endif

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define TFT_CS        44
#define TFT_DC        45
#define TFT_RST       46
#define TFT_LITE      47
#define SPKR_ENABLE   51
#define AUDIO_OUT     A0
#define NEOPIX_PIN     8
#define NEOPIX_NUM     5
#define LIGHT_SENSOR  A7
#define BATTERY_SENSE A6
#define BTN_CLOCK     48
#define BTN_DATA      49
#define BTN_LATCH     50

// Bit positions in the shift register byte, in the order the page expects.
// left 0x01, up 0x02, down 0x04, right 0x08, select 0x10, start 0x20,
// A 0x40, B 0x80.

// The PyBadge's display is on SPI1 -- a second SERCOM on pins 41/42/43, per
// the variant header's "Internal SPI for TFT" block. Constructing the driver
// without naming the bus uses the default SPI, and the screen stays blank
// with no error: the pins are simply not connected to anything.
Adafruit_ST7735  tft = Adafruit_ST7735(&SPI1, TFT_CS, TFT_DC, TFT_RST);
Adafruit_NeoPixel pixels(NEOPIX_NUM, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_LIS3DH   lis;

static bool     accelOK = false;
static uint32_t reportEvery = 50;
static char     line1[26] = "PyBadgeOscuino";
static char     line2[26] = "waiting for OSC";
static uint16_t bgColour = ST77XX_BLACK;

/* ------------------------------------------------------- raw OSC helpers */

// The scope is a blob; writing it straight to the wire avoids OSCMessage
// mallocing and copying it every frame.
static void writePadded(Print &p, const char *str) {
  size_t n = strlen(str) + 1;
  p.write((const uint8_t *) str, n);
  while (n & 3) { p.write((uint8_t) 0); n++; }
}
static void writeBE32(Print &p, uint32_t v) {
  const uint8_t b[4] = { (uint8_t)(v>>24), (uint8_t)(v>>16), (uint8_t)(v>>8), (uint8_t)v };
  p.write(b, 4);
}

/* ----------------------------------------------------------------- buttons */

// One 74HC165: pulse the latch to sample all eight inputs, then clock them
// out MSB first.
//
// Do NOT use Arduino's shiftIn() here. It drives the clock HIGH and *then*
// samples, whereas this register presents its first bit as soon as the latch
// rises and advances on the clock edge -- so sampling after the edge reads
// every bit one position late and rotates the whole byte. Every button then
// appears as its neighbour. Read first, clock afterwards, which is what
// Adafruit's own Arcada driver does.
static uint8_t readButtons() {
  digitalWrite(BTN_LATCH, LOW);
  delayMicroseconds(1);
  digitalWrite(BTN_LATCH, HIGH);
  delayMicroseconds(1);

  uint8_t bits = 0;
  for (int i = 0; i < 8; i++) {
    bits <<= 1;
    bits |= digitalRead(BTN_DATA);      // sample BEFORE the clock edge
    digitalWrite(BTN_CLOCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(BTN_CLOCK, LOW);
    delayMicroseconds(1);
  }
  return bits;
}

/* ------------------------------------------------------------------ screen */

static void redraw() {
  tft.fillScreen(bgColour);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 8);   tft.print(line1);
  tft.setTextSize(2);
  tft.setCursor(5, 30);  tft.print(line2);
}

/* ----------------------------------------------------------------- speaker */

// Audio is the DAC on A0 behind a class-D amp whose enable is pin 51. tone()
// does not drive a DAC pin on SAMD and analogWrite() gives one DC level, so
// the square wave is generated here from two DAC levels. Blocking on purpose:
// the notes are short and it keeps the example readable.
static void beep(uint16_t freq, uint16_t ms, uint8_t vol) {
  if (!freq || !ms) return;
  digitalWrite(SPKR_ENABLE, HIGH);
  const uint32_t halfUs = 500000UL / freq;
  const uint32_t cycles = ((uint32_t) ms * 1000UL) / (halfUs * 2);
  const int hi = 512 + (vol * 2), lo = 512 - (vol * 2);
  for (uint32_t i = 0; i < cycles; i++) {
    analogWrite(AUDIO_OUT, hi); delayMicroseconds(halfUs);
    analogWrite(AUDIO_OUT, lo); delayMicroseconds(halfUs);
  }
  analogWrite(AUDIO_OUT, 512);            // rest at mid-scale, not at 0
  digitalWrite(SPKR_ENABLE, LOW);         // quiet the amp between notes
}

#if BADGE_HAS_PDM_MIC
/* --------------------------------------------------------------------- mic */

// /mic ,iiib  rms, peak, sampleRate, scope
// The PDM stream is 1-bit at a high rate; each 32-bit word read is 32 raw
// samples. Counting set bits per word is a crude but honest low-pass: a loud
// sound skews the density away from half-full, so |popcount - 16| tracks
// amplitude without needing a real decimation filter on an M4.
static void sendMic() {
  // Adafruit_ZeroPDM declares read(uint32_t*, int) but only ever defines the
  // single-word read(), so calling the buffered one fails at link:
  //   undefined reference to Adafruit_ZeroPDM::read(unsigned long*, int)
  for (int i = 0; i < 64; i++) pdmbuf[i] = pdm.read();

  int8_t  scope[SCOPE_POINTS];
  int32_t sumsq = 0, peak = 0;
  for (int i = 0; i < 64; i++) {
    const int ones = __builtin_popcount(pdmbuf[i]);
    const int v = (ones - 16) * 8;            // centre on zero, scale to +-128
    sumsq += v * v;
    const int a = v < 0 ? -v : v;
    if (a > peak) peak = a;
    if (i < SCOPE_POINTS) scope[i] = (int8_t) constrain(v, -127, 127);
  }
  for (int i = 64; i < SCOPE_POINTS; i++) scope[i] = 0;
  const int32_t rms = (int32_t) sqrtf((float) sumsq / 64.0f);

  SLIPSerial.beginPacket();
  writePadded(SLIPSerial, "/mic");
  writePadded(SLIPSerial, ",iiib");
  writeBE32(SLIPSerial, (uint32_t) rms);
  writeBE32(SLIPSerial, (uint32_t) peak);
  writeBE32(SLIPSerial, 16000);
  writeBE32(SLIPSerial, SCOPE_POINTS);
  SLIPSerial.write((const uint8_t *) scope, SCOPE_POINTS);
  SLIPSerial.endPacket();
}
#endif

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {              // /screen/text "a" "b"
  if (m.isString(0)) m.getString(0, line1, sizeof line1);
  if (m.size() > 1 && m.isString(1)) m.getString(1, line2, sizeof line2);
  redraw();
}
static void routeFill(OSCMessage &m) {              // /screen/fill r g b
  if (m.size() < 3) return;
  bgColour = tft.color565(constrain(m.getInt(0), 0, 255),
                          constrain(m.getInt(1), 0, 255),
                          constrain(m.getInt(2), 0, 255));
  redraw();
}
static void routeBacklight(OSCMessage &m) {         // /screen/backlight 0..255
  if (m.size() >= 1 && m.isInt(0)) analogWrite(TFT_LITE, constrain(m.getInt(0), 0, 255));
}
static void routeBox(OSCMessage &m) {               // /screen/box x y w h r g b
  if (m.size() < 7) return;
  tft.fillRect(m.getInt(0), m.getInt(1), m.getInt(2), m.getInt(3),
               tft.color565(constrain(m.getInt(4), 0, 255),
                            constrain(m.getInt(5), 0, 255),
                            constrain(m.getInt(6), 0, 255)));
}
static void routeTone(OSCMessage &m) {              // /tone freq [ms] [vol]
  if (m.size() < 1 || !m.isInt(0)) return;
  beep(constrain(m.getInt(0), 0, 8000),
       (m.size() > 1 && m.isInt(1)) ? constrain(m.getInt(1), 0, 3000) : 150,
       (m.size() > 2 && m.isInt(2)) ? constrain(m.getInt(2), 0, 255) : 120);
}
static void routePixels(OSCMessage &m) {            // /pixels r g b  (all five)
  if (m.size() < 3) return;
  const uint32_t c = pixels.Color(constrain(m.getInt(0), 0, 255),
                                  constrain(m.getInt(1), 0, 255),
                                  constrain(m.getInt(2), 0, 255));
  for (int i = 0; i < NEOPIX_NUM; i++) pixels.setPixelColor(i, c);
  pixels.show();
}
static void routePixel(OSCMessage &m) {             // /pixel i r g b (just one)
  if (m.size() < 4) return;
  pixels.setPixelColor(constrain(m.getInt(0), 0, NEOPIX_NUM - 1),
                       pixels.Color(constrain(m.getInt(1), 0, 255),
                                    constrain(m.getInt(2), 0, 255),
                                    constrain(m.getInt(3), 0, 255)));
  pixels.show();
}
static void routeLed(OSCMessage &m) {               // /led 0|1
  if (m.size() >= 1 && m.isInt(0)) digitalWrite(LED_BUILTIN, m.getInt(0) ? HIGH : LOW);
}
static void routeRate(OSCMessage &m) {              // /rate <ms>
  if (m.size() >= 1 && m.isInt(0)) reportEvery = constrain(m.getInt(0), 20, 5000);
}

/* -------------------------------------------------------------------- main */

void setup() {
  SLIPSerial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TFT_LITE, OUTPUT);      analogWrite(TFT_LITE, 255);
  pinMode(SPKR_ENABLE, OUTPUT);   digitalWrite(SPKR_ENABLE, LOW);
  pinMode(BTN_LATCH, OUTPUT);     digitalWrite(BTN_LATCH, HIGH);
  pinMode(BTN_CLOCK, OUTPUT);    digitalWrite(BTN_CLOCK, LOW);
  pinMode(BTN_DATA, INPUT);

  analogWriteResolution(10);      // the DAC is 10-bit
  analogWrite(AUDIO_OUT, 512);    // idle at mid-scale so it does not click

  tft.initR(INITR_BLACKTAB);      // 160x128 panel
  tft.setRotation(1);             // landscape, as Adafruit's own definition has it
  redraw();

  pixels.begin(); pixels.setBrightness(40); pixels.clear(); pixels.show();

#if BADGE_HAS_PDM_MIC
  // Either call failing simply means no mic on this badge; nothing else cares.
  micOK = pdm.begin() && pdm.configure(16000, false);
#endif

  accelOK = lis.begin(0x19) || lis.begin(0x18);
  if (accelOK) lis.setRange(LIS3DH_RANGE_4_G);

  OSCMessage hello("/hello");
  // the page uses micOK to decide whether to show the microphone panel
  hello.add("PyBadgeOscuino").add((intOSC_t) NEOPIX_NUM).add(accelOK).add(micOK);
  SLIPSerial.beginPacket(); hello.send(SLIPSerial); SLIPSerial.endPacket();
}

void loop() {
  static uint32_t last = 0;

  if (SLIPSerial.available()) {
    OSCMessage in;
    unsigned long lastByte = millis();
    while (!SLIPSerial.endofPacket()) {
      if (SLIPSerial.available()) {
        int c = SLIPSerial.read();      // int, -1 for "no byte"
        if (c >= 0) in.fill((uint8_t) c);
        lastByte = millis();
      } else if (millis() - lastByte > 200) break;
    }
    if (!in.hasError()) {
      in.dispatch("/screen/text",      routeText);
      in.dispatch("/screen/fill",      routeFill);
      in.dispatch("/screen/backlight", routeBacklight);
      in.dispatch("/screen/box",       routeBox);
      in.dispatch("/tone",             routeTone);
      in.dispatch("/pixels",           routePixels);
      in.dispatch("/pixel",            routePixel);
      in.dispatch("/led",              routeLed);
      in.dispatch("/rate",             routeRate);
    }
  }

#if BADGE_HAS_PDM_MIC
  static uint32_t lastMic = 0;
  if (micOK && millis() - lastMic >= 50) { lastMic = millis(); sendMic(); }
#endif

  const uint32_t now = millis();
  if (now - last < reportEvery) return;
  last = now;

  int16_t ax = 0, ay = 0, az = 0;
  if (accelOK) { lis.read(); ax = lis.x; ay = lis.y; az = lis.z; }

  // One message, sampled in one pass, so every reading in it belongs to the
  // same instant -- the same shape as EsploraOscuino and XiaoS3SenseOscuino.
  OSCMessage m("/pybadge");
  m.add((intOSC_t) readButtons());                     // one bitmask, eight buttons
  m.add((intOSC_t) analogRead(LIGHT_SENSOR));
  m.add((intOSC_t) analogRead(BATTERY_SENSE));         // half the battery volts
  m.add((intOSC_t) ax).add((intOSC_t) ay).add((intOSC_t) az);
  m.add(accelOK);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

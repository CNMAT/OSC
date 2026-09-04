// Robotistan Pico Bricks (V2 / v2.1): every module over OSC.
//
//   http://localhost/PicoBricksOscuino.html   (Web Serial; not file://)
//
// FQBN: rp2040:rp2040:rpipico. Robotistan's own sketches tell you to pick
// "Raspberry Pi Pico W", but only because their all-in-one test sketch pulls
// in <WiFi.h>; nothing in the library needs it, and plain rpipico is right.
//
// THE SILKSCREEN LIES ABOUT THE OLED. The board is printed "SDA-GP2 SCL-GP3",
// and Robotistan's own teacher handbook reproduces that, but it is wrong:
// the display is on GP4/GP5. Their pinout diagram is labelled with the Pico's
// PHYSICAL pin numbers 1-40 rather than GP numbers -- physical 6 and 7 are
// GP4 and GP5 -- which is where the error comes from. Every one of their
// MicroPython examples uses I2C(0, scl=Pin(5), sda=Pin(4)), and their Arduino
// code calls a bare Wire.begin(), which on this core defaults to GP4/GP5.
//
// Confirmed here by sweeping the buses on the actual board: GP4/GP5 answered
// with 0x3C, 0x70, 0x22 and 0x01, and GP2/GP3 answered with nothing at all.
//
// THAT SWEEP ALSO SETTLED THE BOARD REVISION, which the photograph did not.
// A blue temperature module looks like the V1's DHT11, but 0x70 is an SHTC3
// and 0x22 is the motor driver as an I2C slave, and both are V2-only: on a V1
// the sensor is a DHT11 on GP11 and the motors are direct on GP21/GP22. So
// this is the V2 pin map, and GP11/GP21/GP22 are free.
//
// Pins are Robotistan's V2 Picobricks.ino defines, verbatim.
//
// GP0 IS DOUBLE-BOOKED: the IR receiver's output and Serial1's TX to the
// ESP-01/Bluetooth socket are the same pin, both on the IoT module. Use one or
// the other, never both. This sketch reads it as the IR input and does not
// touch Serial1.
//
// The address space is ADDRESSES.md: capabilities, not a board prefix.
// Everything the board says goes out as a bundle.
//
// Outbound
//   /enq ,s name             then one /enq/<capability> per module that is
//                              actually present, with its shape:
//                              /enq/display 128 64   only if the OLED answered
//                                                    at 0x3C
//                              /enq/rgb 1, /enq/buzz, /enq/btn 1, /enq/pot 1,
//                              /enq/light, /enq/relay 1
//                              /enq/temp             only if the SHTC3 answered
//                                                    at 0x70
//                              /enq/motor 2, /enq/servo 4   only if the driver
//                                                    answered at 0x22
//   The stream, every /rate ms, is one bundle sampled in one pass so the
//   values share an instant:
//   /state ,ii  seq millis     the sequence counter makes drops visible
//   /btn ,i     1 = pressed
//   /pot ,i     0-1023
//   /light ,i   the LDR, 0-1023
//   /relay/0 ,i what the relay is actually doing, so a page cannot drift
//               from it
//   /temp ,f    degC from the SHTC3. The same conversion also yields
//               relative humidity, but the contract has no humidity address,
//               so that reading is not sent.
//   Every write below is echoed on its own address, and /btn, /pot, /light,
//   /temp and /relay/0 answer on the same address when asked with no
//   argument.
// Inbound
//   /s/l ,i 0|1                the red LED on GP7
//   /rgb ,iii r g b            the single WS2812 on GP6; /rgb/0 and a
//                              one-triple /rgb/pixels mean the same pixel, and
//                              each is echoed on the address it arrived on
//   /rgb/bright ,i 0..255      scales the colour in software (the WS2812 has
//                              no brightness register); echoed
//   /buzz ,ii hz ms            passive buzzer on GP20; hz 0 stops
//   /relay/0 ,i 0|1            the relay on GP12 -- it CLICKS and switches
//                              mains-rated contacts; that is why it is not
//                              pulsed here
//   /display/text ,s...        up to four lines; answered with the number of
//                              lines drawn
//   /motor/<n> ,ii speed dir   n 0-1 on the wire (the driver counts 1-2),
//                              speed 0-255, dir 0|1
//   /servo/<n> ,i angle        n 0-3 on the wire (the driver counts 1-4),
//                              angle 0-180
//   /rate ,i ms                stream period, 20..2000; 0 stops it
//   /enq                     ask again: the boot one is lost to USB
//                              re-enumeration before the host opens the port
//
// STATUS: verified on the board when first added (commit f332fb1,
// 2026-08-14): every module answered over one stream -- OLED 0x3C, SHTC3
// 0x70 reading 25.0 C / 51.1 % RH, I2C motor driver 0x22, WS2812, LED,
// button, relay, buzzer, pot and LDR tracking. The audit pass of 2026-08-17
// (the two SHTC3 reads alternated on a 500 ms cadence) recompiled and
// recorded no re-run. Addresses renamed onto ADDRESSES.md on 2026-09-03
// (/pb/led -> /s/l, /pb/rgb -> /rgb and /rgb/0, /pb/buzz -> /buzz,
// /pb/relay -> /relay/0, /pb/oled -> /display/text, /pb/motor <n> ->
// /motor/<n>, /pb/servo <n> -> /servo/<n>, /pb/rate -> /rate, the /pb blob
// -> /state + /btn + /pot + /light + /relay/0 + /temp with the blob's
// humidity dropped because the contract has no address for it, /enq <name>
// <bool>... -> /enq <name> + /enq/..., and /rgb/bright added as the
// contract's rgb row lists it); that build is compile-checked and has not
// been re-run on the board.
#include <Wire.h>
#include <picobricks.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

// ---- Robotistan's V2 defines ----------------------------------------------
#define IR_PIN      0
#define RGB_PIN     6
#define LED_PIN     7
#define BUTTON_PIN 10
#define RELAY_PIN  12
#define BUZZER_PIN 20
#define POT_PIN    26      // ADC0
#define LDR_PIN    27      // ADC1
#define RGB_COUNT   1
#define ADDR_OLED  0x3C
#define ADDR_SHTC  0x70
#define ADDR_MOTOR 0x22
#define PIN_SDA     4      // scanned, not taken from the silkscreen
#define PIN_SCL     5

#define OLED_W      128    // /enq/display shape, in pixels
#define OLED_H       64
#define TEXT_LINES    4    // what /display/text can show at this font
#define MOTOR_COUNT   2    // the driver's DC outputs, /motor/0 and /motor/1
#define SERVO_COUNT   4    // its servo headers, /servo/0 .. /servo/3

SSD1306     oled(ADDR_OLED, OLED_W, OLED_H);
NeoPixel    strip(RGB_PIN, RGB_COUNT);
SHTC3       shtc(ADDR_SHTC);
motorDriver motor;

static bool oledOK = false, shtOK = false, motorOK = false;
static int32_t  seq = 0;
static uint32_t reportMs = 50, buzzUntil = 0;
static int32_t  relayState = 0;
static char lines[TEXT_LINES][22] = { "PicoBricks", "OSC over USB", "", "" };
static float    tempC = 0;                // the SHTC3 cache, see loop()
static uint8_t  rgbR = 0, rgbG = 0, rgbB = 0, rgbBright = 255;

static bool present(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static void redraw() {
  if (!oledOK) return;
  oled.clear();
  for (uint8_t i = 0; i < TEXT_LINES; i++) {
    oled.setCursor(0, (uint8_t)(i * 12));
    oled.print(lines[i]);
  }
  oled.show();
}

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

// Pressed == HIGH: see the pinMode note in setup().
static void addBtn()   { bundleOUT.add("/btn").add((intOSC_t) (digitalRead(BUTTON_PIN) == HIGH ? 1 : 0)); }
static void addPot()   { bundleOUT.add("/pot").add((intOSC_t) analogRead(POT_PIN)); }
static void addLight() { bundleOUT.add("/light").add((intOSC_t) analogRead(LDR_PIN)); }
static void addRelay() { bundleOUT.add("/relay/0").add((intOSC_t) relayState); }
static void addTemp()  { if (shtOK) bundleOUT.add("/temp").add(tempC); }

// The answer is the name followed by one /enq/<capability> per module this
// board can prove it has, with the shape as parameters. The I2C modules
// detach, so their lines are only there when the address answered at boot.
static void addEnq() {
  bundleOUT.add("/enq").add("PicoBricksOscuino");
  if (oledOK) bundleOUT.add("/enq/display").add((intOSC_t) OLED_W).add((intOSC_t) OLED_H);
  bundleOUT.add("/enq/rgb").add((intOSC_t) RGB_COUNT);
  bundleOUT.add("/enq/buzz");
  bundleOUT.add("/enq/btn").add((intOSC_t) 1);
  bundleOUT.add("/enq/pot").add((intOSC_t) 1);
  bundleOUT.add("/enq/light");
  bundleOUT.add("/enq/relay").add((intOSC_t) 1);
  if (shtOK) bundleOUT.add("/enq/temp");
  if (motorOK) {
    bundleOUT.add("/enq/motor").add((intOSC_t) MOTOR_COUNT);
    bundleOUT.add("/enq/servo").add((intOSC_t) SERVO_COUNT);
  }
}

/* ----------------------------------------------------------------- inbound */

// The /<n> after a route's prefix as a number, or -1 for anything else.
static int indexAfter(OSCMessage &m, int offset) {
  char rest[8];
  m.getAddress(rest, offset, sizeof rest);
  if (rest[0] != '/' || !isdigit((unsigned char) rest[1])) return -1;
  return atoi(rest + 1);
}

static void routeLed(OSCMessage &m) {                // /s/l 0|1
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t v = m.getInt(0) ? 1 : 0;
  digitalWrite(LED_PIN, v ? HIGH : LOW);
  bundleOUT.add("/s/l").add((intOSC_t) v);
}

// The WS2812 has no brightness register and the PicoBricks driver no
// brightness call, so /rgb/bright is a scale applied here to the last colour
// written; the colour itself is kept unscaled so a later /rgb/bright can
// restore it.
static void showRgb() {
  strip.setPixelColor(0, (uint8_t) (rgbR * rgbBright / 255),
                         (uint8_t) (rgbG * rgbBright / 255),
                         (uint8_t) (rgbB * rgbBright / 255));
  strip.Show();
}

// /rgb r g b, /rgb/0 r g b, /rgb/pixels r g b, /rgb/bright n. One pixel, so
// the first three all mean it; each is echoed on the address it arrived on.
static void routeRgb(OSCMessage &m, int offset) {
  char addr[16];
  m.getAddress(addr, 0, sizeof addr);
  if (m.fullMatch("/bright", offset)) {
    if (m.size() < 1 || !m.isInt(0)) return;
    rgbBright = (uint8_t) constrain(m.getInt(0), 0, 255);
    showRgb();
    bundleOUT.add(addr).add((intOSC_t) rgbBright);
    return;
  }
  const bool bare = m.getAddressLength(offset) == 0;
  if (!bare && indexAfter(m, offset) != 0 && !m.fullMatch("/pixels", offset)) return;
  if (m.size() < 3) return;
  rgbR = m.getInt(0) & 0xFF; rgbG = m.getInt(1) & 0xFF; rgbB = m.getInt(2) & 0xFF;
  showRgb();
  bundleOUT.add(addr).add((intOSC_t) rgbR).add((intOSC_t) rgbG).add((intOSC_t) rgbB);
}

static void routeBuzz(OSCMessage &m) {               // /buzz hz [ms]; 0 stops
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t f = m.getInt(0);
  const int32_t ms = (m.size() > 1 && m.isInt(1)) ? m.getInt(1) : 150;
  if (f <= 0) { noTone(BUZZER_PIN); buzzUntil = 0; }
  else {
    tone(BUZZER_PIN, (unsigned int) f);
    buzzUntil = millis() + (uint32_t) constrain(ms, 10, 5000);
  }
  bundleOUT.add("/buzz").add((intOSC_t) f).add((intOSC_t) ms);
}

static void routeRelay(OSCMessage &m, int offset) {  // /relay/0 [0|1]
  if (indexAfter(m, offset) != 0) return;
  if (m.size() >= 1 && m.isInt(0)) {
    relayState = m.getInt(0) ? 1 : 0;
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  }
  addRelay();                                        // the echo, or the answer
}

static void routeText(OSCMessage &m) {               // /display/text up to four lines
  for (uint8_t i = 0; i < TEXT_LINES; i++) lines[i][0] = '\0';
  const int n = m.size() < TEXT_LINES ? m.size() : TEXT_LINES;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  redraw();
  bundleOUT.add("/display/text").add((intOSC_t) (oledOK ? n : 0));
}

// The wire counts from 0 like every other indexed address in the contract;
// Robotistan's driver counts its outputs from 1.
static void routeMotor(OSCMessage &m, int offset) {  // /motor/<n> speed dir
  const int n = indexAfter(m, offset);
  if (!motorOK || n < 0 || n >= MOTOR_COUNT || m.size() < 2) return;
  const int32_t speed = constrain(m.getInt(0), 0, 255);
  const int32_t dir   = m.getInt(1) ? 1 : 0;
  motor.dc(n + 1, speed, dir);
  char addr[16];
  m.getAddress(addr, 0, sizeof addr);
  bundleOUT.add(addr).add((intOSC_t) speed).add((intOSC_t) dir);
}

static void routeServo(OSCMessage &m, int offset) {  // /servo/<n> angle
  const int n = indexAfter(m, offset);
  if (!motorOK || n < 0 || n >= SERVO_COUNT || m.size() < 1) return;
  const int32_t angle = constrain(m.getInt(0), 0, 180);
  motor.servo(n + 1, angle);
  char addr[16];
  m.getAddress(addr, 0, sizeof addr);
  bundleOUT.add(addr).add((intOSC_t) angle);
}

static void routeRate(OSCMessage &m) {               // /rate <ms>, 0 stops
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t v = m.getInt(0);
  reportMs = v <= 0 ? 0 : constrain(v, 20, 2000);
  bundleOUT.add("/rate").add((intOSC_t) reportMs);
}

static void routeBtn(OSCMessage &)   { addBtn(); }
static void routePot(OSCMessage &)   { addPot(); }
static void routeLight(OSCMessage &) { addLight(); }
static void routeTemp(OSCMessage &)  { addTemp(); }
static void routeEnq(OSCMessage &) { addEnq(); }

void setup() {
  SLIPSerial.begin(115200);

  pinMode(LED_PIN,    OUTPUT);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(IR_PIN,     INPUT);
  // Robotistan's own V2 sketch sets INPUT_PULLUP and then treats HIGH as
  // pressed, which cannot both be right -- so it was measured. With the
  // internal pull-up engaged the pin still read LOW at rest, which is only
  // possible against a stronger external pull-down. The button is therefore
  // ACTIVE HIGH: their ISR is correct and their pinMode is the wrong half.
  // Plain INPUT here, and pressed == HIGH.
  pinMode(BUTTON_PIN, INPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);

  // GP4/GP5, from the bus sweep -- NOT the GP2/GP3 on the silkscreen.
  Wire.setSDA(PIN_SDA); Wire.setSCL(PIN_SCL); Wire.begin();

  strip.Init(RGB_PIN, RGB_COUNT);
  strip.setPixelColor(0, 0, 0, 0); strip.Show();

  // Probe each address before trusting a begin(): the modules detach, and a
  // driver that initialises happily against an empty bus reports nothing
  // wrong while streaming zeros. The probe results are what /enq announces.
  oledOK  = present(ADDR_OLED);
  shtOK   = present(ADDR_SHTC);
  motorOK = present(ADDR_MOTOR);

  if (oledOK) { oled.init(); redraw(); }
  if (shtOK)  shtc.begin();

  addEnq(); flushOut();   // usually lost; the page asks again
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
      inMsg.dispatch("/s/l",          routeLed);
      inMsg.route("/rgb",             routeRgb);      // /rgb, /rgb/0, /rgb/pixels, /rgb/bright
      inMsg.dispatch("/buzz",         routeBuzz);
      inMsg.route("/relay",           routeRelay);    // /relay/0
      inMsg.dispatch("/display/text", routeText);
      inMsg.route("/motor",           routeMotor);    // /motor/<n>
      inMsg.route("/servo",           routeServo);    // /servo/<n>
      inMsg.dispatch("/rate",         routeRate);
      inMsg.dispatch("/btn",          routeBtn);
      inMsg.dispatch("/pot",          routePot);
      inMsg.dispatch("/light",        routeLight);
      inMsg.dispatch("/temp",         routeTemp);
      inMsg.dispatch("/enq",        routeEnq);
    }
    inMsg.empty();
    flushOut();                               // echoes and answers, one bundle
  }

  const uint32_t now = millis();
  if (buzzUntil && now >= buzzUntil) { noTone(BUZZER_PIN); buzzUntil = 0; }
  if (!reportMs || now - last < reportMs) return;
  last = now;

  // Each SHTC3 read runs a full conversion with a blocking delay(100)
  // inside the PicoBricks library, which read on every report would have
  // tripled the 50 ms default period. Room temperature does not move at
  // 20 Hz: convert once a second and reuse the cache between. (Humidity used
  // to alternate with it on this timer; it has no contract address and is
  // no longer read.)
  static uint32_t lastSht = 0;
  if (shtOK && now - lastSht >= 1000) {
    lastSht = now;
    tempC = shtc.readTemperature();
  }

  // One bundle, sampled in one pass, so every reading in it belongs to the
  // same instant: /state first, then one message per capability streamed.
  bundleOUT.add("/state").add((intOSC_t) seq++).add((intOSC_t) now);
  addBtn();
  addPot();
  addLight();
  addRelay();
  addTemp();
  flushOut();
}

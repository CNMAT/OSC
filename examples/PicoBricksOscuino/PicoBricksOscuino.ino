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
// Outbound
//   /hello ,siiii  name, oledOK, shtOK, motorOK, rgbCount
//   /pb ,iiiii ff  seq, button, pot (0-1023), ldr (0-1023), relay,
//                  tempC, humidity (%)
//                  One message, sampled in one pass, so the values share an
//                  instant; the sequence counter makes drops visible.
// Inbound
//   /pb/led ,i 0|1          the red LED on GP7
//   /pb/rgb ,iii r g b      the single WS2812 on GP6
//   /pb/buzz ,ii freq ms    passive buzzer on GP20
//   /pb/relay ,i 0|1        the relay on GP12 -- it CLICKS and switches mains
//                           rated contacts; that is why it is not pulsed here
//   /pb/oled ,s...          up to four lines
//   /pb/motor ,iii n speed dir     n 1-2, speed 0-255, dir 0|1
//   /pb/servo ,ii n angle          n 1-4, angle 0-180
//   /pb/rate ,i ms          report interval, 20..2000
//   /hello                  ask again: the boot one is lost to USB
//                           re-enumeration before the host opens the port
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

SSD1306     oled(ADDR_OLED, 128, 64);
NeoPixel    strip(RGB_PIN, RGB_COUNT);
SHTC3       shtc(ADDR_SHTC);
motorDriver motor;

static bool oledOK = false, shtOK = false, motorOK = false;
static int32_t  seq = 0;
static uint32_t reportMs = 50, buzzUntil = 0;
static int32_t  relayState = 0;
static char lines[4][22] = { "PicoBricks", "OSC over USB", "", "" };

static bool present(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static void redraw() {
  if (!oledOK) return;
  oled.clear();
  for (uint8_t i = 0; i < 4; i++) {
    oled.setCursor(0, (uint8_t)(i * 12));
    oled.print(lines[i]);
  }
  oled.show();
}

/* ----------------------------------------------------------------- inbound */

static void routeLed(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) digitalWrite(LED_PIN, m.getInt(0) ? HIGH : LOW);
}

static void routeRgb(OSCMessage &m) {
  if (m.size() < 3) return;
  strip.setPixelColor(0, m.getInt(0) & 0xFF, m.getInt(1) & 0xFF, m.getInt(2) & 0xFF);
  strip.Show();
}

static void routeBuzz(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t f = m.getInt(0);
  const int32_t ms = (m.size() > 1 && m.isInt(1)) ? m.getInt(1) : 150;
  if (f <= 0) { noTone(BUZZER_PIN); buzzUntil = 0; return; }
  tone(BUZZER_PIN, (unsigned int) f);
  buzzUntil = millis() + (uint32_t) constrain(ms, 10, 5000);
}

static void routeRelay(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  relayState = m.getInt(0) ? 1 : 0;
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
}

static void routeOled(OSCMessage &m) {
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  const int n = m.size() < 4 ? m.size() : 4;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  redraw();
}

static void routeMotor(OSCMessage &m) {
  if (!motorOK || m.size() < 3) return;
  motor.dc(constrain(m.getInt(0), 1, 2),
           constrain(m.getInt(1), 0, 255),
           m.getInt(2) ? 1 : 0);
}

static void routeServo(OSCMessage &m) {
  if (!motorOK || m.size() < 2) return;
  motor.servo(constrain(m.getInt(0), 1, 4), constrain(m.getInt(1), 0, 180));
}

static void routeRate(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) reportMs = constrain(m.getInt(0), 20, 2000);
}

static void sendHello() {
  OSCMessage h("/hello");
  h.add("PicoBricksOscuino").add(oledOK).add(shtOK).add(motorOK)
   .add((intOSC_t) RGB_COUNT);
  SLIPSerial.beginPacket(); h.send(SLIPSerial); SLIPSerial.endPacket();
}
static void routeHello(OSCMessage &) { sendHello(); }

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
  // wrong while streaming zeros.
  oledOK  = present(ADDR_OLED);
  shtOK   = present(ADDR_SHTC);
  motorOK = present(ADDR_MOTOR);

  if (oledOK) { oled.init(); redraw(); }
  if (shtOK)  shtc.begin();

  sendHello();          // usually lost; the page asks again
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
      inMsg.dispatch("/pb/led",   routeLed);
      inMsg.dispatch("/pb/rgb",   routeRgb);
      inMsg.dispatch("/pb/buzz",  routeBuzz);
      inMsg.dispatch("/pb/relay", routeRelay);
      inMsg.dispatch("/pb/oled",  routeOled);
      inMsg.dispatch("/pb/motor", routeMotor);
      inMsg.dispatch("/pb/servo", routeServo);
      inMsg.dispatch("/pb/rate",  routeRate);
      inMsg.dispatch("/hello",    routeHello);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (buzzUntil && now >= buzzUntil) { noTone(BUZZER_PIN); buzzUntil = 0; }
  if (now - last < reportMs) return;
  last = now;

  // Each SHTC3 read runs a full conversion with a blocking delay(100)
  // inside the PicoBricks library, so reading both per report cost 200 ms
  // and silently tripled the 50 ms report period. Room temperature does not
  // move at 20 Hz: sample once a second, alternating the two conversions so
  // no single pass blocks more than ~100 ms, and reuse the cache between.
  static float tempC = 0, humid = 0;
  static uint32_t lastSht = 0;
  static bool whichSht = false;
  if (shtOK && now - lastSht >= 500) {
    lastSht = now;
    if (whichSht) tempC = shtc.readTemperature(); else humid = shtc.readHumidity();
    whichSht = !whichSht;
  }

  OSCMessage m("/pb");
  m.add((intOSC_t) seq++)
   .add((intOSC_t) (digitalRead(BUTTON_PIN) == HIGH ? 1 : 0))
   .add((intOSC_t) analogRead(POT_PIN))
   .add((intOSC_t) analogRead(LDR_PIN))
   .add((intOSC_t) relayState)
   .add(tempC).add(humid);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

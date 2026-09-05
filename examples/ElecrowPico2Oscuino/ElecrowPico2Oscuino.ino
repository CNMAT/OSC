// Elecrow All-in-one Starter Kit for Pico 2: the sensors and actuators over
// OSC. The 2.4" TFT and its touch panel come in a later pass.
//
//   http://localhost/ElecrowPico2Oscuino.html   (Web Serial; not file://)
//
// FQBN: rp2040:rp2040:rpipico2 -- what Elecrow's own lessons select, and
// what the factory firmware was built with (its USB descriptor says "Pico 2").
// The kit is NOT a socketed Pico 2, though: the datasheet's controller table
// puts an RP2350A on Elecrow's mainboard with a W25Q64 flash and an APS6404L
// PSRAM (CS on GP1), and `picotool info -a` in BOOTSEL read the flash as
// 8192K where the spec sheet says 4 MB. The rpipico2 definition assumes 4 MB
// and no PSRAM; both are left as they are. Flash it with the 1200-baud touch
// and `picotool load -x` (BOARDS.md, flashing procedures).
//
// PINS are the datasheet's (All-in-one_Starter_Kit_for_Pico2_Datasheet.pdf,
// V1.0, section 6) cross-checked against Elecrow's factory sketch for
// mainboards V1.1-1.3; this is a v1.2. None had been measured when this was
// written -- see STATUS.
//
//   GP18/19/20  red / green / yellow LEDs     GP10  passive buzzer
//   GP12  relay (with its red indicator)      GP13  servo header
//   GP15  vibration motor                     GP14  capacitive touch pad
//   GP21  hall sensor (digital)               GP11  IR receiver
//   GP22  20 x WS2812, GP23 their power enable (and a physical LED switch)
//   GP9   HC-SR04 trigger, GP8 its echo
//   GP26  MQ-2 gas (A0)   GP27  four buttons on one resistor ladder (A1)
//   GP28  slide pot (A2)  GP29  sound sensor (A3)
//   GP2/GP3  I2C1: DHT20 0x38, BH1750 0x5C, and at 0x6B an ST LSM6DS3TR-C
//            (WHO_AM_I 0x6A) where the datasheet promises an MPU6050 at 0x68.
//            The boot sweep that found it is reported in /diag.
//   GP4/GP5  I2C0 touch panel, GP6/7/16/17 TFT on SPI0, GP0 backlight,
//            GP24/25 touch reset/interrupt -- all untouched here
//
// The address space is ADDRESSES.md: capabilities, not a board prefix.
// Everything the board says goes out as a bundle.
//
// Outbound
//   /enq ,s name              then one /enq/<capability> per module present:
//                              /enq/led, /enq/rgb 20, /enq/buzz, /enq/btn 4,
//                              /enq/pot 1, /enq/mic, /enq/gas, /enq/dist,
//                              /enq/ir, /enq/cap 1, /enq/relay 1,
//                              /enq/servo 1, /enq/motor 1 -- fixed to the
//                              board -- and, only when the chip answered on
//                              I2C1 at boot: /enq/light (BH1750),
//                              /enq/temp + /enq/hum (DHT20), /enq/imu 6
//                              (LSM6DS3TR-C, WHO_AM_I checked).
//   The stream, every /rate ms, one bundle sampled in one pass:
//   /state ,ii  seq millis
//   /btn ,iiii  one per button, 1 = pressed, in ascending order of the
//               ladder's ADC value (which physical button is which is not
//               yet known -- see STATUS)
//   /pot ,i     0..1023            /gas ,i    0..1023 raw, uncalibrated
//   /mic ,ii    rms peak on the contract's 0..32767 scale, from 64 samples
//               of the analog sound module per tick
//   /cap ,i     the touch pad, 1 = touched
//   /relay/0 ,i what the relay is doing, so a page cannot drift from it
//   /light ,i   BH1750 raw counts (lux is raw / 1.2)
//   /temp ,f  /hum ,f   DHT20, converted once a second
//   /imu ,ffffff        LSM6DS3TR-C g x3 then deg/s x3
//   /dist ,f    HC-SR04 centimetres -- only in ticks that got an echo
//   /ir ,i      the raw 32-bit code, once, in the tick after a remote press
// Inbound (every write is echoed on its own address)
//   /s/l ,i 0|1           the red LED; green and yellow are /d/19 and /d/20
//   /rgb ,iii  /rgb/<n> ,iii  /rgb/pixels ,iii...  /rgb/bright ,i
//   /buzz ,i[i] hz ms     0 stops
//   /relay/0 ,i           it CLICKS and switches its terminals
//   /servo/0 ,i angle     0..180
//   /motor/0 ,ii speed dir  the vibration motor: speed 0..255 is PWM, dir
//                         is accepted and ignored, it only goes one way
//   /rate ,i ms           20..2000; 0 stops the stream
//   /d/<pin> [,i|,f] /a/<n> [,i|,f] /s/m /s/d /s/a   the core pin set, so
//                         the hall sensor is /d/21 and the LEDs /d/19, /d/20
//   /state /btn /pot /mic /gas /cap /light /temp /hum /imu /dist /ir /relay/0
//                         asked bare, answer on the same address
//
// STATUS -- RUN ON THE KIT 2026-09-04 (mainboard v1.2, chip id
// 0xec4722e8dbef5ecc), test/hardware/contractprobe.py --actuate --sound
// 45 passed, 0 failed, 2 skipped (ir and diag are passive), begun with the
// stream stopped so the bare asks were really asked; and a stream watched at 50 ms: /light 96-98 raw, /temp 27.2-27.4 C,
// /hum 47.4 %, /dist 70.0-70.7 cm steady against whatever faces the sensor,
// /gas 181-188, /pot 0 then 1020 after the slider was moved, /mic rms 4-6 in
// a quiet room, /imu |g| = 1.017 with z up and the gyro within 3 deg/s of
// zero at rest, /cap 0, /btn 0 0 0 0 at rest. Every echo answered.
// Two things the run found: the IMU is an ST LSM6DS3TR-C at 0x6B (WHO_AM_I
// 0x6A), not the datasheet's MPU6050 at 0x68 -- the boot sweep in /diag is
// what caught it; and sampling the sound module 100 us after the
// rangefinder's 40 kHz ping produced a /mic spike of 15882 rms in silence,
// gone once the samples were taken before the ping (one run each way).
// NOT yet seen: which physical button is which on the ladder, the WS2812
// colour order (the factory sketch says NEO_RGB), the touch pad's and hall
// sensor's polarity, the remote's protocol, and every actuator's motion --
// the relay, servo, vibration motor and buzzer were commanded and echoed,
// not watched. The display and its touch panel are deliberately absent.

#include <Wire.h>
#include <Servo.h>
#include <Adafruit_NeoPixel.h>
#include <IRremote.hpp>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

// ---- the kit's pins, from the datasheet -----------------------------------
#define PIN_LED_RED     18
#define PIN_LED_GREEN   19
#define PIN_LED_YELLOW  20
#define PIN_BUZZER      10
#define PIN_RELAY       12
#define PIN_SERVO       13
#define PIN_VIB         15
#define PIN_TOUCH       14
#define PIN_HALL        21
#define PIN_IR          11
#define PIN_RGB         22
#define PIN_RGB_EN      23
#define PIN_US_TRIG      9
#define PIN_US_ECHO      8
#define PIN_GAS         26      // A0
#define PIN_KEYS        27      // A1: four buttons on one ladder
#define PIN_POT         28      // A2
#define PIN_SOUND       29      // A3
#define PIN_SDA1         2
#define PIN_SCL1         3
#define ADDR_DHT20    0x38
#define ADDR_IMU      0x6A    // SA0 low; the kit straps it high: 0x6B. Both are tried
#define ADDR_BH1750   0x5C

#define RGB_COUNT       20
#define BTN_COUNT        4

// The factory sketch decodes the ladder with +-5 windows around these four
// values (10-bit ADC). Widened to +-25 here; the idle value is not known.
static const int KEY_CENTRE[BTN_COUNT] = { 745, 805, 865, 910 };
static const int KEY_TOL = 25;

Adafruit_NeoPixel strip(RGB_COUNT, PIN_RGB, NEO_RGB + NEO_KHZ800);   // NEO_RGB per the factory sketch
Servo servo;

static bool     lightOK = false, dhtOK = false, imuOK = false;
static char     i2cDiag[128] = "";           // "i2c1: 0x38 0x5c ..." from the boot sweep
static int32_t  seq = 0;
static uint32_t reportMs = 50, buzzUntil = 0;
static int32_t  relayState = 0, motorSpeed = 0, servoAngle = -1;
static float    tempC = 0, humPct = 0;
static uint32_t dhtTriggered = 0;           // millis of the pending DHT20 conversion, 0 = none
static int32_t  lastIr = 0;                 // the last remote code, 0 until one arrives
static bool     irNew = false;
static bool     irSeen = false;

/* ------------------------------------------------------------- HC-SR04 */
// Interrupt-timed so the loop never waits for an echo. TRIG is pulsed for
// 10 us; ECHO goes high for the round trip. Speed of sound: 58 us per cm of
// distance, out and back.
static volatile uint32_t echoRise = 0, echoWidth = 0;
static volatile bool     echoDone = false;

static void echoISR() {
  if (digitalRead(PIN_US_ECHO) == HIGH) echoRise = micros();
  else { echoWidth = micros() - echoRise; echoDone = true; }
}

static float    distCm = -1;                // last good reading
static uint32_t distAt = 0;                 // when it arrived; 0 = never
static uint32_t pingAt = 0;                 // when the last trigger went out

static void pingDistance(uint32_t now) {
  if (echoDone) {
    echoDone = false;
    const float cm = echoWidth / 58.0f;
    if (cm >= 2.0f && cm <= 400.0f) { distCm = cm; distAt = now; }   // the module's own range
    pingAt = 0;
  }
  if (pingAt && now - pingAt < 60) return;    // a ping is out, or too soon after one
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);
  pingAt = now;
}

/* ------------------------------------------------------------- I2C1 parts */
// Three small chips, read directly: fewer libraries to install, and the
// register sequences fit on a page each.

static bool present(TwoWire &w, uint8_t a) {
  w.beginTransmission(a);
  return w.endTransmission() == 0;
}

static bool i2cWrite(uint8_t addr, const uint8_t *b, size_t n) {
  Wire1.beginTransmission(addr);
  Wire1.write(b, n);
  return Wire1.endTransmission() == 0;
}

static int i2cRead(uint8_t addr, uint8_t *b, size_t n) {
  const int got = (int) Wire1.requestFrom(addr, (uint8_t) n);
  for (int i = 0; i < got && i < (int) n; i++) b[i] = (uint8_t) Wire1.read();
  return got;
}

static int i2cReadReg(uint8_t addr, uint8_t reg, uint8_t *b, size_t n) {
  Wire1.beginTransmission(addr);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return 0;
  return i2cRead(addr, b, n);
}

// BH1750: power on, continuous high-resolution mode (1 lx steps, ~120 ms).
static bool bh1750Begin() {
  const uint8_t on = 0x01, cont = 0x10;
  return i2cWrite(ADDR_BH1750, &on, 1) && i2cWrite(ADDR_BH1750, &cont, 1);
}
static bool bh1750Read(int32_t &raw) {
  uint8_t b[2];
  if (i2cRead(ADDR_BH1750, b, 2) != 2) return false;
  raw = ((int32_t) b[0] << 8) | b[1];
  return true;
}

// DHT20 (an AHT20): status 0x71; if not calibrated, init 0xBE 0x08 0x00;
// measure 0xAC 0x33 0x00, then >= 80 ms later read 7 bytes -- status,
// 20 bits humidity, 20 bits temperature.
static bool dht20Begin() {
  uint8_t st;
  if (i2cRead(ADDR_DHT20, &st, 1) != 1) return false;
  if ((st & 0x18) != 0x18) {
    const uint8_t init[3] = { 0xBE, 0x08, 0x00 };
    if (!i2cWrite(ADDR_DHT20, init, 3)) return false;
    delay(10);
  }
  return true;
}
static bool dht20Trigger() {
  const uint8_t m[3] = { 0xAC, 0x33, 0x00 };
  return i2cWrite(ADDR_DHT20, m, 3);
}
static bool dht20Read(float &t, float &h) {
  uint8_t b[7];
  if (i2cRead(ADDR_DHT20, b, 7) != 7 || (b[0] & 0x80)) return false;   // busy
  const uint32_t hr = ((uint32_t) b[1] << 12) | ((uint32_t) b[2] << 4) | (b[3] >> 4);
  const uint32_t tr = (((uint32_t) b[3] & 0x0F) << 16) | ((uint32_t) b[4] << 8) | b[5];
  h = hr * 100.0f / 1048576.0f;
  t = tr * 200.0f / 1048576.0f - 50.0f;
  return true;
}

// ST LSM6DS3 family (LSM6DS3 0x69, LSM6DS3TR-C 0x6A, LSM6DSL 0x6B, LSM6DSO
// 0x6C at WHO_AM_I 0x0F), at 0x6A or 0x6B. CTRL1_XL 0x10 = 0x40: accel
// 104 Hz, +-2 g (0.061 mg/LSB); CTRL2_G 0x11 = 0x40: gyro 104 Hz, +-250 dps
// (8.75 mdps/LSB). The twelve output bytes OUTX_L_G 0x22 .. OUTZ_H_XL 0x2D
// are gyro x y z then accel x y z, little-endian, one read.
static uint8_t imuAddr = 0, imuWho = 0;
static bool imuBegin() {
  for (uint8_t a = ADDR_IMU; a <= ADDR_IMU + 1; a++) {
    uint8_t who = 0;
    if (!present(Wire1, a) || i2cReadReg(a, 0x0F, &who, 1) != 1) continue;
    if (who == 0x69 || who == 0x6A || who == 0x6B || who == 0x6C) {
      imuAddr = a; imuWho = who;
      const uint8_t xl[2] = { 0x10, 0x40 }, g[2] = { 0x11, 0x40 };
      return i2cWrite(a, xl, 2) && i2cWrite(a, g, 2);
    }
  }
  return false;
}
static bool imuRead(float g[3], float dps[3]) {
  uint8_t b[12];
  if (i2cReadReg(imuAddr, 0x22, b, 12) != 12) return false;
  for (int i = 0; i < 3; i++) {
    dps[i] = (int16_t) (b[i * 2] | (b[i * 2 + 1] << 8)) * 0.00875f;
    g[i]   = (int16_t) (b[6 + i * 2] | (b[6 + i * 2 + 1] << 8)) * 0.000061f;
  }
  return true;
}

/* ------------------------------------------------------------- sampling */

static void readButtons(int b[BTN_COUNT]) {
  const int v = analogRead(PIN_KEYS);
  for (int i = 0; i < BTN_COUNT; i++) b[i] = abs(v - KEY_CENTRE[i]) <= KEY_TOL ? 1 : 0;
}

// 64 samples of the sound module's analog output: rms and peak about the
// window's own mean, scaled from the 10-bit ADC to the contract's 0..32767.
// Squares accumulate in a wide type and the division is in float
// (BRINGUP.md, integer DSP lessons).
static void sampleSound(int32_t &rms, int32_t &peak) {
  const int N = 64;
  int v[N];
  long sum = 0;
  for (int i = 0; i < N; i++) { v[i] = analogRead(PIN_SOUND); sum += v[i]; }
  const float mean = sum / (float) N;
  uint64_t sq = 0;
  int pk = 0;
  for (int i = 0; i < N; i++) {
    const int d = (int) (v[i] - mean);
    sq += (uint64_t) ((int64_t) d * d);
    if (abs(d) > pk) pk = abs(d);
  }
  rms  = (int32_t) (sqrtf(sq / (float) N) * 32.0f);
  peak = (int32_t) (pk * 32);
  if (rms > 32767) rms = 32767;
  if (peak > 32767) peak = 32767;
}

/* ---------------------------------------------------------------- outbound */

static OSCBundle bundleOUT;

static void flushOut() {
  if (bundleOUT.size() == 0) return;
  SLIPSerial.beginPacket(); bundleOUT.send(SLIPSerial); SLIPSerial.endPacket();
  bundleOUT.empty();
}

static void addBtn() {
  int b[BTN_COUNT];
  readButtons(b);
  OSCMessage &m = bundleOUT.add("/btn");
  for (int i = 0; i < BTN_COUNT; i++) m.add((intOSC_t) b[i]);
}
static void addPot()   { bundleOUT.add("/pot").add((intOSC_t) analogRead(PIN_POT)); }
static void addGas()   { bundleOUT.add("/gas").add((intOSC_t) analogRead(PIN_GAS)); }
static void addCap()   { bundleOUT.add("/cap").add((intOSC_t) (digitalRead(PIN_TOUCH) == HIGH ? 1 : 0)); }
static void addRelay() { bundleOUT.add("/relay/0").add((intOSC_t) relayState); }
static void addMic() {
  int32_t rms, peak;
  sampleSound(rms, peak);
  bundleOUT.add("/mic").add((intOSC_t) rms).add((intOSC_t) peak);
}
static void addLight() {
  int32_t raw;
  if (lightOK && bh1750Read(raw)) bundleOUT.add("/light").add((intOSC_t) raw);
}
static void addTemp()  { if (dhtOK) bundleOUT.add("/temp").add(tempC); }
static void addHum()   { if (dhtOK) bundleOUT.add("/hum").add(humPct); }
static void addImu() {
  float g[3], dps[3];
  if (!imuOK || !imuRead(g, dps)) return;
  bundleOUT.add("/imu").add(g[0]).add(g[1]).add(g[2]).add(dps[0]).add(dps[1]).add(dps[2]);
}
static void addDist(bool onlyFresh) {
  // Absence is silence: a tick with no echo carries no /dist. Asked bare,
  // the last good reading answers if there has ever been one.
  if (distAt == 0) return;
  if (onlyFresh && millis() - distAt > 200) return;
  bundleOUT.add("/dist").add(distCm);
}
static void addIr()    { if (irSeen) bundleOUT.add("/ir").add((intOSC_t) lastIr); }

static void addEnq() {
  bundleOUT.add("/enq").add("ElecrowPico2Oscuino");
  bundleOUT.add("/enq/diag");
  bundleOUT.add("/diag").add(i2cDiag);
  bundleOUT.add("/enq/led");
  bundleOUT.add("/enq/rgb").add((intOSC_t) RGB_COUNT);
  bundleOUT.add("/enq/buzz");
  bundleOUT.add("/enq/btn").add((intOSC_t) BTN_COUNT);
  bundleOUT.add("/enq/pot").add((intOSC_t) 1);
  bundleOUT.add("/enq/mic");
  bundleOUT.add("/enq/gas");
  bundleOUT.add("/enq/dist");
  bundleOUT.add("/enq/ir");
  bundleOUT.add("/enq/cap").add((intOSC_t) 1);
  bundleOUT.add("/enq/relay").add((intOSC_t) 1);
  bundleOUT.add("/enq/servo").add((intOSC_t) 1);
  bundleOUT.add("/enq/motor").add((intOSC_t) 1);
  if (lightOK) bundleOUT.add("/enq/light");
  if (dhtOK)   { bundleOUT.add("/enq/temp"); bundleOUT.add("/enq/hum"); }
  if (imuOK)   bundleOUT.add("/enq/imu").add((intOSC_t) 6);
}

/* ----------------------------------------------------------------- inbound */

static int indexAfter(OSCMessage &m, int offset) {
  char rest[8];
  m.getAddress(rest, offset, sizeof rest);
  if (rest[0] != '/' || !isdigit((unsigned char) rest[1])) return -1;
  return atoi(rest + 1);
}

// "/12" for 12, into a static buffer.
static char *numToOSCAddress(int pin) {
  static char s[10];
  int i = 9;
  s[i--] = '\0';
  do { s[i--] = "0123456789"[pin % 10]; pin /= 10; } while (pin && i);
  s[i] = '/';
  return &s[i];
}

static void routeLed(OSCMessage &m) {                // /s/l 0|1: the red LED
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t v = m.getInt(0) ? 1 : 0;
  digitalWrite(PIN_LED_RED, v ? HIGH : LOW);
  bundleOUT.add("/s/l").add((intOSC_t) v);
}

// /rgb r g b (all), /rgb/<n> r g b, /rgb/pixels r g b ... (a frame),
// /rgb/bright n. Echoed on the address each arrived on.
static void routeRgb(OSCMessage &m, int offset) {
  char addr[24];
  m.getAddress(addr, 0, sizeof addr);
  if (m.fullMatch("/bright", offset)) {
    if (m.size() < 1 || !m.isInt(0)) return;
    const int32_t b = constrain(m.getInt(0), 0, 255);
    strip.setBrightness((uint8_t) b);
    strip.show();
    bundleOUT.add(addr).add((intOSC_t) b);
    return;
  }
  if (m.fullMatch("/pixels", offset)) {
    const int n = m.size() / 3;
    for (int i = 0; i < n && i < RGB_COUNT; i++)
      strip.setPixelColor(i, m.getInt(i * 3) & 0xFF, m.getInt(i * 3 + 1) & 0xFF, m.getInt(i * 3 + 2) & 0xFF);
    strip.show();
    bundleOUT.add(addr).add((intOSC_t) n);
    return;
  }
  if (m.size() < 3) return;
  const uint8_t r = m.getInt(0) & 0xFF, g = m.getInt(1) & 0xFF, b = m.getInt(2) & 0xFF;
  const int n = indexAfter(m, offset);
  if (m.getAddressLength(offset) == 0) strip.fill(strip.Color(r, g, b));
  else if (n >= 0 && n < RGB_COUNT)    strip.setPixelColor(n, r, g, b);
  else return;
  strip.show();
  bundleOUT.add(addr).add((intOSC_t) r).add((intOSC_t) g).add((intOSC_t) b);
}

static void routeBuzz(OSCMessage &m) {               // /buzz hz [ms]; 0 stops
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t f = m.getInt(0);
  const int32_t ms = (m.size() > 1 && m.isInt(1)) ? m.getInt(1) : 150;
  if (f <= 0) { noTone(PIN_BUZZER); buzzUntil = 0; }
  else {
    tone(PIN_BUZZER, (unsigned int) f);
    buzzUntil = millis() + (uint32_t) constrain(ms, 10, 5000);
  }
  bundleOUT.add("/buzz").add((intOSC_t) f).add((intOSC_t) ms);
}

static void routeRelay(OSCMessage &m, int offset) {  // /relay/0 [0|1]
  if (indexAfter(m, offset) != 0) return;
  if (m.size() >= 1 && m.isInt(0)) {
    relayState = m.getInt(0) ? 1 : 0;
    digitalWrite(PIN_RELAY, relayState ? HIGH : LOW);
  }
  addRelay();
}

static void routeServo(OSCMessage &m, int offset) {  // /servo/0 angle
  if (indexAfter(m, offset) != 0 || m.size() < 1 || !m.isInt(0)) return;
  servoAngle = constrain(m.getInt(0), 0, 180);
  if (!servo.attached()) servo.attach(PIN_SERVO);
  servo.write((int) servoAngle);
  bundleOUT.add("/servo/0").add((intOSC_t) servoAngle);
}

static void routeMotor(OSCMessage &m, int offset) {  // /motor/0 speed dir
  if (indexAfter(m, offset) != 0 || m.size() < 1 || !m.isInt(0)) return;
  motorSpeed = constrain(m.getInt(0), 0, 255);
  const int32_t dir = (m.size() > 1 && m.isInt(1)) ? (m.getInt(1) ? 1 : 0) : 0;
  analogWrite(PIN_VIB, (int) motorSpeed);
  bundleOUT.add("/motor/0").add((intOSC_t) motorSpeed).add((intOSC_t) dir);
}

static void routeRate(OSCMessage &m) {               // /rate ms; 0 stops
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t v = m.getInt(0);
  reportMs = v <= 0 ? 0 : constrain(v, 20, 2000);
  bundleOUT.add("/rate").add((intOSC_t) reportMs);
}

// The core pin set. /d/<pin>: int = level, float 0..1 = PWM, bare = read,
// /u = read with pull-up. Pins are GP numbers.
static void routeDigital(OSCMessage &m, int offset) {
  const int pin = indexAfter(m, offset);
  if (pin < 0 || pin >= NUM_DIGITAL_PINS) return;
  const int matched = m.match(numToOSCAddress(pin), offset);
  char addr[16];
  strcpy(addr, "/d"); strcat(addr, numToOSCAddress(pin));
  if (m.isInt(0)) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, m.getInt(0) > 0 ? HIGH : LOW);
    bundleOUT.add(addr).add((intOSC_t) (m.getInt(0) > 0 ? 1 : 0));
  } else if (m.isFloat(0)) {
    const float v = constrain(m.getFloat(0), 0.0f, 1.0f);
    pinMode(pin, OUTPUT);
    analogWrite(pin, (int) (v * 255.0f + 0.5f));
    bundleOUT.add(addr).add(v);
  } else if (m.fullMatch("/u", offset + matched)) {
    pinMode(pin, INPUT_PULLUP);
    strcat(addr, "/u");
    bundleOUT.add(addr).add((intOSC_t) digitalRead(pin));
  } else {
    pinMode(pin, INPUT);
    bundleOUT.add(addr).add((intOSC_t) digitalRead(pin));
  }
}

// /a/<n>: n is 0..3 for A0..A3 (GP26..GP29); the GP number is accepted too.
static void routeAnalog(OSCMessage &m, int offset) {
  int n = indexAfter(m, offset);
  if (n < 0) return;
  const int pin = n < NUM_ANALOG_INPUTS ? A0 + n : n;
  if (pin < A0 || pin >= A0 + NUM_ANALOG_INPUTS) return;
  char addr[16];
  strcpy(addr, "/a"); strcat(addr, numToOSCAddress(n));
  if (m.isInt(0)) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, m.getInt(0) > 0 ? HIGH : LOW);
    bundleOUT.add(addr).add((intOSC_t) (m.getInt(0) > 0 ? 1 : 0));
  } else if (m.isFloat(0)) {
    const float v = constrain(m.getFloat(0), 0.0f, 1.0f);
    pinMode(pin, OUTPUT);
    analogWrite(pin, (int) (v * 255.0f + 0.5f));
    bundleOUT.add(addr).add(v);
  } else {
    bundleOUT.add(addr).add((intOSC_t) analogRead(pin));
  }
}

static void routeMicros(OSCMessage &)  { bundleOUT.add("/s/m").add((intOSC_t) micros()); }
static void routeDcount(OSCMessage &)  { bundleOUT.add("/s/d").add((intOSC_t) NUM_DIGITAL_PINS); }
static void routeAcount(OSCMessage &)  { bundleOUT.add("/s/a").add((intOSC_t) NUM_ANALOG_INPUTS); }
static void routeBtn(OSCMessage &)   { addBtn(); }
static void routePot(OSCMessage &)   { addPot(); }
static void routeMic(OSCMessage &)   { addMic(); }
static void routeGas(OSCMessage &)   { addGas(); }
static void routeCap(OSCMessage &)   { addCap(); }
static void routeLight(OSCMessage &) { addLight(); }
static void routeTemp(OSCMessage &)  { addTemp(); }
static void routeHum(OSCMessage &)   { addHum(); }
static void routeImu(OSCMessage &)   { addImu(); }
static void routeDist(OSCMessage &)  { addDist(false); }
static void routeIr(OSCMessage &)    { addIr(); }
static void routeEnq(OSCMessage &)   { addEnq(); }
// /state asked bare answers with the heartbeat, whether or not the stream is
// running: a request that reads something answers on the same address.
static void routeState(OSCMessage &) { bundleOUT.add("/state").add((intOSC_t) seq).add((intOSC_t) millis()); }

void setup() {
  SLIPSerial.begin(115200);

  pinMode(PIN_LED_RED, OUTPUT);    digitalWrite(PIN_LED_RED, LOW);
  pinMode(PIN_LED_GREEN, OUTPUT);  digitalWrite(PIN_LED_GREEN, LOW);
  pinMode(PIN_LED_YELLOW, OUTPUT); digitalWrite(PIN_LED_YELLOW, LOW);
  pinMode(PIN_RELAY, OUTPUT);      digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_VIB, OUTPUT);        analogWrite(PIN_VIB, 0);
  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_HALL, INPUT);
  pinMode(PIN_US_TRIG, OUTPUT);    digitalWrite(PIN_US_TRIG, LOW);
  pinMode(PIN_US_ECHO, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_US_ECHO), echoISR, CHANGE);

  // The pixels have a power enable (and a physical switch beside it, which
  // this cannot override): enable them, start dark.
  pinMode(PIN_RGB_EN, OUTPUT); digitalWrite(PIN_RGB_EN, HIGH);
  strip.begin();
  strip.setBrightness(64);
  strip.clear(); strip.show();

  IrReceiver.begin(PIN_IR, DISABLE_LED_FEEDBACK);

  // The sensors live on I2C1 = GP2/GP3. Probe each address before trusting
  // anything: a bus with nothing on it initialises happily and streams zeros.
  // What answered is what /enq announces.
  Wire1.setSDA(PIN_SDA1); Wire1.setSCL(PIN_SCL1); Wire1.begin();
  Wire1.setClock(400000);
  // The sweep first, and kept for /diag: what is really on the bus is the
  // fact a page or a bench wants when a sensor is missing from /enq.
  strcpy(i2cDiag, "i2c1:");
  for (uint8_t a = 0x08; a < 0x78; a++)
    if (present(Wire1, a) && strlen(i2cDiag) < sizeof i2cDiag - 6)
      snprintf(i2cDiag + strlen(i2cDiag), 6, " 0x%02x", a);
  lightOK = present(Wire1, ADDR_BH1750) && bh1750Begin();
  imuOK   = imuBegin();
  if (imuOK && strlen(i2cDiag) < sizeof i2cDiag - 24)
    snprintf(i2cDiag + strlen(i2cDiag), 24, " imu@0x%02x who 0x%02x", imuAddr, imuWho);
  if (present(Wire1, ADDR_DHT20) && dht20Begin() && dht20Trigger()) {
    // The signal, not begin(): announce temp/hum only once a conversion
    // has actually come back. One blocking wait here, never again.
    delay(90);
    dhtOK = dht20Read(tempC, humPct);
    dhtTriggered = 0;
  }

  addEnq(); flushOut();   // usually lost to enumeration; the page asks again
}

// Non-blocking receive, the extras/webserial/template.ino pattern:
// endofPacket() before available() on every pass, return rather than block
// when the buffer runs dry, and the message at file scope because it fills
// across several passes.
static OSCMessage inMsg;

static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) inMsg.fill((uint8_t) c);
    }
  }
  return true;
}

void loop() {
  static uint32_t last = 0;

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/s/l",    routeLed);
      inMsg.route("/rgb",       routeRgb);
      inMsg.dispatch("/buzz",   routeBuzz);
      inMsg.route("/relay",     routeRelay);
      inMsg.route("/servo",     routeServo);
      inMsg.route("/motor",     routeMotor);
      inMsg.dispatch("/rate",   routeRate);
      inMsg.route("/d",         routeDigital);
      inMsg.route("/a",         routeAnalog);
      inMsg.dispatch("/s/m",    routeMicros);
      inMsg.dispatch("/s/d",    routeDcount);
      inMsg.dispatch("/s/a",    routeAcount);
      inMsg.dispatch("/btn",    routeBtn);
      inMsg.dispatch("/pot",    routePot);
      inMsg.dispatch("/mic",    routeMic);
      inMsg.dispatch("/gas",    routeGas);
      inMsg.dispatch("/cap",    routeCap);
      inMsg.dispatch("/light",  routeLight);
      inMsg.dispatch("/temp",   routeTemp);
      inMsg.dispatch("/hum",    routeHum);
      inMsg.dispatch("/imu",    routeImu);
      inMsg.dispatch("/dist",   routeDist);
      inMsg.dispatch("/ir",     routeIr);
      inMsg.dispatch("/enq",    routeEnq);
      inMsg.dispatch("/state",  routeState);
    }
    inMsg.empty();
    flushOut();
  }

  const uint32_t now = millis();
  if (buzzUntil && now >= buzzUntil) { noTone(PIN_BUZZER); buzzUntil = 0; }

  // The remote: decode whenever a frame is in, remember it for the next
  // stream tick, and free the receiver at once.
  if (IrReceiver.decode()) {
    if (IrReceiver.decodedIRData.protocol != UNKNOWN || IrReceiver.decodedIRData.decodedRawData) {
      lastIr = (int32_t) IrReceiver.decodedIRData.decodedRawData;
      irNew = irSeen = true;
    }
    IrReceiver.resume();
  }

  // The DHT20 conversion takes 80 ms: trigger on one pass, read on a later
  // one, once a second. Room air does not move at 20 Hz.
  if (dhtOK) {
    static uint32_t lastDht = 0;
    if (dhtTriggered && now - dhtTriggered >= 90) {
      float t, h;
      if (dht20Read(t, h)) { tempC = t; humPct = h; }
      dhtTriggered = 0;
    } else if (!dhtTriggered && now - lastDht >= 1000) {
      lastDht = now;
      if (dht20Trigger()) dhtTriggered = now;
    }
  }

  if (!reportMs || now - last < reportMs) return;
  last = now;

  // One bundle, sampled in one pass, so every reading in it belongs to the
  // same instant: /state first, then one message per capability streamed.
  // The sound module is sampled BEFORE the rangefinder is pinged: with the
  // ping first, its 40 kHz burst landed inside the 64-sample window and
  // showed up as a /mic spike of 15882 rms in a quiet room (first run,
  // 2026-09-04). Whether that coupling is acoustic or electrical is not
  // known; ordering removes it either way.
  bundleOUT.add("/state").add((intOSC_t) seq++).add((intOSC_t) now);
  addMic();
  addBtn();
  addPot();
  addGas();
  addCap();
  addRelay();
  addLight();
  addTemp();
  addHum();
  addImu();
  addDist(true);
  if (irNew) { addIr(); irNew = false; }
  flushOut();
  pingDistance(now);                        // after the samples, see addMic above
}

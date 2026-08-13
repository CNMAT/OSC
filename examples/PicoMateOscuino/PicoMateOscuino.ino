// DeskPi PicoMate: every sensor on the board, in one OSC message.
//
//   http://localhost/PicoMateOscuino.html   (Web Serial; not file://)
//
// FQBN: rp2040:rp2040:rpipico -- DeskPi ships no board definition, and no
// Arduino code at all; their examples are CircuitPython. The pin map below
// comes from DeskPi's own pinout diagram, cross-checked against those
// examples, and then confirmed on this board by scanning the buses.
//
// TWO I2C BUSES, and they are not the core's defaults:
//
//   Wire1 (i2c1)  SDA GP14  SCL GP15   LSM6DS3TR-C 0x6A, MMC5603 0x30,
//                                      LTR-381RGB 0x53, SHT30 0x44
//   Wire  (i2c0)  SDA GP16  SCL GP17   SSD1315 OLED 0x3C, alone
//
// Scanned on the real board: the sweep found exactly those two groups and
// nothing on the eight other candidate pin pairs.
//
// THE TRAP THAT WOULD COST YOU AN AFTERNOON. arduino-pico's rpipico variant
// defaults Wire1 to GP26/GP27 -- which on this board are THE BUTTON AND THE
// BUZZER. A bare Wire1.begin() quietly reconfigures both as an I2C bus and
// finds nothing. The setters must come first, and they must come before
// begin(), because the core rejects them once the peripheral is running.
//
// GP26 IS DOUBLE-BOOKED: the push button and the rotary encoder's switch are
// the same pin, and DeskPi documents them with opposite polarity (button
// active-low with a pull-up, encoder switch "active high"). This sketch reads
// it once, as one active-low input, and reports one flag. Do not write code
// that claims to tell the two apart.
//
// Outbound
//   /hello ,siiiiii name + a found flag per sensor, so the page hides
//                   panels for anything absent instead of drawing zeros
//   /pm ,iiii ffffff fff ff iiiii
//        seq, button, pir, encoder,
//        ax ay az (g), gx gy gz (dps),
//        mx my mz (uT),
//        tempC, humidity (%),
//        ir, green, red, blue (counts), lux,
//        micRms, micPeak (full scale, 0..32767)
//
//   One message, sampled in one pass, so every value belongs to the same
//   instant -- with a bundle of separate messages the IMU and the light
//   sensor can be milliseconds apart and a receiver cannot tell which
//   readings were simultaneous. The sequence counter makes drops visible.
//
// Inbound
//   /pm/rgb ,iii r g b     the WS2812 on GP22 (one pixel)
//   /pm/buzz ,ii freq ms   passive buzzer on GP27, via tone()
//   /pm/oled ,s...         up to four lines on the SSD1315
//   /pm/rate ,i ms         report interval, 20..2000
//   /hello                 ask for /hello again: the boot one is always lost,
//                          because USB re-enumerates before the host listens
//
// MEASURED on this board: all five I2C parts answer, gravity reads +0.999 g
// on Z, the magnetometer sees ~51 uT total, 23.2 C / 57 % RH, and the light
// sensor reports 105 lx indoors. One thing NOT characterised: the LTR-381's
// R/G/B channel counts read very low at the library's default gain while lux
// is sensible, so treat the raw colour ratio as uncalibrated.
//
// The ZTS6531S PDM microphone (GP9 clock, GP8 data) uses the arduino-pico
// core's own PDM library -- PIO-based, so any pin pair works and no
// SAMD/ESP32-specific driver is needed. Its callback fires from an interrupt,
// so the ISR only copies; the arithmetic happens in loop().
#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_MMC56x3.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino_LTR381RGB.h>
#include <PDM.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

// ---- pins, from DeskPi's pinout diagram ------------------------------------
#define PIN_SDA1   14      // sensors, i2c1
#define PIN_SCL1   15
#define PIN_SDA0   16      // OLED, i2c0
#define PIN_SCL0   17
#define PIN_BTN    26      // shared with the encoder switch; active LOW
#define PIN_ENC_A   7
#define PIN_ENC_B   6
#define PIN_PIR    28      // AS312, active HIGH
#define PIN_RGB    22      // WS2812, one pixel
#define PIN_BUZZ   27      // passive, PWM
#define PIN_MIC_CLK 9      // ZTS6531S PDM
#define PIN_MIC_DIN 8
#define MIC_RATE   16000

#define ADDR_IMU  0x6A
#define ADDR_MAG  0x30
#define ADDR_LTR  0x53
#define ADDR_SHT  0x44
#define ADDR_OLED 0x3C

// Every part here uses a stock Library Manager driver, the LTR-381 included:
// Arduino_LTR381RGB is an official arduino-libraries release and it computes
// lux for you, which hand-rolled register reads do not.

Adafruit_LSM6DS3TRC imu;
Adafruit_MMC5603     mag = Adafruit_MMC5603(0x5603);
Adafruit_SHT31       sht = Adafruit_SHT31(&Wire1);  // bus goes in the ctor here
Adafruit_SSD1306     oled(128, 64, &Wire, -1);
Adafruit_NeoPixel    pixel(1, PIN_RGB, NEO_GRB + NEO_KHZ800);
LTR381RGBClass       ltr(Wire1, ADDR_LTR);

static bool imuOK = false, magOK = false, shtOK = false, ltrOK = false, oledOK = false;
static int32_t  seq = 0;
static uint32_t reportMs = 50, buzzUntil = 0;
static volatile int32_t encPos = 0;
static char lines[4][22] = { "PicoMate", "OSC over USB", "", "" };

// ---- PDM microphone --------------------------------------------------------
// The scaling here is the PyBadge microphone's lessons applied up front rather
// than rediscovered: accumulate squares in uint64 (a pre-shift such as
// (v>>5)^2 silently zeroes a quiet room), divide in float (integer division
// truncates a quiet mean-square to 1), and send rms and peak at FULL SCALE so
// the page can render dBFS instead of a linear bar that pins everything short
// of a shout against the left stop.
static bool     micOK = false;
static int16_t  micBuf[512];
static volatile int  micSamples = 0;
static volatile bool micReady = false;
static int32_t  micRms = 0, micPeak = 0;

static void micISR() {
  const int avail = PDM.available();
  if (avail <= 0 || micReady) return;              // still holding a frame
  const int n = PDM.read(micBuf, avail > (int) sizeof micBuf ? (int) sizeof micBuf : avail);
  micSamples = n / (int) sizeof(int16_t);
  micReady = true;
}

static bool present(TwoWire &w, uint8_t a) {
  w.beginTransmission(a);
  return w.endTransmission() == 0;
}

// The encoder is quadrature on GP7/GP6. Interrupt on A only and read B to get
// direction: one count per detent transition, cheap enough for an ISR.
static void encISR() {
  encPos += (digitalRead(PIN_ENC_A) == digitalRead(PIN_ENC_B)) ? 1 : -1;
}

static void redraw() {
  if (!oledOK) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  for (uint8_t i = 0; i < 4; i++) {
    oled.setCursor(0, (int16_t)(i * 10));
    oled.print(lines[i]);
  }
  oled.display();
}

/* ----------------------------------------------------------------- inbound */

static void routeRgb(OSCMessage &m) {
  if (m.size() < 3) return;
  pixel.setPixelColor(0, pixel.Color(m.getInt(0) & 0xFF,
                                     m.getInt(1) & 0xFF,
                                     m.getInt(2) & 0xFF));
  pixel.show();
}

static void routeBuzz(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t f = m.getInt(0);
  const int32_t ms = (m.size() > 1 && m.isInt(1)) ? m.getInt(1) : 150;
  if (f <= 0) { noTone(PIN_BUZZ); buzzUntil = 0; return; }
  tone(PIN_BUZZ, (unsigned int) f);
  buzzUntil = millis() + (uint32_t) constrain(ms, 10, 5000);
}

static void routeOled(OSCMessage &m) {
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  const int n = m.size() < 4 ? m.size() : 4;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  redraw();
}

static void routeRate(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) reportMs = constrain(m.getInt(0), 20, 2000);
}

static void sendHello() {
  OSCMessage h("/hello");
  h.add("PicoMateOscuino").add(imuOK).add(magOK).add(shtOK).add(ltrOK)
   .add(oledOK).add(micOK);
  SLIPSerial.beginPacket(); h.send(SLIPSerial); SLIPSerial.endPacket();
}
static void routeHello(OSCMessage &) { sendHello(); }

void setup() {
  SLIPSerial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);        // active LOW, shared with encoder sw
  pinMode(PIN_PIR, INPUT);               // AS312, active HIGH
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);

  pixel.begin(); pixel.setBrightness(60); pixel.clear(); pixel.show();

  // Setters BEFORE begin(), and before anything else touches these pins --
  // see the note at the top about Wire1's default landing on GP26/GP27.
  Wire1.setSDA(PIN_SDA1); Wire1.setSCL(PIN_SCL1); Wire1.begin();
  Wire.setSDA(PIN_SDA0);  Wire.setSCL(PIN_SCL0);  Wire.begin();

  // Probe the bus first: a driver's begin() is not proof a part is fitted,
  // and this board is sold with detachable modules that can be snapped off.
  if (present(Wire1, ADDR_IMU)) imuOK = imu.begin_I2C(ADDR_IMU, &Wire1);
  if (present(Wire1, ADDR_MAG)) magOK = mag.begin(ADDR_MAG, &Wire1);
  if (present(Wire1, ADDR_SHT)) shtOK = sht.begin(ADDR_SHT);

  // Arduino_LTR381RGB::begin() returns 1 on success and 0 on failure -- the
  // opposite of the Adafruit begin_I2C() convention used three lines above.
  // Getting it backwards reports the sensor absent and streams zeros, which
  // looks exactly like an unplugged module. It must also run AFTER the
  // Wire1 setters, because its begin() calls _wire->begin() itself.
  if (present(Wire1, ADDR_LTR)) ltrOK = (ltr.begin() != 0);

  PDM.setCLK(PIN_MIC_CLK);
  PDM.setDIN(PIN_MIC_DIN);
  PDM.onReceive(micISR);
  micOK = PDM.begin(1, MIC_RATE) != 0;              // 1 = mono

  if (present(Wire, ADDR_OLED))
    oledOK = oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
  if (oledOK) { oled.setTextColor(SSD1306_WHITE); oled.cp437(true); redraw(); }

  sendHello();          // nearly always lost -- the page asks again
}

void loop() {
  static uint32_t last = 0;

  if (SLIPSerial.available()) {
    static OSCMessage in;
    in.empty();
    while (!SLIPSerial.endofPacket()) {
      if (SLIPSerial.available()) {
        int c = SLIPSerial.read();
        if (c >= 0) in.fill((uint8_t) c);
      }
    }
    if (!in.hasError()) {
      in.dispatch("/pm/rgb",  routeRgb);
      in.dispatch("/pm/buzz", routeBuzz);
      in.dispatch("/pm/oled", routeOled);
      in.dispatch("/pm/rate", routeRate);
      in.dispatch("/hello",   routeHello);
    }
  }

  const uint32_t now = millis();
  if (buzzUntil && now >= buzzUntil) { noTone(PIN_BUZZ); buzzUntil = 0; }
  if (now - last < reportMs) return;
  last = now;

  float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
  if (imuOK) {
    sensors_event_t a, g, t;
    imu.getEvent(&a, &g, &t);
    ax = a.acceleration.x / 9.80665f;     // report in g, not m/s^2
    ay = a.acceleration.y / 9.80665f;
    az = a.acceleration.z / 9.80665f;
    gx = g.gyro.x * 57.2957795f;          // rad/s -> deg/s
    gy = g.gyro.y * 57.2957795f;
    gz = g.gyro.z * 57.2957795f;
  }

  float mx = 0, my = 0, mz = 0;
  if (magOK) {
    sensors_event_t e;
    mag.getEvent(&e);
    mx = e.magnetic.x; my = e.magnetic.y; mz = e.magnetic.z;
  }

  float tempC = 0, humid = 0;
  if (shtOK) { tempC = sht.readTemperature(); humid = sht.readHumidity(); }

  int ir = 0, gr = 0, rd = 0, bl = 0, rawlux = 0, lux = 0;
  if (ltrOK) ltr.readAllSensors(rd, gr, bl, rawlux, lux, ir);

  if (micReady) {
    const int n = micSamples;
    uint64_t sumsq = 0;
    int32_t  pk = 0;
    for (int i = 0; i < n; i++) {
      const int32_t v = micBuf[i];
      sumsq += (uint32_t)(v * v);
      const int32_t a = v < 0 ? -v : v;
      if (a > pk) pk = a;
    }
    micReady = false;                               // release for the next ISR
    if (n > 0) {
      micRms  = (int32_t) sqrtf((float) sumsq / (float) n);
      micPeak = pk;
    }
  }

  OSCMessage m("/pm");
  m.add((intOSC_t) seq++)
   .add((intOSC_t) (digitalRead(PIN_BTN) == LOW ? 1 : 0))
   .add((intOSC_t) (digitalRead(PIN_PIR) == HIGH ? 1 : 0))
   .add((intOSC_t) encPos)
   .add(ax).add(ay).add(az)
   .add(gx).add(gy).add(gz)
   .add(mx).add(my).add(mz)
   .add(tempC).add(humid)
   .add((intOSC_t) ir).add((intOSC_t) gr)
   .add((intOSC_t) rd).add((intOSC_t) bl)
   .add((intOSC_t) lux)
   .add((intOSC_t) micRms).add((intOSC_t) micPeak);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

// DeskPi PicoMate: every sensor on the board, in one OSC bundle.
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
// The address space is ADDRESSES.md: capability names, not board names, so
// the same page can drive any Oscuino sketch that has the same parts.
//
// Outbound
//   /enq, at boot and whenever asked, is answered with ONE BUNDLE:
//     /enq ,s            "PicoMateOscuino"
//     /enq/btn ,i 1        GP26 push button (shared with the encoder switch)
//     /enq/enc             GP7/GP6 quadrature encoder
//     /enq/rgb ,i 1        the WS2812 on GP22
//     /enq/buzz            passive buzzer on GP27
//     /enq/display ,ii 128 64   only when the SSD1315 answered its probe
//     /enq/imu ,i 6        only when the LSM6DS3TR-C answered: accel + gyro
//     /enq/temp            only when the SHT30 answered
//     /enq/hum             the same SHT30: it measures both, and the contract
//                          makes humidity its own capability
//     /enq/light           only when the LTR-381 answered
//     /enq/mic             only when the PDM library started
//     /enq/diag            only when the MMC5603 answered -- see below
//   A part that did not answer has no /enq line, so the page hides its
//   panel instead of drawing zeros. Presence is proven at boot, not assumed.
//
//   Every /rate ms, one bundle, sampled in one pass:
//     /state ,ii           seq, millis
//     /btn ,i              1 = pressed
//     /d/28 ,i             the AS312 PIR, 1 = motion. ADDRESSES.md has no
//                          motion capability, so the pin's own digital-read
//                          reply carries it; it is not announced by /enq
//     /enc ,ii             position, delta since the previous bundle
//     /imu ,ffffff         ax ay az (g), gx gy gz (deg/s)
//     /temp ,f             degrees C
//     /hum ,f              relative humidity, per cent
//     /light ,i            the LTR-381's raw ALS count, which is what the
//                          contract's one-int light reply asks for
//     /mic ,ii             rms, peak, full scale 0..32767
//
//   One bundle, sampled in one pass, so every value belongs to the same
//   instant -- with separate packets the IMU and the light sensor can be
//   milliseconds apart and a receiver cannot tell which readings were
//   simultaneous. The /state sequence counter makes drops visible.
//
//   TWO PARTS ARE READ BUT NOT STREAMED, because ADDRESSES.md has no
//   capability for either and the contract's own rule is that a reply keeps
//   its documented shape rather than growing arguments:
//     - the MMC5603 magnetometer. /enq/imu's axis count is 3 or 6, so there
//       is no way to announce a compass and no honest way to hang three more
//       floats off /imu. The board says it is fitted in a /diag line -- free
//       text, never parsed -- and the page greys the compass.
//     - the LTR-381's lux conversion and its red/green/blue/IR counts.
//       /light is one int, so the raw ALS count goes out and the rest stays
//       on the board.
//   Both are in the migration report as contract gaps: if a mag capability
//   or a colour capability is added, three lines here restore them.
//
// Inbound
//   /rgb ,iii r g b        the WS2812 on GP22 (one pixel); echoed
//   /rgb/0 ,iii r g b      the same pixel by index; echoed
//   /rgb/bright ,i         0..255; echoed
//   /buzz ,i[i] hz [ms]    passive buzzer on GP27, via tone(); 0 stops; echoed
//   /display/text ,s...    up to four lines on the SSD1315; replies
//                          /display/text <i> lines drawn
//   /display/clear         blank the panel
//   /enc/zero              zero the encoder count
//   /rate ,i ms            bundle period, 20..2000; 0 stops; echoed
//   /enq                 ask for the enq bundle again: the boot one is
//                          always lost, because USB re-enumerates before the
//                          host listens
//
// MEASURED on this board: all five I2C parts answer, gravity reads +0.999 g
// on Z, the magnetometer sees ~51 uT total, 23.2 C / 57 % RH, and the light
// sensor reports 105 lx indoors. One thing NOT characterised: the LTR-381's
// R/G/B channel counts read very low at the library's default gain while lux
// is sensible, so treat the raw colour ratio as uncalibrated.
//
// STATUS: run on the board when added and reworked (commits cea1d3d to
// 263fd5c, 2026-08-13/14); the measurements above and the microphone response
// below were taken with that build, which spoke /pm. Addresses renamed onto
// ADDRESSES.md on 2026-09-03 (/pm/rgb -> /rgb, /pm/buzz -> /buzz, /pm/oled ->
// /display/text, /pm/rate -> /rate, the /pm blob -> /state + /btn, /d/28,
// /enc, /imu, /temp, /hum, /light, /mic, the /enq booleans -> /enq lines);
// that build is compile-checked and has not been re-run on the board. The
// compass and the colour channels the /pm blob carried are no longer on the
// wire at all, for the reason given above, so a board run would show the
// magnetometer and the RGB counts missing rather than wrong.
//
// The ZTS6531S PDM microphone (GP9 clock, GP8 data) uses the arduino-pico
// core's own PDM library -- PIO-based, so any pin pair works and no
// SAMD/ESP32-specific driver is needed. Its callback fires from an interrupt,
// so the ISR only copies; the arithmetic happens in loop().
//
// MIC RESPONSE, measured with the board's own buzzer as the source, which
// removes the coordination problem entirely -- no one has to make a noise on
// cue. Quiet floor rms ~140 (-47.5 dBFS); a 4800 Hz tone lifts it to ~490,
// +10.3 dB, mean of three runs with 1.1 dB spread, and it drops back to the
// floor afterwards. So the microphone genuinely hears.
//
// The frequency matters more than the level does: a sweep from 500 Hz to
// 4800 Hz found +1 to +3 dB nearly everywhere and +11 dB only at the top.
// That is the passive buzzer's resonance, not a property of the microphone --
// testing at 2 kHz first gave an inconclusive +2.1 dB and nearly wrote the
// mic off. Buzzer and mic share this PCB, so part of the coupling is
// structural; this is a response test, not a calibration.
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
#define PIN_PIR    28      // AS312, active HIGH; streamed as /d/28
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

#define OLED_W 128
#define OLED_H 64

// Every part here uses a stock Library Manager driver, the LTR-381 included:
// Arduino_LTR381RGB is an official arduino-libraries release and it computes
// lux for you, which hand-rolled register reads do not.

Adafruit_LSM6DS3TRC imu;
Adafruit_MMC5603     mag = Adafruit_MMC5603(0x5603);
Adafruit_SHT31       sht = Adafruit_SHT31(&Wire1);  // bus goes in the ctor here
Adafruit_SSD1306     oled(OLED_W, OLED_H, &Wire, -1);
Adafruit_NeoPixel    pixel(1, PIN_RGB, NEO_GRB + NEO_KHZ800);
LTR381RGBClass       ltr(Wire1, ADDR_LTR);

static bool imuOK = false, magOK = false, shtOK = false, ltrOK = false, oledOK = false;
static int32_t  seq = 0;
static uint32_t reportMs = 50, buzzUntil = 0;
static volatile int32_t encPos = 0;
static int32_t  encLast = 0;                 // for the delta in /enc
static char lines[4][22] = { "PicoMate", "OSC over USB", "", "" };

// Everything outbound goes through one bundle: the hello, the stream, and
// the echoes, each flushed as its own SLIP packet.
static OSCBundle bundleOUT;

static void flush() {
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

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

// /rgb and /rgb/0 both drive the one pixel; each echoes on its own address.
static bool setRgb(OSCMessage &m) {
  if (m.size() < 3 || !m.isInt(0) || !m.isInt(1) || !m.isInt(2)) return false;
  pixel.setPixelColor(0, pixel.Color(m.getInt(0) & 0xFF,
                                     m.getInt(1) & 0xFF,
                                     m.getInt(2) & 0xFF));
  pixel.show();
  return true;
}
static void echoRgb(const char *addr, OSCMessage &m) {
  bundleOUT.add(addr).add((intOSC_t) (m.getInt(0) & 0xFF))
                     .add((intOSC_t) (m.getInt(1) & 0xFF))
                     .add((intOSC_t) (m.getInt(2) & 0xFF));
  flush();
}
static void routeRgb(OSCMessage &m)  { if (setRgb(m)) echoRgb("/rgb", m); }
static void routeRgb0(OSCMessage &m) { if (setRgb(m)) echoRgb("/rgb/0", m); }

static void routeRgbBright(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t b = constrain(m.getInt(0), 0, 255);
  pixel.setBrightness((uint8_t) b);
  pixel.show();
  bundleOUT.add("/rgb/bright").add((intOSC_t) b);
  flush();
}

static void routeBuzz(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t f = m.getInt(0);
  const int32_t ms = (m.size() > 1 && m.isInt(1)) ? m.getInt(1) : 150;
  if (f <= 0) { noTone(PIN_BUZZ); buzzUntil = 0; }
  else {
    tone(PIN_BUZZ, (unsigned int) f);
    buzzUntil = millis() + (uint32_t) constrain(ms, 10, 5000);
  }
  bundleOUT.add("/buzz").add((intOSC_t) f).add((intOSC_t) ms);
  flush();
}

static void routeText(OSCMessage &m) {
  // Absence is silence (ADDRESSES.md): with no OLED there is no /enq/display
  // in the greeting, so answering here would claim a screen that is not there.
  if (!oledOK) return;
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  const int n = m.size() < 4 ? m.size() : 4;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  redraw();
  bundleOUT.add("/display/text").add((intOSC_t) n);
  flush();
}

static void routeClear(OSCMessage &) {
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  redraw();
}

static void routeEncZero(OSCMessage &) {
  encPos = 0;
  encLast = 0;
}

static void routeRate(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t v = m.getInt(0);
  reportMs = v <= 0 ? 0 : (uint32_t) constrain(v, 20, 2000);   // 0 stops
  bundleOUT.add("/rate").add((intOSC_t) reportMs);
  flush();
}

// The enq bundle: the name, then one /enq line per capability this board
// proved it has. The probes in setup() decide which lines appear.
static void sendEnq() {
  bundleOUT.add("/enq").add("PicoMateOscuino");
  bundleOUT.add("/enq/btn").add((intOSC_t) 1);
  bundleOUT.add("/enq/enc");
  bundleOUT.add("/enq/rgb").add((intOSC_t) 1);
  bundleOUT.add("/enq/buzz");
  if (oledOK) bundleOUT.add("/enq/display").add((intOSC_t) OLED_W).add((intOSC_t) OLED_H);
  if (imuOK)  bundleOUT.add("/enq/imu").add((intOSC_t) 6);   // accel + gyro
  if (shtOK) { bundleOUT.add("/enq/temp"); bundleOUT.add("/enq/hum"); }
  if (ltrOK)  bundleOUT.add("/enq/light");
  if (micOK)  bundleOUT.add("/enq/mic");
  // The magnetometer is fitted and working but has no capability to be
  // announced under, so it is said in free text instead of being faked into
  // /imu's axis count. /diag is never parsed: this is for the human.
  if (magOK) {
    bundleOUT.add("/enq/diag");
    bundleOUT.add("/diag").add("MMC5603 magnetometer present; not streamed, "
                               "ADDRESSES.md has no magnetometer capability");
  }
  flush();
}
static void routeEnq(OSCMessage &) { sendEnq(); }

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

  sendEnq();          // nearly always lost -- the page asks again
}

// ---- one reading, two callers ------------------------------------------------
// Each capability this board announces in /enq has to answer when it is
// asked, not only when the stream happens to tick: ADDRESSES.md says a request
// that reads something answers on the same address, and a /enq line is a
// promise that the request will work. Streaming and answering therefore share
// one function per capability rather than duplicating the sensor code, so the
// two can never drift into reporting different things.

static int      lightRaw = 0;             // the one value /light carries
static uint32_t lastLtr  = 0;

static void addBtn() {
  bundleOUT.add("/btn").add((intOSC_t) (digitalRead(PIN_BTN) == LOW ? 1 : 0));
}

static void addEnc() {
  const int32_t pos = encPos;
  bundleOUT.add("/enc").add((intOSC_t) pos).add((intOSC_t) (pos - encLast));
  encLast = pos;                          // delta is "since the last report"
}

static void addImu() {
  if (!imuOK) return;
  sensors_event_t a, g, t;
  imu.getEvent(&a, &g, &t);
  OSCMessage &m = bundleOUT.add("/imu");
  m.add(a.acceleration.x / 9.80665f)       // report in g, not m/s^2
   .add(a.acceleration.y / 9.80665f)
   .add(a.acceleration.z / 9.80665f)
   .add(g.gyro.x * 57.2957795f)            // rad/s -> deg/s
   .add(g.gyro.y * 57.2957795f)
   .add(g.gyro.z * 57.2957795f);
  // Six axes, no more: /enq/imu said 6, and the contract's axis counts are
  // 3 and 6. The magnetometer's three axes are not appended here -- see the
  // note at the top of the file.
}

// Two capabilities, one part: the SHT30 measures both and the contract gives
// humidity its own address, so the two readings go out separately rather than
// as a second argument on /temp.
static void addTemp() { if (shtOK) bundleOUT.add("/temp").add(sht.readTemperature()); }
static void addHum()  { if (shtOK) bundleOUT.add("/hum").add(sht.readHumidity()); }

// readAllSensors() polls the LTR-381's status register with delay(50) loops
// inside the library -- around 100 ms, 200 worst case -- which blocked every
// report and quietly stretched a 50 ms /rate to 150+. Light changes slowly;
// read it on its own 500 ms cadence and reuse the cached value in between, so
// the report period is honest again. A request gets that same cached value:
// answering must not be slower than streaming.
static void pollLight() {
  const uint32_t now = millis();
  if (!ltrOK || now - lastLtr < 500) return;
  lastLtr = now;
  // The contract's /light reply is one int, the raw ALS count. The colour
  // channels and the library's lux conversion are read and left on the board
  // rather than sent as extra arguments -- a five-argument /light is a private
  // dialect wearing a contract address, which is what this namespace exists
  // to stop.
  int rd = 0, gr = 0, bl = 0, lux = 0, ir = 0;
  ltr.readAllSensors(rd, gr, bl, lightRaw, lux, ir);
}
static void addLight() { if (ltrOK) bundleOUT.add("/light").add((intOSC_t) lightRaw); }

static void drainMic() {
  if (!micReady) return;
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
static void addMic() {
  if (micOK) bundleOUT.add("/mic").add((intOSC_t) micRms).add((intOSC_t) micPeak);
}

// The request handlers. Each answers on the address it was asked on, and the
// reply leaves in the next flush() alongside anything else queued.
static void routeBtn(OSCMessage &)   { addBtn(); }
static void routeEnc(OSCMessage &)   { addEnc(); }
static void routeImu(OSCMessage &)   { addImu(); }
static void routeTemp(OSCMessage &)  { addTemp(); }
static void routeHum(OSCMessage &)   { addHum(); }
static void routeLight(OSCMessage &) { pollLight(); addLight(); }
static void routeMic(OSCMessage &)   { drainMic(); addMic(); }

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
      inMsg.dispatch("/rgb",           routeRgb);
      inMsg.dispatch("/rgb/0",         routeRgb0);
      inMsg.dispatch("/rgb/bright",    routeRgbBright);
      inMsg.dispatch("/buzz",          routeBuzz);
      inMsg.dispatch("/display/text",  routeText);
      inMsg.dispatch("/display/clear", routeClear);
      inMsg.dispatch("/enc/zero",      routeEncZero);
      inMsg.dispatch("/rate",          routeRate);
      inMsg.dispatch("/enq",         routeEnq);
      inMsg.dispatch("/btn",           routeBtn);
      inMsg.dispatch("/enc",           routeEnc);
      inMsg.dispatch("/imu",           routeImu);
      inMsg.dispatch("/temp",          routeTemp);
      inMsg.dispatch("/hum",           routeHum);
      inMsg.dispatch("/light",         routeLight);
      inMsg.dispatch("/mic",           routeMic);
      flush();                          // a request is answered now, not at
                                        // the next stream tick

    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (buzzUntil && now >= buzzUntil) { noTone(PIN_BUZZ); buzzUntil = 0; }
  if (reportMs == 0 || now - last < reportMs) return;
  last = now;

  bundleOUT.add("/state").add((intOSC_t) seq++).add((intOSC_t) now);
  addBtn();
  bundleOUT.add("/d/28").add((intOSC_t) (digitalRead(PIN_PIR) == HIGH ? 1 : 0));
  addEnc();
  addImu();
  addTemp();
  addHum();
  pollLight();
  addLight();
  drainMic();
  addMic();

  flush();
}

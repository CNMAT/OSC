/*
 * EsploraOscuino — the whole Esplora in one OSC bundle
 * -----------------------------------------------------------------------------
 * Board : Arduino Esplora (ATmega32U4)
 * FQBN  : arduino:avr:esplora
 * Libs  : Esplora (Arduino's own board library)
 * Page  : EsploraOscuino.html  (Chrome/Edge, Web Serial)
 *
 * The older OSCEsplora example sends ~20 separate messages, one per sensor,
 * each sampled at a different moment. This sends ONE bundle per report,
 * sampled in a single pass so every value belongs to the same instant, and it
 * speaks the capability-named address space in ADDRESSES.md: nothing on the
 * wire is named after the board, so a page that knows that contract can drive
 * it without knowing it is an Esplora. A report bundle carries:
 *
 *    /state <seq> <millis>  free-running counter (gaps mean dropped packets)
 *                           and the board's millis()
 *    /btn   <i> x5          switch 1 (DOWN), 2 (LEFT), 3 (UP), 4 (RIGHT), then
 *                           the joystick click; 1 = pressed
 *    /joy   <x> <y>         joystick, -512..511 each, the sign the hardware
 *                           gives (readJoystickX() goes positive pushed LEFT,
 *                           readJoystickY() positive pushed DOWN; the page
 *                           flips both, see its comment)
 *    /pot   <i> x3          linear potentiometer 0..1023, then TinkerKit IN-A
 *                           and IN-B (multiplexer channels 8 and 9, 0..1023).
 *                           The contract has no address for auxiliary analog
 *                           inputs that are not Arduino pins; /pot is the
 *                           nearest and the page labels them by connector
 *    /light <i>             light sensor 0..1023
 *    /temp  <f>             degrees C. The Esplora library computes whole
 *                           degrees, so this float has no fraction
 *    /mic   <i> <i>         microphone. The board has a hardware envelope
 *                           follower read by a 10-bit ADC, not rms and peak,
 *                           so the one reading is sent in both slots, scaled
 *                           x32 onto the contract's 0..32767 full scale
 *    /imu   <f> x3          accelerometer X Y Z as RAW ADC COUNTS zeroed at
 *                           rest, NOT g. The Esplora library (1.0.4) returns
 *                           analogRead() 0..1023 minus a fixed rest offset of
 *                           320, 330 and 310 (ACCEL_ZERO_X/Y/Z in Esplora.cpp),
 *                           so X spans -320..703, Y -330..693, Z -310..713.
 *                           Neither the library nor the Arduino reference
 *                           states a counts-per-g figure, so none is invented
 *
 * That matters for sensor fusion: with a bundle of separately sampled
 * messages the accelerometer and joystick readings can be milliseconds apart,
 * and a receiver has no way to tell which readings were simultaneous. Here
 * they are simultaneous by construction, and the sequence counter makes
 * dropped packets visible.
 *
 * Reporting is change-driven, not timer-driven. A bundle goes out when
 * something actually moved, and otherwise once every heartbeat interval so a
 * page that connects mid-session still gets a full picture and so silence is
 * distinguishable from a dead link. Analog channels are compared with a
 * deadband because the microphone, light sensor and accelerometer never read
 * the same value twice - without one, "on change" degenerates into "always".
 * Buttons bypass the deadband: a press is never noise.
 *
 * The Esplora's switches are pulled up and read LOW when pressed. That is an
 * electrical detail, not something a receiver should have to know, so the
 * buttons are inverted here: 1 always means pressed.
 *
 * /enq answers with the capability bundle. Every one of these is soldered
 * to the board, so there is nothing to probe:
 *
 *    /enq EsploraOscuino
 *    /enq/rgb 1    /enq/buzz    /enq/btn 5    /enq/joy 2    /enq/pot 3
 *    /enq/light    /enq/temp    /enq/mic      /enq/imu 3
 *
 * Asking /btn, /joy, /pot, /light, /temp, /mic or /imu (no arguments) is
 * answered with a full report bundle, which carries the answer on the
 * address that was asked, even while the stream is stopped.
 *
 * Inbound, so the page can drive the board:
 *
 *    /rgb        <r> <g> <b>   0..255 each; echoed
 *    /rgb/0      <r> <g> <b>   the same LED by index, the contract's form for
 *                              a one-LED board (/enq/rgb 1); echoed
 *    /rgb/bright <i>           0..255, scales the colour last set; echoed
 *    /buzz       <hz> [<ms>]   0 or no argument stops it; echoed
 *    /d/3        [<i>|<f>]     TinkerKit OUTPUT A, the contract's /d/<pin>:
 *                              int = level (0 LOW, anything else HIGH),
 *                              float 0..1 = PWM duty; no argument reads the
 *                              pin back as /d/3 <i>
 *    /d/11       [<i>|<f>]     TinkerKit OUTPUT B, likewise
 *    /rate       <ms>          floor on the gap between reports, 1..1000;
 *                              0 stops the stream; echoed
 *    /heartbeat  <ms>          report at least this often, 0 disables
 *    /deadband   <counts>      analog change needed to trigger, 0..64
 *    /enq                    resend the capability bundle
 *
 * The page draws its indicators over a photograph of the board (esplora.jpg,
 * Arduino SA, CC BY-SA 3.0 — see CREDITS.md), so the joystick, switches,
 * slider, RGB LED and the four TinkerKit connectors light up where they
 * actually are rather than on a diagram of them.
 *
 * STATUS: the earlier build of this sketch, which sent the same readings as
 * one board-named message, was run on the board — its joystick sign was
 * confirmed on hardware on 2026-08-03 (see the page) and BOARDS.md records
 * 199/199 SLIP frames decoded from it. Addresses renamed onto ADDRESSES.md
 * on 2026-09-03 (/esplora -> /state + /btn + /joy + /pot + /light + /temp +
 * /mic + /imu in one bundle, /tone -> /buzz, /enq <name> <count> ->
 * /enq <name> + /enq/..., /d/3 and /d/11 int 0..255 duty -> int = level and
 * float 0..1 = PWM, /rgb/0 and /rgb/bright added); that build is
 * compile-checked and has not been re-run on the board.
 *
 * Written by Adrian Freed.
 */

#include <Esplora.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

static const long BAUD = 115200;      // ignored by native USB, kept for clarity
static const byte TK_OUT_A = 3;       // TinkerKit OUTPUT A
static const byte TK_OUT_B = 11;      // TinkerKit OUTPUT B

// All of these must outlive a single pass through loop(). pollOSC() returns as
// soon as the serial buffer runs dry, which for anything but a very short frame
// happens part way through one; a message declared inside loop() would lose the
// bytes it had already accumulated every time that happened.
static OSCMessage msgIn;
static OSCMessage msgReply;           // echoes of /rgb, /buzz, /rate, /d reads

// One static message per streamed capability. OSCMessage::empty() frees the
// argument list but keeps the address, and OSCBundle::empty() would also
// delete every message object and address string on every report; on a part
// with 2.5 KB of RAM that is churn worth not having, so the bundle framing is
// written by hand around these (see bundleBegin/bundleAdd/bundleEnd).
static OSCMessage mState("/state"), mBtn("/btn"), mJoy("/joy"), mPot("/pot"),
                  mLight("/light"), mTemp("/temp"), mMic("/mic"), mImu("/imu");

static int32_t  seq = 0;
static uint16_t reportInterval = 20;    // floor on the gap between reports, ms; 0 = stopped
static uint16_t heartbeatMs = 2000;     // report even when nothing moves
static uint16_t deadband = 4;           // analog counts needed to count as change
static uint32_t lastReport = 0;
static bool     reportNow = false;      // a read request: answer with one full report

// One sample of everything, raw, in the units the Esplora library returns.
struct Sample {
  int32_t slider, light, mic, tempC, joyX, joyY;
  int32_t joySw, sw1, sw2, sw3, sw4;
  int32_t accX, accY, accZ;
  int32_t tkA, tkB;
};
static Sample prev;
static bool havePrev = false;

/* ------------------------------------------------------------ bundle out */

// Exactly the bytes OSCBundle::send() writes: "#bundle\0", the immediate
// timetag (0.1), then each message prefixed by its big-endian byte count.
static void bundleBegin() {
  static const uint8_t header[16] = { '#','b','u','n','d','l','e',0,  0,0,0,0,  0,0,0,1 };
  SLIPSerial.beginPacket();
  SLIPSerial.write(header, sizeof header);
}

static void bundleAdd(OSCMessage &m) {
  uint32_t n = (uint32_t) m.bytes();
  uint8_t len[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t) n };
  SLIPSerial.write(len, 4);
  m.send(SLIPSerial);
}

static void bundleEnd() { SLIPSerial.endPacket(); }

static void sendReply() {
  SLIPSerial.beginPacket();
  msgReply.send(SLIPSerial);
  SLIPSerial.endPacket();
}

/* ---------------------------------------------------------------- inbound */

// The one RGB LED answers on /rgb and, as the contract spells a one-LED
// board's pixel, on /rgb/0; /rgb/bright scales whatever colour was last set.
// At the default brightness of 255 the scaling is exact (c * 255 / 255 == c),
// so /rgb alone drives the LED exactly as it did before.
static uint8_t rgbR = 0, rgbG = 0, rgbB = 0, rgbBright = 255;

static uint8_t scaled(uint8_t c) {           // uint16_t: 255 * 255 overflows an AVR int
  return (uint8_t) (((uint16_t) c * rgbBright + 127) / 255);
}
static void showRgb() { Esplora.writeRGB(scaled(rgbR), scaled(rgbG), scaled(rgbB)); }

static bool setRgb(OSCMessage &m) {
  if (m.size() < 3) return false;
  rgbR = (uint8_t) constrain(m.getInt(0), 0, 255);
  rgbG = (uint8_t) constrain(m.getInt(1), 0, 255);
  rgbB = (uint8_t) constrain(m.getInt(2), 0, 255);
  showRgb();
  return true;
}
static void echoRgb(const char *addr) {
  msgReply.empty();
  msgReply.setAddress(addr).add((int32_t) rgbR).add((int32_t) rgbG).add((int32_t) rgbB);
  sendReply();
}
static void routeRgb(OSCMessage &m)  { if (setRgb(m)) echoRgb("/rgb"); }
static void routeRgb0(OSCMessage &m) { if (setRgb(m)) echoRgb("/rgb/0"); }

static void routeRgbBright(OSCMessage &m) {
  if (m.size() < 1) return;
  rgbBright = (uint8_t) constrain(m.getInt(0), 0, 255);
  showRgb();
  msgReply.empty();
  msgReply.setAddress("/rgb/bright").add((int32_t) rgbBright);
  sendReply();
}

static void routeBuzz(OSCMessage &m) {
  int freq = (m.size() >= 1) ? m.getInt(0) : 0;
  if (freq <= 0) { Esplora.noTone(); freq = 0; }
  else if (m.size() >= 2) Esplora.tone((unsigned int) freq, (unsigned long) m.getInt(1));
  else                    Esplora.tone((unsigned int) freq);
  msgReply.empty();
  msgReply.setAddress("/buzz").add((int32_t) freq);
  if (freq > 0 && m.size() >= 2) msgReply.add((int32_t) m.getInt(1));
  sendReply();
}

static void routeRate(OSCMessage &m) {
  if (m.size() < 1) return;
  int r = m.getInt(0);
  reportInterval = (r <= 0) ? 0 : (uint16_t) constrain(r, 1, 1000);
  havePrev = false;                     // a restarted stream opens with a full report
  msgReply.empty();
  msgReply.setAddress("/rate").add((int32_t) reportInterval);
  sendReply();
}

static void routeHeartbeat(OSCMessage &m) {
  if (m.size() < 1) return;
  heartbeatMs = (uint16_t) constrain(m.getInt(0), 0, 60000);
}

static void routeDeadband(OSCMessage &m) {
  if (m.size() < 1) return;
  deadband = (uint16_t) constrain(m.getInt(0), 0, 64);
  havePrev = false;                     // force one full report at the new setting
}

// A read of any streamed capability: one full report, which contains the
// answer on the address that was asked.
static void routeAsk(OSCMessage &) { reportNow = true; }

// TinkerKit OUTPUT A and B follow the contract's /d/<pin> row, the same way
// SerialOscuinowithBundles does: an int is a level (0 = LOW, anything else
// HIGH), a float 0..1 is a PWM duty, and no argument reads the pin back on
// the same address. digitalWrite() on AVR also stops any PWM on the pin.
static void routeOut(OSCMessage &m, byte pin, const char *addr) {
  if (m.isFloat(0)) {
    analogWrite(pin, (int) constrain(m.getFloat(0) * 255.0f, 0.0f, 255.0f));
  } else if (m.isInt(0)) {
    digitalWrite(pin, m.getInt(0) > 0 ? HIGH : LOW);
  } else if (m.size() == 0) {
    msgReply.empty();
    msgReply.setAddress(addr).add((int32_t) digitalRead(pin));
    sendReply();
  }
}
static void routeOutA(OSCMessage &m) { routeOut(m, TK_OUT_A, "/d/3"); }
static void routeOutB(OSCMessage &m) { routeOut(m, TK_OUT_B, "/d/11"); }

// Returns true once a whole packet has been accumulated in msgIn.
static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int avail = SLIPSerial.available();
    if (avail <= 0) return false;            // nothing buffered; try next loop()
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) msgIn.fill((uint8_t) c);   // read() returns -1 on a SLIP error
    }
  }
  return true;
}

/* --------------------------------------------------------------- outbound */

// Sample everything in one pass. readChannel() walks an analog multiplexer, so
// these are not simultaneous to the microsecond, but they are as close together
// as the hardware allows and no host round-trip separates them.
static void sample(Sample &s) {
  s.slider = (int32_t) Esplora.readSlider();
  s.light  = (int32_t) Esplora.readLightSensor();
  s.mic    = (int32_t) Esplora.readMicrophone();
  s.tempC  = (int32_t) Esplora.readTemperature(DEGREES_C);
  s.joyX   = (int32_t) Esplora.readJoystickX();
  s.joyY   = (int32_t) Esplora.readJoystickY();
  // readJoystickSwitch() gives 1023 released / 0 pressed; readJoystickButton()
  // has no return value on the path between those, so it is avoided here.
  s.joySw  = (Esplora.readJoystickSwitch() == 0) ? 1 : 0;
  s.sw1    = (Esplora.readButton(SWITCH_1) == LOW) ? 1 : 0;
  s.sw2    = (Esplora.readButton(SWITCH_2) == LOW) ? 1 : 0;
  s.sw3    = (Esplora.readButton(SWITCH_3) == LOW) ? 1 : 0;
  s.sw4    = (Esplora.readButton(SWITCH_4) == LOW) ? 1 : 0;
  s.accX   = (int32_t) Esplora.readAccelerometer(X_AXIS);
  s.accY   = (int32_t) Esplora.readAccelerometer(Y_AXIS);
  s.accZ   = (int32_t) Esplora.readAccelerometer(Z_AXIS);
  s.tkA    = (int32_t) Esplora.readTinkerkitInputA();   // multiplexer channel 8
  s.tkB    = (int32_t) Esplora.readTinkerkitInputB();   // multiplexer channel 9
}

static bool moved(int32_t a, int32_t b) {
  int32_t d = a > b ? a - b : b - a;
  return d > (int32_t) deadband;
}

// Any button transition, or any analog channel that moved further than the
// deadband. Temperature is included but rarely trips it, which is the point.
// The comparison is on the raw 10-bit samples, so the deadband stays in ADC
// counts whatever scaling the wire format applies.
static bool changed(const Sample &a, const Sample &b) {
  if (a.joySw != b.joySw || a.sw1 != b.sw1 || a.sw2 != b.sw2 ||
      a.sw3   != b.sw3   || a.sw4 != b.sw4) return true;
  return moved(a.slider, b.slider) || moved(a.light, b.light) ||
         moved(a.mic,    b.mic)    || moved(a.tempC, b.tempC) ||
         moved(a.joyX,   b.joyX)   || moved(a.joyY,  b.joyY)  ||
         moved(a.accX,   b.accX)   || moved(a.accY,  b.accY)  ||
         moved(a.accZ,   b.accZ)   || moved(a.tkA,   b.tkA)   ||
         moved(a.tkB,    b.tkB);
}

static void report(const Sample &s, uint32_t now) {
  mState.empty(); mState.add(seq++).add((int32_t) now);
  mBtn.empty();   mBtn.add(s.sw1).add(s.sw2).add(s.sw3).add(s.sw4).add(s.joySw);
  mJoy.empty();   mJoy.add(s.joyX).add(s.joyY);
  mPot.empty();   mPot.add(s.slider).add(s.tkA).add(s.tkB);
  mLight.empty(); mLight.add(s.light);
  mTemp.empty();  mTemp.add((float) s.tempC);
  // one envelope reading in both the rms and the peak slot, x32 -> 0..32736
  mMic.empty();   mMic.add(s.mic * 32).add(s.mic * 32);
  // raw counts as floats; no counts-per-g figure is documented (see header)
  mImu.empty();   mImu.add((float) s.accX).add((float) s.accY).add((float) s.accZ);

  bundleBegin();
  bundleAdd(mState); bundleAdd(mBtn);   bundleAdd(mJoy); bundleAdd(mPot);
  bundleAdd(mLight); bundleAdd(mTemp);  bundleAdd(mMic); bundleAdd(mImu);
  bundleEnd();
}

/* -------------------------------------------------------------------- */

// The boot /enq is very nearly always lost: the board resets, its USB
// device re-enumerates, and the host opens the port some hundreds of
// milliseconds later, by which time setup() has long finished. Measured on
// this repo's ESP32 and SAMD boards -- a probe opening the port straight
// after flashing never once caught it. So /enq is also an INBOUND address
// and the page asks for it on connect.
static void sendEnq() {
  msgReply.empty();
  bundleBegin();
  msgReply.setAddress("/enq").add("EsploraOscuino");          bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/rgb").add((int32_t) 1);   bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/buzz");                   bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/btn").add((int32_t) 5);   bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/joy").add((int32_t) 2);   bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/pot").add((int32_t) 3);   bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/light");                  bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/temp");                   bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/mic");                    bundleAdd(msgReply);
  msgReply.empty(); msgReply.setAddress("/enq/imu").add((int32_t) 3);   bundleAdd(msgReply);
  bundleEnd();
  msgReply.empty();
}

static void routeEnq(OSCMessage &) { sendEnq(); }

void setup() {
  SLIPSerial.begin(BAUD);
  Esplora.writeRGB(0, 0, 0);
  pinMode(TK_OUT_A, OUTPUT); analogWrite(TK_OUT_A, 0);
  pinMode(TK_OUT_B, OUTPUT); analogWrite(TK_OUT_B, 0);

  delay(300);                       // let the host finish enumerating
  sendEnq();                      // nearly always lost; the page asks again
}

void loop() {
  if (pollOSC()) {
    if (!msgIn.hasError()) {
      msgIn.dispatch("/rgb",        routeRgb);
      msgIn.dispatch("/rgb/0",      routeRgb0);
      msgIn.dispatch("/rgb/bright", routeRgbBright);
      msgIn.dispatch("/buzz",      routeBuzz);
      msgIn.dispatch("/d/3",       routeOutA);
      msgIn.dispatch("/d/11",      routeOutB);
      msgIn.dispatch("/rate",      routeRate);
      msgIn.dispatch("/heartbeat", routeHeartbeat);
      msgIn.dispatch("/deadband",  routeDeadband);
      msgIn.dispatch("/enq",     routeEnq);
      msgIn.dispatch("/btn",       routeAsk);
      msgIn.dispatch("/joy",       routeAsk);
      msgIn.dispatch("/pot",       routeAsk);
      msgIn.dispatch("/light",     routeAsk);
      msgIn.dispatch("/temp",      routeAsk);
      msgIn.dispatch("/mic",       routeAsk);
      msgIn.dispatch("/imu",       routeAsk);
    }
    msgIn.empty();
  }

  uint32_t now = millis();
  if (!reportNow) {
    if (reportInterval == 0) return;    // /rate 0: the stream is stopped
    // reportInterval is a floor, not a period: it caps how fast change can
    // push packets out, so a noisy microphone cannot saturate the link.
    if ((uint32_t)(now - lastReport) < reportInterval) return;
  }

  Sample s;
  sample(s);

  bool due = heartbeatMs && (uint32_t)(now - lastReport) >= heartbeatMs;
  if (reportNow || !havePrev || due || changed(s, prev)) {
    reportNow = false;
    lastReport = now;
    prev = s;
    havePrev = true;
    report(s, now);
  }
}

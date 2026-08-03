/*
 * EsploraOscuino — the whole Esplora in one OSC message
 * -----------------------------------------------------------------------------
 * Board : Arduino Esplora (ATmega32U4)
 * FQBN  : arduino:avr:esplora
 * Page  : EsploraOscuino.html  (Chrome/Edge, Web Serial)
 *
 * The older OSCEsplora example sends a bundle of ~20 separate messages, one per
 * sensor. This sends ONE message instead:
 *
 *     /esplora ,iiiiiiiiiiiiiiiiii  <18 ints>
 *
 * Everything the board knows about itself in a single packet, sampled in one
 * pass so the values belong to the same instant. That matters for sensor
 * fusion: with a bundle of separate messages the accelerometer and joystick
 * readings can be milliseconds apart, and a receiver has no way to tell which
 * readings were simultaneous. Here they are simultaneous by construction, and
 * the sequence counter makes dropped packets visible.
 *
 * Argument order is fixed and positional. Index, name, range:
 *
 *    0  seq      free-running counter, wraps at 2^31; gaps mean dropped packets
 *    1  slider   0..1023   linear potentiometer
 *    2  light    0..1023   light sensor
 *    3  mic      0..1023   microphone envelope
 *    4  tempC    degrees Celsius
 *    5  tempF    degrees Fahrenheit
 *    6  joyX     -512..511 joystick X
 *    7  joyY     -512..511 joystick Y
 *    8  joySw    0/1       joystick click, 1 = pressed
 *    9  sw1      0/1       switch 1 (DOWN),  1 = pressed
 *   10  sw2      0/1       switch 2 (LEFT),  1 = pressed
 *   11  sw3      0/1       switch 3 (UP),    1 = pressed
 *   12  sw4      0/1       switch 4 (RIGHT), 1 = pressed
 *   13  accelX   roughly -512..511, zeroed at rest
 *   14  accelY
 *   15  accelZ
 *   16  tinkerA  0..1023  TinkerKit INPUT A, multiplexer channel 8
 *   17  tinkerB  0..1023  TinkerKit INPUT B, multiplexer channel 9
 *
 * Reporting is change-driven, not timer-driven. A packet goes out when
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
 * Inbound, so the page can drive the board:
 *
 *    /rgb        <r> <g> <b>   0..255 each
 *    /tone       <freq> [<ms>]  0 or no argument stops it
 *    /d/3        <0..255>      TinkerKit OUTPUT A, analogWrite
 *    /d/11       <0..255>      TinkerKit OUTPUT B, analogWrite
 *    /rate       <ms>          floor on the gap between reports, 0..1000
 *    /heartbeat  <ms>          report at least this often, 0 disables
 *    /deadband   <counts>      analog change needed to trigger, 0..64
 *
 * The page draws its indicators over a photograph of the board (esplora.jpg,
 * Arduino SA, CC BY-SA 3.0 — see CREDITS.md), so the joystick, switches,
 * slider, RGB LED and the four TinkerKit connectors light up where they
 * actually are rather than on a diagram of them.
 *
 * Written by Adrian Freed, CNMAT. Part of the CNMAT OSC library.
 */

#include <Esplora.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

static const long BAUD = 115200;      // ignored by native USB, kept for clarity
static const byte TK_OUT_A = 3;       // TinkerKit OUTPUT A
static const byte TK_OUT_B = 11;      // TinkerKit OUTPUT B
static const int  ARG_COUNT = 18;

// Both of these must outlive a single pass through loop(). pollOSC() returns as
// soon as the serial buffer runs dry, which for anything but a very short frame
// happens part way through one; a message declared inside loop() would lose the
// bytes it had already accumulated every time that happened.
static OSCMessage msgIn;
static OSCMessage msgOut("/esplora");

static int32_t  seq = 0;
static uint16_t reportInterval = 20;    // floor on the gap between reports, ms
static uint16_t heartbeatMs = 2000;     // report even when nothing moves
static uint16_t deadband = 4;           // analog counts needed to count as change
static uint32_t lastReport = 0;

// One sample of everything, in the order it goes on the wire (after seq).
struct Sample {
  int32_t slider, light, mic, tempC, tempF, joyX, joyY;
  int32_t joySw, sw1, sw2, sw3, sw4;
  int32_t accX, accY, accZ;
  int32_t tkA, tkB;
};
static Sample prev;
static bool havePrev = false;

/* ---------------------------------------------------------------- inbound */

static void routeRgb(OSCMessage &m) {
  if (m.size() < 3) return;
  Esplora.writeRGB(
    (byte) constrain(m.getInt(0), 0, 255),
    (byte) constrain(m.getInt(1), 0, 255),
    (byte) constrain(m.getInt(2), 0, 255));
}

static void routeTone(OSCMessage &m) {
  if (m.size() < 1) { Esplora.noTone(); return; }
  int freq = m.getInt(0);
  if (freq <= 0) { Esplora.noTone(); return; }
  if (m.size() >= 2) Esplora.tone((unsigned int) freq, (unsigned long) m.getInt(1));
  else               Esplora.tone((unsigned int) freq);
}

static void routeRate(OSCMessage &m) {
  if (m.size() < 1) return;
  reportInterval = (uint16_t) constrain(m.getInt(0), 0, 1000);
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

// TinkerKit OUTPUT A and B are plain PWM pins. Accept an int 0..255, or a
// float 0..1 for callers that think in normalised terms.
static int pwmArg(OSCMessage &m) {
  if (m.isFloat(0)) return (int) constrain(m.getFloat(0) * 255.0f, 0.0f, 255.0f);
  return (int) constrain(m.getInt(0), 0, 255);
}
static void routeOutA(OSCMessage &m) { if (m.size()) analogWrite(TK_OUT_A, pwmArg(m)); }
static void routeOutB(OSCMessage &m) { if (m.size()) analogWrite(TK_OUT_B, pwmArg(m)); }

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
  s.tempF  = (int32_t) Esplora.readTemperature(DEGREES_F);
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
static bool changed(const Sample &a, const Sample &b) {
  if (a.joySw != b.joySw || a.sw1 != b.sw1 || a.sw2 != b.sw2 ||
      a.sw3   != b.sw3   || a.sw4 != b.sw4) return true;
  return moved(a.slider, b.slider) || moved(a.light, b.light) ||
         moved(a.mic,    b.mic)    || moved(a.tempC, b.tempC) ||
         moved(a.tempF,  b.tempF)  || moved(a.joyX,  b.joyX)  ||
         moved(a.joyY,   b.joyY)   || moved(a.accX,  b.accX)  ||
         moved(a.accY,   b.accY)   || moved(a.accZ,  b.accZ)  ||
         moved(a.tkA,    b.tkA)    || moved(a.tkB,   b.tkB);
}

static void send(const Sample &s) {
  // empty() keeps the address and reuses the allocation, so the steady state
  // does not churn the heap. On a 2.5 KB part that is worth caring about.
  msgOut.empty();
  msgOut.add(seq++)
        .add(s.slider).add(s.light).add(s.mic)
        .add(s.tempC).add(s.tempF)
        .add(s.joyX).add(s.joyY).add(s.joySw)
        .add(s.sw1).add(s.sw2).add(s.sw3).add(s.sw4)
        .add(s.accX).add(s.accY).add(s.accZ)
        .add(s.tkA).add(s.tkB);

  SLIPSerial.beginPacket();
  msgOut.send(SLIPSerial);
  SLIPSerial.endPacket();
}

/* -------------------------------------------------------------------- */

void setup() {
  SLIPSerial.begin(BAUD);
  Esplora.writeRGB(0, 0, 0);
  pinMode(TK_OUT_A, OUTPUT); analogWrite(TK_OUT_A, 0);
  pinMode(TK_OUT_B, OUTPUT); analogWrite(TK_OUT_B, 0);

  delay(300);                       // let the host finish enumerating
  OSCMessage hello("/hello");
  hello.add("EsploraOscuino").add((int32_t) ARG_COUNT);
  SLIPSerial.beginPacket();
  hello.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void loop() {
  if (pollOSC()) {
    if (!msgIn.hasError()) {
      msgIn.dispatch("/rgb",       routeRgb);
      msgIn.dispatch("/tone",      routeTone);
      msgIn.dispatch("/d/3",       routeOutA);
      msgIn.dispatch("/d/11",      routeOutB);
      msgIn.dispatch("/rate",      routeRate);
      msgIn.dispatch("/heartbeat", routeHeartbeat);
      msgIn.dispatch("/deadband",  routeDeadband);
    }
    msgIn.empty();
  }

  uint32_t now = millis();
  // reportInterval is a floor, not a period: it caps how fast change can push
  // packets out, so a noisy microphone cannot saturate the link.
  if ((uint32_t)(now - lastReport) < reportInterval) return;

  Sample s;
  sample(s);

  bool due = heartbeatMs && (uint32_t)(now - lastReport) >= heartbeatMs;
  if (!havePrev || due || changed(s, prev)) {
    lastReport = now;
    prev = s;
    havePrev = true;
    send(s);
  }
}

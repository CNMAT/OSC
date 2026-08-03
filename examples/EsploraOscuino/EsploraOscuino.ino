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
 *     /esplora ,iiiiiiiiiiiiiiii  <16 ints>
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
 *
 * The Esplora's switches are pulled up and read LOW when pressed. That is an
 * electrical detail, not something a receiver should have to know, so the
 * buttons are inverted here: 1 always means pressed.
 *
 * Inbound, so the page can drive the board:
 *
 *    /rgb   <r> <g> <b>     0..255 each
 *    /tone  <freq> [<ms>]   0 or no argument stops it
 *    /rate  <ms>            minimum interval between reports, 0..1000
 *
 * Written by Adrian Freed, CNMAT. Part of the CNMAT OSC library.
 */

#include <Esplora.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

static const long BAUD = 115200;      // ignored by native USB, kept for clarity
static const int  ARG_COUNT = 16;

// Both of these must outlive a single pass through loop(). pollOSC() returns as
// soon as the serial buffer runs dry, which for anything but a very short frame
// happens part way through one; a message declared inside loop() would lose the
// bytes it had already accumulated every time that happened.
static OSCMessage msgIn;
static OSCMessage msgOut("/esplora");

static int32_t  seq = 0;
static uint16_t reportInterval = 20;  // ms; 50 Hz by default
static uint32_t lastReport = 0;

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

static void report() {
  // Sample everything in one pass. readChannel() walks an analog multiplexer,
  // so these are not simultaneous to the microsecond, but they are as close
  // together as the hardware allows and no host round-trip separates them.
  int32_t slider = (int32_t) Esplora.readSlider();
  int32_t light  = (int32_t) Esplora.readLightSensor();
  int32_t mic    = (int32_t) Esplora.readMicrophone();
  int32_t tempC  = (int32_t) Esplora.readTemperature(DEGREES_C);
  int32_t tempF  = (int32_t) Esplora.readTemperature(DEGREES_F);
  int32_t joyX   = (int32_t) Esplora.readJoystickX();
  int32_t joyY   = (int32_t) Esplora.readJoystickY();
  // readJoystickSwitch() gives 1023 released / 0 pressed; readJoystickButton()
  // has no return value on the path between those, so it is avoided here.
  int32_t joySw  = (Esplora.readJoystickSwitch() == 0) ? 1 : 0;
  int32_t sw1    = (Esplora.readButton(SWITCH_1) == LOW) ? 1 : 0;
  int32_t sw2    = (Esplora.readButton(SWITCH_2) == LOW) ? 1 : 0;
  int32_t sw3    = (Esplora.readButton(SWITCH_3) == LOW) ? 1 : 0;
  int32_t sw4    = (Esplora.readButton(SWITCH_4) == LOW) ? 1 : 0;
  int32_t accX   = (int32_t) Esplora.readAccelerometer(X_AXIS);
  int32_t accY   = (int32_t) Esplora.readAccelerometer(Y_AXIS);
  int32_t accZ   = (int32_t) Esplora.readAccelerometer(Z_AXIS);

  // empty() keeps the address and reuses the allocation, so the steady state
  // does not churn the heap. On a 2.5 KB part that is worth caring about.
  msgOut.empty();
  msgOut.add(seq++)
        .add(slider).add(light).add(mic)
        .add(tempC).add(tempF)
        .add(joyX).add(joyY).add(joySw)
        .add(sw1).add(sw2).add(sw3).add(sw4)
        .add(accX).add(accY).add(accZ);

  SLIPSerial.beginPacket();
  msgOut.send(SLIPSerial);
  SLIPSerial.endPacket();
}

/* -------------------------------------------------------------------- */

void setup() {
  SLIPSerial.begin(BAUD);
  Esplora.writeRGB(0, 0, 0);

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
      msgIn.dispatch("/rgb",  routeRgb);
      msgIn.dispatch("/tone", routeTone);
      msgIn.dispatch("/rate", routeRate);
    }
    msgIn.empty();
  }

  uint32_t now = millis();
  if ((uint32_t)(now - lastReport) >= reportInterval) {
    lastReport = now;
    report();
  }
}

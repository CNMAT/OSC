/*
 * PlaygroundOscuino — Oscuino over SLIP-encoded USB serial, for Adafruit Circuit Playground Express
 * -----------------------------------------------------------------------------
 * GENERATED FILE — do not edit directly.
 * Source: extras/webserial/template.ino  +  extras/webserial/boards.json
 * Regenerate:  cd extras/webserial && make generate
 *
 * Board : Adafruit Circuit Playground Express (SAMD21G18A)
 * FQBN  : adafruit:samd:adafruit_circuitplayground_m0
 *
 * A1-A7 are the alligator-clip pads. D4/D5 are the buttons and D7 the slide switch, all read with /u for the internal pullup.
 *
 * Pair this with PlaygroundOscuino.html, sitting next to this file. Serve that page
 * over http://localhost or https:// (Web Serial refuses a file:// origin), click
 * Connect, pick the board. No server process and no npm install.
 *
 * ADDRESSES — the standard Oscuino set, so this sketch also answers the CNMAT
 * Max/MSP patches and the other Serial* examples' clients.
 *
 *   /d/<pin>            digitalRead            -> /d/<pin> <int>
 *   /d/<pin>/u          digitalRead w/ pullup  -> /d/<pin>/u <int>
 *   /d/<pin> <int>      digitalWrite
 *   /d/<pin> <float>    analogWrite, 0.0 .. 1.0
 *   /a/<pin>            analogRead             -> /a/<pin> <int>
 *   /a/<pin> <int>      digitalWrite on the matching digital pin
 *   /a/<pin> <float>    analogWrite, 0.0 .. 1.0
 *   /tone/<pin> <freq> [<ms>]   square wave; no argument stops it
 *   /s/m                micros                 -> /s/m <int>
 *   /s/d                digital pin count      -> /s/d <int>
 *   /s/a                analog pin count       -> /s/a <int>
 *   /s/l <int>          set LED_BUILTIN        -> /s/l <int>
 *
 * Everything travels as an OSCBundle in both directions, which is what the
 * stock Oscuino clients expect. Tick "bundle" in the companion page.
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>

// OSCBoards.h defines BOARD_HAS_USB_SERIAL and thisBoardsSerialUSB for boards
// with native USB. Selecting through the macro rather than naming a port keeps
// this example working when a variant calls its USB CDC something unexpected.
#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

// This variant's NUM_*_PINS macros match its pads; nothing to clamp.

static const unsigned long BAUD = 115200;   // ignored on native USB, but Web Serial still demands a value

#ifndef LED_BUILTIN
#define LED_BUILTIN 13
#endif

static OSCBundle bundleOUT;

// -----------------------------------------------------------------------------
// "/12" for pin 12, without pulling in sprintf. Returns a pointer into a static
// buffer, so use the result before calling again.
// -----------------------------------------------------------------------------
static char *numToOSCAddress(int pin) {
  static char s[10];
  int i = 9;
  s[i--] = '\0';
  do {
    s[i--] = "0123456789"[pin % 10];
    pin /= 10;
  } while (pin && i);
  s[i] = '/';
  return &s[i];
}

// Builds "<prefix></pin>[suffix]" into out. prefix is 2 chars, so 12 is ample.
static void pinAddress(char *out, const char *prefix, int pin, const char *suffix) {
  strcpy(out, prefix);
  strcat(out, numToOSCAddress(pin));
  if (suffix) strcat(out, suffix);
}

// -----------------------------------------------------------------------------
// routes
// -----------------------------------------------------------------------------

// "/d/<pin>" — write with an int, analogWrite with a float, read with neither.
void routeDigital(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < NUM_DIGITAL_PINS; pin++) {
    int matched = msg.match(numToOSCAddress(pin), addrOffset);
    if (!matched) continue;

    char addr[12];
    if (msg.isInt(0)) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, msg.getInt(0) > 0 ? HIGH : LOW);
    } else if (msg.isFloat(0)) {
      float v = msg.getFloat(0);
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      pinMode(pin, OUTPUT);
      analogWrite(pin, (int)(v * 255.0f + 0.5f));
    } else if (msg.fullMatch("/u", matched + addrOffset)) {
      pinMode(pin, INPUT_PULLUP);
      pinAddress(addr, "/d", pin, "/u");
      bundleOUT.add(addr).add((intOSC_t)digitalRead(pin));
    } else {
      pinMode(pin, INPUT);
      pinAddress(addr, "/d", pin, NULL);
      bundleOUT.add(addr).add((intOSC_t)digitalRead(pin));
    }
    return;
  }
}

// "/a/<pin>"
void routeAnalog(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < NUM_ANALOG_INPUTS; pin++) {
    int matched = msg.match(numToOSCAddress(pin), addrOffset);
    if (!matched) continue;

    char addr[12];
    if (msg.isInt(0)) {
      pinMode(analogInputToDigitalPin(pin), OUTPUT);
      digitalWrite(analogInputToDigitalPin(pin), msg.getInt(0) > 0 ? HIGH : LOW);
    } else if (msg.isFloat(0)) {
      float v = msg.getFloat(0);
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      pinMode(analogInputToDigitalPin(pin), OUTPUT);
      analogWrite(analogInputToDigitalPin(pin), (int)(v * 255.0f + 0.5f));
    } else {
      pinAddress(addr, "/a", pin, NULL);
      bundleOUT.add(addr).add((intOSC_t)analogRead(pin));
    }
    return;
  }
}

// "/tone/<pin> <freq> [<duration ms>]" — no argument stops the tone.
void routeTone(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < NUM_DIGITAL_PINS; pin++) {
    if (!msg.match(numToOSCAddress(pin), addrOffset)) continue;

    unsigned int freq = 0;
    if (msg.isInt(0))        freq = (unsigned int)msg.getInt(0);
    else if (msg.isFloat(0)) freq = (unsigned int)msg.getFloat(0);

    if (freq == 0) noTone(pin);
    else if (msg.isInt(1))   tone(pin, freq, msg.getInt(1));
    else                     tone(pin, freq);
    return;
  }
}

// "/s/..." — system queries and the built-in LED.
void routeSystem(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/m", addrOffset)) bundleOUT.add("/s/m").add((intOSC_t)micros());
  if (msg.fullMatch("/d", addrOffset)) bundleOUT.add("/s/d").add((intOSC_t)NUM_DIGITAL_PINS);
  if (msg.fullMatch("/a", addrOffset)) bundleOUT.add("/s/a").add((intOSC_t)NUM_ANALOG_INPUTS);
  if (msg.fullMatch("/l", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, v > 0 ? HIGH : LOW);
    bundleOUT.add("/s/l").add((intOSC_t)v);
  }
}

// -----------------------------------------------------------------------------

void setup() {
  SLIPSerial.begin(BAUD);
  pinMode(LED_BUILTIN, OUTPUT);

  // Native-USB boards enumerate after begin(); give the host a moment, then say
  // hello so the browser log shows something the instant it connects.
  delay(300);
  bundleOUT.add("/hello").add("PlaygroundOscuino");
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

// Non-blocking receive. Returns true once a whole packet sits in bundleIN.
//
// ORDERING MATTERS: endofPacket() must be called BEFORE available() on every
// pass. Inside SLIPEncodedSerial, available() drives the SLIP state machine, and
// when it is called while that machine sits on the packet-terminating END with
// more bytes already buffered behind it, the state resets to CHAR — silently
// eating the packet boundary. The stock examples get this ordering right but
// block in the outer while; this version returns instead, so a sketch that also
// has work to do in loop() keeps doing it.
static bool pollOSC(OSCBundle &bundleIN) {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;              // nothing buffered — try later
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t)c);  // read() returns -1 on SLIP error
    }
  }
  return true;
}

// Must outlive a single pass through loop(). pollOSC() returns as soon as the
// serial buffer runs dry, which for anything but a very short frame happens
// part-way through one. When this was declared inside loop() the half-filled
// bundle was destroyed on every such return, so a packet only ever arrived if
// the whole frame happened to be buffered in one go.
static OSCBundle bundleIN;

void loop() {
  if (pollOSC(bundleIN)) {
    if (!bundleIN.hasError()) {
      bundleIN.route("/d", routeDigital);
      bundleIN.route("/a", routeAnalog);
      bundleIN.route("/tone", routeTone);
      bundleIN.route("/s", routeSystem);
    }
    bundleIN.empty();
  }

  // Only transmit when a route actually produced something. Sending an empty
  // bundle every pass would flood the port at loop speed and drown the replies
  // you care about.
  if (bundleOUT.size() > 0) {
    SLIPSerial.beginPacket();
    bundleOUT.send(SLIPSerial);
    SLIPSerial.endPacket();
    bundleOUT.empty();
  }
}

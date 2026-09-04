
/*
 Bidirectional Esplora OSC communications  using SLIP

 Adrian Freed, Jeff Lubow 2013

 Includes some examples of common "best practices" for OSC name space and parameter
 mapping design.

 Board : Arduino Esplora (ATmega32U4)
 FQBN  : arduino:avr:esplora
 Libs  : Esplora (Arduino's own board library)

 Since 2026-09-03 this speaks the capability-named address space of
 ADDRESSES.md: nothing on the wire is named after the board or copied off
 the silkscreen, so a page that knows that contract can drive it without
 knowing it is an Esplora. The 2013 build had two dialects of its own, a
 "raw" one named off the silkscreen and a "cooked" one in unit intervals
 (the STATUS paragraph below lists what moved where); the contract's units
 (raw counts, degrees C, 1 = pressed) replace both.

 Outbound, one /state bundle every /rate ms (default 20; 0 stops), every
 value read in a single pass so the readings belong to the same instant:

    /state <seq> <millis>   free-running counter (gaps mean dropped packets)
                            and the board's millis()
    /btn   <i> x4           switches 1 (DOWN), 2 (LEFT), 3 (UP), 4 (RIGHT);
                            1 = pressed. The hardware reads LOW pressed;
                            that is inverted here so nobody has to know
    /joy   <x> <y> <click>  joystick, -512..511 each, the sign the Esplora
                            library gives (X goes positive pushed LEFT, Y
                            positive pushed DOWN), then the stick's own
                            switch, 1 = pressed. The old build also sent
                            four direction "switches", which were only
                            thresholds on these two axes; a host that wants
                            them can threshold the axes itself
    /pot   <i> x3           linear potentiometer 0..1023, then TinkerKit
                            IN-A and IN-B (multiplexer channels 8 and 9,
                            0..1023). The contract has no address for
                            analog inputs that are not Arduino pins; /pot
                            is the nearest
    /light <i>              light sensor 0..1023
    /temp  <f>              degrees C. The Esplora library computes whole
                            degrees, so the float has no fraction. The old
                            Fahrenheit twin is gone; a host can convert
    /mic   <i> <i>          microphone. The board has a hardware envelope
                            follower behind a 10-bit ADC, not rms and peak,
                            so the one reading fills both slots, scaled x32
                            onto the contract's 0..32767 full scale
    /imu   <f> x3           accelerometer X Y Z as RAW ADC COUNTS zeroed at
                            rest (about -512..511), NOT g: readAccelerometer()
                            subtracts a fixed offset from analogRead() and
                            neither the Esplora library nor the Arduino
                            reference gives a counts-per-g figure, so none
                            is invented here. The old build's "cooked"
                            version divided by 511, a normalisation, not g

 /enq (asked, and once at boot, where it is usually lost to USB
 enumeration) answers with the capability bundle. Everything is soldered to
 the board, so there is nothing to probe:

    /enq OSCEsplora
    /enq/rgb 1   /enq/buzz   /enq/btn 4   /enq/joy 2   /enq/pot 3
    /enq/light   /enq/temp   /enq/mic     /enq/imu 3
    /diag "Arduino Esplora serial number" 2     the old vendor, product
                                                name and serial number
                                                lines, as free text

 Inbound, so a host can drive the board:

    /rgb    <r> <g> <b>   the big RGB LED, 0..255 each; /rgb/0 is the same
                          LED addressed as pixel 0; echoed
    /s/l    <i>           the dinky green LED on pin 13; echoed
    /buzz   <hz> [<ms>]   the buzzer; int or float hz, 0 or no argument
                          stops it; echoed
    /d/3, /d/11 [<i>|<f>] TinkerKit OUTPUT A (D3) and OUTPUT B (D11): int =
                          level, float 0..1 = PWM. With no argument the
                          connector becomes an input and its level comes
                          back as /d/<pin> <i>; /d/<pin>/u reads it with
                          the pull-up on
    /s/v                  answered /s/v <f>, the 32U4's supply voltage
    /rate   <ms>          stream period, 0 stops; echoed
    /enq                resend the capability bundle

 Asking /btn, /joy, /pot, /light, /temp, /mic or /imu with no arguments is
 answered with one full /state bundle, which carries the answer on the
 address that was asked, even while the stream is stopped.

 STATUS: the receive-loop fix of commit ab6ea4f was made while checking
 this example against a real Esplora (its message says so); no end-to-end
 run of this sketch is recorded beyond that, and the 199/199 SLIP figure
 in BOARDS.md belongs to EsploraOscuino, not to this one. Addresses renamed
 onto ADDRESSES.md on 2026-09-03 (/manifest + /serialnumber -> /enq +
 /enq/... + /diag, the per-sensor dump -> /state + /btn + /joy + /pot +
 /light + /temp + /mic + /imu in one bundle, /led/rgb -> /rgb, /led ->
 /s/l, /tone -> /buzz, /out/A + /out/B -> /d/3 + /d/11, /32u4/supplyVoltage
 -> /s/v); that build is compile-checked and has not been re-run on the
 board.
*/

#include <Esplora.h>
#include <OSCBundle.h>
//Teensy and Leonardo variants have special USB serial
#include <SLIPEncodedSerial.h>
#include <OSCBoards.h>

#if !defined(__AVR_ATmega32U4__)
#error select Arduino Esplora in board menu
#endif

SLIPEncodedUSBSerial SLIPSerial(Serial);

const intOSC_t serialnumber = 2;
const byte GREEN_LED_PIN = 13;        // the dinky green LED, top left
const byte TK_OUT_A = 3;              // TinkerKit OUTPUT A
const byte TK_OUT_B = 11;             // TinkerKit OUTPUT B

static uint16_t rateMs = 20;          // stream period; 0 = stopped
static uint32_t lastReport = 0;
static bool reportNow = false;        // a read request: answer with one full bundle
static intOSC_t seq = 0;

// one message, framed as its own SLIP packet
static void sendMessage(OSCMessage &msg) {
  SLIPSerial.beginPacket();
  msg.send(SLIPSerial);
  SLIPSerial.endPacket();
}

// The echoes below reuse the inbound message as its own reply: empty()
// frees the arguments but keeps the address, and the values that were
// actually applied go back in its place.

// Esplora has  a dinky green led at the top left and a big RGB led at the bottom right
// The RGB one is /rgb (or /rgb/0, the same LED as pixel 0); the green one is /s/l.
void routeRgb(OSCMessage &msg) {
  if (msg.isInt(0) && msg.isInt(1) && msg.isInt(2)) {
    intOSC_t r = constrain(msg.getInt(0), 0, 255);
    intOSC_t g = constrain(msg.getInt(1), 0, 255);
    intOSC_t b = constrain(msg.getInt(2), 0, 255);
    Esplora.writeRGB((byte)r, (byte)g, (byte)b);
    msg.empty();
    msg.add(r).add(g).add(b);
    sendMessage(msg);
  }
}

void routeLed(OSCMessage &msg) {
  if (msg.isInt(0)) {
    intOSC_t on = msg.getInt(0) > 0 ? 1 : 0;
    digitalWrite(GREEN_LED_PIN, on ? HIGH : LOW);
    msg.empty();
    msg.add(on);
    sendMessage(msg);
  }
}

// TinkerKit OUTPUT A (D3) and OUTPUT B (D11) under the standard pin
// addresses. Nothing else on the board is brought out to a connector, so
// every other /d/<pin> is ignored.
void routeOut(OSCMessage &msg, int addrOffset) {
  byte pin;
  int n;
  if ((n = msg.match("/3", addrOffset)) > 0) pin = TK_OUT_A;
  else if ((n = msg.match("/11", addrOffset)) > 0) pin = TK_OUT_B;
  else return;
  bool pullup = msg.match("/u", addrOffset + n) > 0;
  if (pullup || msg.size() == 0) {          // a read: the connector is an input
    pinMode(pin, pullup ? INPUT_PULLUP : INPUT);
    msg.empty();
    msg.add((intOSC_t)(digitalRead(pin) == HIGH ? 1 : 0));
    sendMessage(msg);
  } else if (msg.isFloat(0)) {              // float 0..1 = PWM
    pinMode(pin, OUTPUT);
    analogWrite(pin, (int)constrain(msg.getFloat(0) * 255.0f, 0.0f, 255.0f));
  } else if (msg.isInt(0)) {                // int = level
    pinMode(pin, OUTPUT);
    digitalWrite(pin, msg.getInt(0) > 0 ? HIGH : LOW);
  }
}

/**
 * BUZZ
 *
 * square wave output "/buzz"
 *
 * format:
 * /buzz
 *   (no value) = notone
 *  float or int = frequency
 *   optional length of time as an integer in milliseconds afterwards
 *
 **/

void routeBuzz(OSCMessage &msg) {

  intOSC_t frequency = 0;
  intOSC_t duration = 0;
  if (msg.isInt(0)) {
    frequency = msg.getInt(0);
  } else if (msg.isFloat(0)) {
    frequency = msg.getFloat(0);
  }
  if (frequency > 0) {
    if (msg.isInt(1) && msg.getInt(1) > 0) {
      duration = msg.getInt(1);
      Esplora.tone((unsigned int)frequency, (unsigned long)duration);
    } else
      Esplora.tone((unsigned int)frequency);
  } else {
    frequency = 0;
    Esplora.noTone();
  }
  msg.empty();
  msg.add(frequency);
  if (duration > 0) msg.add(duration);
  sendMessage(msg);
}

void routeSupply(OSCMessage &msg) {
  msg.empty();
  msg.add(getSupplyVoltage());
  sendMessage(msg);
}

void routeRate(OSCMessage &msg) {
  if (!msg.isInt(0)) return;
  intOSC_t r = msg.getInt(0);
  rateMs = (r <= 0) ? 0 : (uint16_t)constrain(r, 1, 60000);
  msg.empty();
  msg.add((intOSC_t)rateMs);
  sendMessage(msg);
}

// A read of any streamed capability: one full bundle, which contains the
// answer on the address that was asked.
void routeAsk(OSCMessage &) {
  reportNow = true;
}

const byte MUX_ADDR_PINS[] = {
  A0, A1, A2, A3
};
const byte MUX_COM_PIN = A4;

unsigned int myReadChannel(byte channel) {
  digitalWrite(MUX_ADDR_PINS[0], (channel & 1) ? HIGH : LOW);
  digitalWrite(MUX_ADDR_PINS[1], (channel & 2) ? HIGH : LOW);
  digitalWrite(MUX_ADDR_PINS[2], (channel & 4) ? HIGH : LOW);
  digitalWrite(MUX_ADDR_PINS[3], (channel & 8) ? HIGH : LOW);

  return analogRead(MUX_COM_PIN);
}

// The capability bundle. Every capability here is soldered to the board,
// so there is nothing to probe and the list is fixed.
void sendEnq() {
  OSCBundle bndl;
  bndl.add("/enq").add("OSCEsplora");
  bndl.add("/enq/rgb").add((intOSC_t)1);
  bndl.add("/enq/buzz");
  bndl.add("/enq/btn").add((intOSC_t)4);
  bndl.add("/enq/joy").add((intOSC_t)2);
  bndl.add("/enq/pot").add((intOSC_t)3);
  bndl.add("/enq/light");
  bndl.add("/enq/temp");
  bndl.add("/enq/mic");
  bndl.add("/enq/imu").add((intOSC_t)3);
  bndl.add("/diag").add("Arduino Esplora serial number").add(serialnumber);
  SLIPSerial.beginPacket();
  bndl.send(SLIPSerial);
  SLIPSerial.endPacket();
}

void routeEnq(OSCMessage &) {
  sendEnq();
}

// One /state bundle. Values are acquired as close together as reasonably
// possible to increase their usability in sensor fusion contexts, i.e. in
// this case with the accelerometer. The 2013 build split its dump into four
// bundles for memory's sake; this one is 8 messages, fewer than the largest
// of those four (21), so it goes as one.
void sendState() {
  OSCBundle bndl;
  bndl.add("/state").add(seq++).add((intOSC_t)millis());
  bndl.add("/btn")                                   // 1 = pressed
      .add((intOSC_t)(Esplora.readButton(SWITCH_1) == LOW ? 1 : 0))
      .add((intOSC_t)(Esplora.readButton(SWITCH_2) == LOW ? 1 : 0))
      .add((intOSC_t)(Esplora.readButton(SWITCH_3) == LOW ? 1 : 0))
      .add((intOSC_t)(Esplora.readButton(SWITCH_4) == LOW ? 1 : 0));
  // readJoystickSwitch() gives 1023 released / 0 pressed
  bndl.add("/joy")
      .add((intOSC_t)Esplora.readJoystickX())
      .add((intOSC_t)Esplora.readJoystickY())
      .add((intOSC_t)(Esplora.readJoystickSwitch() == 0 ? 1 : 0));
  bndl.add("/pot")
      .add((intOSC_t)Esplora.readSlider())
      .add((intOSC_t)myReadChannel(CH_TINKERKIT_A))
      .add((intOSC_t)myReadChannel(CH_TINKERKIT_B));
  bndl.add("/light").add((intOSC_t)Esplora.readLightSensor());
  bndl.add("/temp").add((float)Esplora.readTemperature(DEGREES_C));
  // one envelope reading in both the rms and the peak slot, x32 -> 0..32736
  intOSC_t mic = (intOSC_t)Esplora.readMicrophone() * 32;
  bndl.add("/mic").add(mic).add(mic);
  // raw counts as floats; no counts-per-g figure is documented (see header)
  bndl.add("/imu")
      .add((float)Esplora.readAccelerometer(X_AXIS))
      .add((float)Esplora.readAccelerometer(Y_AXIS))
      .add((float)Esplora.readAccelerometer(Z_AXIS));
  SLIPSerial.beginPacket();
  bndl.send(SLIPSerial);   // send the bytes to the SLIP stream
  SLIPSerial.endPacket();  // mark the end of the OSC Packet
}

void setup() {
  SLIPSerial.begin(115200);  // set this as high as you can reliably run on your platform
  // Pre-existing: the 2013 build drove pin 13 without ever making it an
  // output, which on an AVR only toggles the pull-up.
  pinMode(GREEN_LED_PIN, OUTPUT);
  sendEnq();               // nearly always lost to enumeration; the host asks again
}

void loop() {
  if (!SLIPSerial.available()) {
    uint32_t now = millis();
    if (reportNow || (rateMs && (uint32_t)(now - lastReport) >= rateMs)) {
      reportNow = false;
      lastReport = now;
      sendState();
    }
  } else {
    OSCBundle bundleIN;
    int size;

    // Bounded, because this loop had no way out: a packet that stops
    // arriving part way leaves available() at 0 and endofPacket() false for
    // good, and the sketch spins there and stops sending. Reachable from
    // whatever is on the other end of the cable.
    unsigned long lastByte = millis();
    while (!SLIPSerial.endofPacket()) {
      if ((size = SLIPSerial.available()) > 0) {
        while (size--) {
          // read() returns int and -1 on underrun; passing that straight to
          // fill() narrowed it to an ordinary 0xFF data byte
          int c = SLIPSerial.read();
          if (c >= 0) bundleIN.fill((uint8_t)c);
        }
        lastByte = millis();
      } else if (millis() - lastByte > 200) {
        break;                  // stalled mid-packet; drop it and move on
      }
    }
    {
      if (!bundleIN.hasError()) {
        bundleIN.dispatch("/enq", routeEnq);
        bundleIN.dispatch("/rgb", routeRgb);
        bundleIN.dispatch("/rgb/0", routeRgb);   // the one LED, as pixel 0
        bundleIN.dispatch("/s/l", routeLed);
        bundleIN.dispatch("/buzz", routeBuzz);
        bundleIN.route("/d", routeOut);          // the TinkerKit output connectors
        bundleIN.dispatch("/s/v", routeSupply);
        bundleIN.dispatch("/rate", routeRate);
        bundleIN.dispatch("/btn", routeAsk);
        bundleIN.dispatch("/joy", routeAsk);
        bundleIN.dispatch("/pot", routeAsk);
        bundleIN.dispatch("/light", routeAsk);
        bundleIN.dispatch("/temp", routeAsk);
        bundleIN.dispatch("/mic", routeAsk);
        bundleIN.dispatch("/imu", routeAsk);
      }
    }
  }
}

/*
 * KB2040Oscuino — a Wii extension controller on an Adafruit KB2040's
 * STEMMA QT port, found, identified and read by NintendoExtensionCtrl and
 * streamed as OSC over SLIP-encoded USB serial: XiaoSamd21Oscuino's I2C mode
 * on its own, with no touch pads and no breadboard.
 * -----------------------------------------------------------------------------
 * Board : Adafruit KB2040 (RP2040, VID 0x239A PID 0x8105)
 * FQBN  : rp2040:rp2040:adafruit_kb2040
 * Libs  : "Nintendo Extension Ctrl" (0.8.3) from the Library Manager; "OSC"
 *         (CNMAT, 4.0.0). Built here with arduino-pico 6.1.1.
 * Page  : KB2040Oscuino.html beside this sketch; serve this folder
 *         (python3 -m http.server) and open it in Chrome or Edge
 * Tests : extras/webserial/test/test-kb2040-contract.mjs holds this
 *         sketch's Wii code to XiaoSamd21Oscuino's line for line, its
 *         WiiExtension.h to that sketch's byte for byte, the page's codec,
 *         Wii lists and drawings to that sketch's page, the addresses to
 *         the page's, and the page's drawing to the two boards' files;
 *         test/hardware/wiiprobe.py PORT and contractprobe.py PORT on the
 *         board.
 *
 * WIRING. One STEMMA QT (Qwiic) cable, a JST SH 4-pin plug at each end, from
 * the KB2040's STEMMA QT port to either of the two on Adafruit's Wii Nunchuck
 * Breakout Adapter (product 4836); the extension's plug slides onto the
 * adapter's tongue. Nothing else. From the two boards' schematics
 * (adafruit/Adafruit-KB2040-PCB at b4160dc, adafruit/Adafruit-Wii-Nunchuck-
 * Breakout-Adapter-PCB at d3f2e25): the KB2040's connector carries GND, 3.3V,
 * SDA and SCL on its pads 1 to 4, and either of the adapter's carries GND,
 * VIN, SDA and SCL, so the adapter's VIN is the KB2040's 3.3 V, as the
 * adapter's guide asks: "give it the same power as the logic level of your
 * microcontroller". SDA is GPIO12 and SCL GPIO13, the RP2040's I2C0 (Adafruit's
 * KB2040 pinouts page, and the schematic's nets), which the core's
 * adafruit_kb2040 variant gives Wire (PIN_WIRE0_SDA 12, PIN_WIRE0_SCL 13).
 * The KB2040 pulls both lines up to its 3.3V through 5.1K (R10, R12), and
 * the adapter pulls its microcontroller side of each up to VIN through 10K.
 *
 * ADDRESSES. The OSC library's ADDRESSES.md is the contract for these; they
 * are XiaoSamd21Oscuino's I2C addresses, and its core ones but /heartbeat and
 * /deadband, which there apply to touch only:
 *
 *   /enq                  -> /enq "KB2040Oscuino", /enq/i2c, /enq/wii <s>
 *                            while a Wii extension is plugged in, then
 *                            /enq/diag and /diag <s>: the bus and the
 *                            extension's kind and identity bytes
 *   /state                -> /state <seq> <millis>, seq = the next report's
 *   /s/m                  -> /s/m <micros>
 *   /rate <ms>            stream period, 0..1000; 0 stops the stream. Echoed
 *   /i2c [<i>]            ask, or 1 to start the bus and 0 to stop it
 *                         -> /i2c <i>, /i2c/pull
 *   /i2c/scan             -> /i2c/scan <i>..., each 7-bit address from 0x08
 *                            to 0x77 that answered a one-byte read (only
 *                            while on)
 *   /i2c/pull             -> /i2c/pull, with nothing after the address.
 *                            XiaoSamd21Oscuino reads each line against the
 *                            chip's pull-down to find a pull-up outside the
 *                            board; here the board's own 5.1K pull-ups (R10,
 *                            R12) are always on both lines, so that reading
 *                            could not tell whether the adapter is there
 *   /wii                  -> /wii <i|f>..., the extension's fields in its
 *                            kind's order (ADDRESSES.md "wii fields"); no
 *                            reply when none is plugged in
 *   /wii/raw              -> /wii/raw <i>..., the bytes the library read, and
 *                            the /wii from that same update()
 *   /wii/id               -> /wii/id <s> <i>x6, the kind and the six identity
 *                            bytes; the bare address when none is plugged in
 *
 *   STREAM: one bundle every /rate ms (20 at power-up, CircuitPlaygroundSensors'
 *   value, as on XiaoSamd21Oscuino): /state and, while an extension is plugged
 *   in, /wii; /state alone while the bus is off or empty, because /state is the
 *   stream's heartbeat.
 *
 * THE BUS is on from power-up; /i2c 0 stops it and /i2c 1 starts it again.
 * Nothing else here wants the two pins, so stopping it only frees the port.
 * Before Wire takes the pins each line is read as a plain input, and a line
 * that reads LOW against the board's own pull-ups is held down by something:
 * the bus is then not started, and /diag says so. That is
 * XiaoSamd21Oscuino's rule, made there because the SAMD core's Wire waits on
 * the bus with no timeout (that sketch's comment). arduino-pico's Wire gives
 * up after its timeout, which this sketch leaves at the core's default,
 * Stream's 1000 ms (Wire's setTimeout() is never called), so a bus that hangs
 * would hold each call up to a second. Not seen.
 *
 * WII EXTENSIONS, through NintendoExtensionCtrl (Library Manager "Nintendo
 * Extension Ctrl" 0.8.3, by David Madison, LGPL-3.0-or-later, used as
 * installed), exactly as XiaoSamd21Oscuino uses it: WiiExtension.h is that
 * sketch's file, byte for byte, and wiiForget(), wiiProbe(), wiiRead(),
 * wiiUpdate(), wiiName(), addWii(), addWiiId(), routeWii(), i2cTick() and
 * pollOSC() are its functions, line for line, as are flush(), addI2c(),
 * argInt(), routeState() and routeSystem(). The library finds, initialises,
 * identifies and reads the extension, with ExtensionPort::connect(),
 * getControllerType() and update() as its MultipleTypes example uses them;
 * this sketch has no protocol code of its own. ADDRESSES.md's "wii fields"
 * lists the fields of each kind, and the page draws each kind.
 *
 * The bus runs at 100 kHz, the core's TWI_CLOCK (its Wire.h), which the
 * library's begin() leaves as it is. XiaoSamd21Oscuino measured that clock
 * and the 1 ms gap below on a SAMD21 (its header has the runs, and
 * test/hardware/WiiLibProbe the probe); both are carried here from that
 * board, NOT measured on this one.
 *
 * Every call of the library's connect(), requestIdentity() and update(), and
 * every probe of the scan, is followed by 1 ms of idle bus (I2C_GAP_MS). On
 * the SAMD21, a requestIdentity() straight after connect() returned succeeded
 * 0 times of 30, and 1 ms after, 30 of 30; why the extension needs the gap
 * was not looked into, so it is kept.
 *
 * STATUS, 2026-10-30: built (arduino-cli 1.5.1, arduino-pico 6.1.1,
 * NintendoExtensionCtrl 0.8.3) and run on a KB2040 (USB serial
 * DF6310711B605E39) with a Nunchuk on the adapter: contractprobe.py 13
 * passed, 1 skipped (/diag); wiiprobe.py 13 of 13 (BOARDS.md). By hand
 * through the page with this board, the same day: "it works" (the user's
 * words); observed, not recorded. Which way the buttons and the stick read
 * was not checked (wiiprobe.py --watch).
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <Wire.h>
#include "WiiExtension.h"

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#define SLIP_PORT thisBoardsSerialUSB      // the port under SLIPSerial, for pollOSC()
#else
SLIPEncodedSerial SLIPSerial(Serial);
#define SLIP_PORT Serial
#endif

static const unsigned long BAUD = 115200;  // ignored on native USB

/* ----------------------------------------------------------------- state */

// bundleIN must outlive one pass through loop(): pollOSC() returns as soon as
// the serial buffer runs dry, often part way through a packet.
static OSCBundle bundleIN;
static OSCBundle bundleOUT;

static int32_t  seq = 0;
static uint16_t reportInterval = 20;       // stream period, ms; 0 = stream off
static uint32_t lastReport = 0;

// The I2C bus on the STEMMA QT port: GPIO12 (SDA) and GPIO13 (SCL), the
// variant's Wire pins.
static bool i2cOn = false;
static bool busLow = false;                // a line read LOW at start: bus not started
static int8_t wiiKind = -1;                // WII_KINDS index of the connected extension, or -1
static bool wiiPresent = false;            // NintendoExtensionCtrl connected to something
static uint8_t wiiId[6];
static uint32_t lastProbe = 0;
static const uint16_t WII_PROBE_MS = 250;  // how often an empty bus is asked again: XiaoSamd21Oscuino's choice
// The bus is left idle this long after each of the scan's probes and after
// each call of the library's connect(), requestIdentity() and update(); inside
// a call the library keeps its own timing. XiaoSamd21Oscuino's value, measured
// there on a SAMD21 (2026-10-29, Wire at 100 kHz, a Nunchuk on Adafruit's
// adapter): a requestIdentity() 0 ms after connect() returned succeeded 0 times
// of 30, 1 ms after, 30 of 30, as at 2 ms. Not measured on this board.
static const uint8_t I2C_GAP_MS = 1;

/* -------------------------------------------------------------------- I2C */

static void wiiForget() {
  wiiKind = -1;
  wiiPresent = false;
  memset(wiiId, 0, sizeof wiiId);
  lastProbe = millis();
}

// Each line is read as a plain input before Wire has the pins: the board's
// own 5.1K pull-ups hold an idle bus high, so a line that reads LOW is held
// down by something, and the bus is not started (see THE BUS above).
static bool i2cStart() {
  pinMode(SDA, INPUT);
  pinMode(SCL, INPUT);
  delay(1);
  busLow = !digitalRead(SDA) || !digitalRead(SCL);
  if (busLow) return false;
  wiiPort.begin();                         // the library's Wire.begin(): 100 kHz, the core's TWI_CLOCK
  i2cOn = true;
  wiiForget();
  lastProbe = millis() - WII_PROBE_MS;     // probe on the first tick
  return true;
}

static void i2cStop() {
  Wire.end();                              // the core's end() leaves both pins plain inputs
  i2cOn = false;
  wiiForget();
}

// An extension, found and identified by NintendoExtensionCtrl: its connect()
// initialises it, reads its identity and runs the matching type's own
// start-up. connect() keeps only the type, so the six identity bytes are read
// again with the library's requestIdentity(). Each call is followed by
// I2C_GAP_MS of idle bus, which the read after connect() needs.
static bool wiiProbe() {
  const bool found = wiiPort.connect();
  delay(I2C_GAP_MS);
  if (!found) return false;
  wiiKind = wiiKindOf(wiiPort.getControllerType());
  if (!wiiPort.requestIdentity(wiiId)) memset(wiiId, 0, sizeof wiiId);
  delay(I2C_GAP_MS);
  wiiPresent = true;
  return true;
}

// The library's update(): a fresh report into its data, then I2C_GAP_MS of
// idle bus.
static bool wiiRead() {
  const bool ok = wiiPort.update();
  delay(I2C_GAP_MS);
  return ok;
}

// A fresh report. When it fails, connect() again, as the library's
// MultipleTypes example does; where the example keeps trying a second apart,
// this tries once, then forgets the extension, and i2cTick() asks the empty
// bus every WII_PROBE_MS. False only if that one try fails too.
static bool wiiUpdate(bool &changed) {
  changed = false;
  if (wiiRead()) return true;
  const int8_t was = wiiKind;
  if (wiiProbe() && wiiRead()) {
    changed = wiiKind != was;
    return true;
  }
  wiiForget();
  changed = true;
  return false;
}

static const char *wiiName() {
  return wiiKind >= 0 ? WII_KINDS[wiiKind].name : "Unknown";
}

/* --------------------------------------------------------------- outbound */

static void flush() {
  if (bundleOUT.size() == 0) return;
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

static void addI2c() {
  bundleOUT.add("/i2c").add((intOSC_t) (i2cOn ? 1 : 0));
}

// Always the bare address: see /i2c/pull in the header.
static void addPull() {
  bundleOUT.add("/i2c/pull");
}

// Every 7-bit address from 0x08 to 0x77 that answers a one-byte read, probed
// I2C_GAP_MS apart, as XiaoSamd21Oscuino scans: a read writes nothing to
// whatever is there. A /diag line gives the count and both lines' levels, to
// tell an empty bus from one that cannot run. 0x08..0x77 is the range outside
// the I2C specification's two reserved groups of eight (from memory of the
// specification, not re-read for this sketch).
static void addScan() {
  OSCMessage &m = bundleOUT.add("/i2c/scan");
  if (!i2cOn) return;
  uint8_t answered = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    if (Wire.requestFrom(a, (uint8_t) 1) == 1) {
      m.add((intOSC_t) a);
      answered++;
    }
    while (Wire.available()) Wire.read();
    delay(I2C_GAP_MS);
  }
  char d[96];
  snprintf(d, sizeof d, "scan by one-byte reads %u ms apart: %u of 112 answered; SDA %s, SCL %s",
           I2C_GAP_MS, answered, digitalRead(SDA) ? "high" : "low", digitalRead(SCL) ? "high" : "low");
  bundleOUT.add("/diag").add(d);
}

// The extension's fields, from the library's getters (WiiExtension.h); for a
// type the library does not know, the six bytes of its report as the library
// read them, so it can be learned.
static void addWii() {
  OSCMessage &m = bundleOUT.add("/wii");
  if (wiiKind < 0) {
    for (uint8_t i = 0; i < 6; i++) m.add((intOSC_t) wiiPort.getControlData(i));
    return;
  }
  const WiiKind &k = WII_KINDS[wiiKind];
  for (uint8_t i = 0; i < k.count; i++) {
    const WiiField &f = k.fields[i];
    if (f.getf) m.add(f.getf());
    else m.add((intOSC_t) f.get());
  }
}

static void addWiiId() {
  OSCMessage &m = bundleOUT.add("/wii/id");
  if (!wiiPresent) return;
  m.add(wiiName());
  for (uint8_t i = 0; i < 6; i++) m.add((intOSC_t) wiiId[i]);
}

static void sayHello() {
  bundleOUT.add("/enq").add("KB2040Oscuino");
  bundleOUT.add("/enq/i2c");
  if (i2cOn && wiiPresent) bundleOUT.add("/enq/wii").add(wiiName());
  bundleOUT.add("/enq/diag");
  char diag[128];
  if (!i2cOn)
    snprintf(diag, sizeof diag, "I2C off%s", busLow ? ": SDA or SCL read LOW, so the bus was not started" : "");
  else if (wiiPresent)
    snprintf(diag, sizeof diag, "I2C at 100 kHz on the STEMMA QT port, GPIO12 SDA, GPIO13 SCL; "
             "Wii %s %02x %02x %02x %02x %02x %02x", wiiName(),
             wiiId[0], wiiId[1], wiiId[2], wiiId[3], wiiId[4], wiiId[5]);
  else
    snprintf(diag, sizeof diag, "I2C at 100 kHz on the STEMMA QT port, GPIO12 SDA, GPIO13 SCL; "
             "no Wii extension");
  bundleOUT.add("/diag").add(diag);
  flush();
}

/* ---------------------------------------------------------------- inbound */

static bool argInt(OSCMessage &m, int i, int32_t &v) {
  if (m.isInt(i))   { v = m.getInt(i); return true; }
  if (m.isFloat(i)) { v = lroundf(m.getFloat(i)); return true; }
  return false;
}

static void routeEnq(OSCMessage &m) {
  (void) m;
  sayHello();
}

// /i2c [<i>]: ask, or 1 to start the bus and 0 to stop it. A change re-sends
// the hello, because the Wii extension comes and goes with it.
static void routeI2c(OSCMessage &m, int off) {
  int32_t v;
  if (m.fullMatch("/i2c")) {
    bool changed = false;
    if (argInt(m, 0, v) && (v != 0) != i2cOn) {
      if (v) i2cStart(); else i2cStop();
      changed = i2cOn == (v != 0);
    }
    addI2c();
    addPull();
    if (busLow) bundleOUT.add("/diag").add("I2C not started: SDA or SCL reads LOW against the board's pull-ups");
    flush();
    if (changed) sayHello();
    return;
  }
  if (m.fullMatch("/scan", off)) addScan();
  if (m.fullMatch("/pull", off)) addPull();
  flush();
}

// /wii: a fresh reading; /wii/raw: the same, with the bytes the library read
// for it beside it, from the one update(); /wii/id: what is at 0x52. Nothing
// for /wii or /wii/raw, and an empty /wii/id, while no extension is there. An
// ask looks for one itself, so it works with the stream stopped, and an
// extension that no longer answers is forgotten; either change re-sends the
// hello, as the stream's own probe does.
static void routeWii(OSCMessage &m, int off) {
  const bool raw = m.fullMatch("/raw", off), id = m.fullMatch("/id", off);
  if (!raw && !id && !m.fullMatch("/wii")) return;
  bool changed = false;
  if (i2cOn && !wiiPresent) changed = wiiProbe();
  if (id) {
    addWiiId();
  } else if (i2cOn && wiiPresent) {
    bool lost;
    if (wiiUpdate(lost)) {
      if (raw) {
        OSCMessage &r = bundleOUT.add("/wii/raw");
        for (uint8_t i = 0; i < wiiPort.getRequestSize(); i++) r.add((intOSC_t) wiiPort.getControlData(i));
      }
      addWii();
    }
    changed = changed || lost;
  }
  flush();
  if (changed) sayHello();
}

// The stream while the bus is on: an empty bus is asked again every
// WII_PROBE_MS, a found extension read every /rate tick, and a change of
// extension re-sends the hello.
static void i2cTick() {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastReport) < reportInterval) return;
  lastReport = now;
  bool have = false, changed = false;
  if (wiiPresent) {
    have = wiiUpdate(changed);
  } else if ((uint32_t)(now - lastProbe) >= WII_PROBE_MS) {
    lastProbe = now;
    if (wiiProbe()) {
      changed = true;
      have = wiiRead();
    }
  }
  if (changed) sayHello();
  bundleOUT.add("/state").add(seq++).add((intOSC_t) millis());
  if (have) addWii();
  flush();
}

// Answered with the sequence number the next report will carry, without
// consuming it, as the OSC repo's template.ino does.
static void routeState(OSCMessage &m) {
  (void) m;
  bundleOUT.add("/state").add((intOSC_t) seq).add((intOSC_t) millis());
  flush();
}

static void routeSystem(OSCMessage &m, int off) {
  if (m.fullMatch("/m", off)) bundleOUT.add("/s/m").add((intOSC_t) micros());
  flush();
}

static void routeRate(OSCMessage &m) {
  int32_t v;
  if (!argInt(m, 0, v)) return;
  reportInterval = (uint16_t) constrain(v, 0, 1000);
  bundleOUT.add("/rate").add((intOSC_t) reportInterval);
  flush();
}

// Returns true once a whole packet has been accumulated in bundleIN.
//
// XiaoSamd21Oscuino's pollOSC(). The template's pollOSC() returns whenever
// available() says 0, but available() also says 0 just after it has swallowed
// a frame's opening END with the frame still buffered behind it, and the
// closing END only reads as the end on the next call, so every message waits
// two extra loop() passes. There a pass could be a 0.70 s touch pad (that
// sketch's header has the runs); here it is at most an I2C update, a probe or
// a scan of 112 addresses, none of them timed on this board. So this keeps
// going while the port still holds bytes, and asks endofPacket() once more
// before giving up: the template's calls in the template's order
// (endofPacket() before available()), without a loop() pass between them.
static bool pollOSC() {
  for (;;) {
    if (SLIPSerial.endofPacket()) return true;
    int avail = SLIPSerial.available();
    if (avail <= 0) {
      if (SLIP_PORT.available() > 0) continue;   // an END was swallowed; more behind it
      return SLIPSerial.endofPacket();           // the closing END, if that was the last byte
    }
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t) c);   // read() returns -1 on a SLIP error
    }
  }
}

/* ---------------------------------------------------------------------- */

void setup() {
  SLIPSerial.begin(BAUD);
  i2cStart();                              // the bus is on from power-up
  delay(300);                              // let the host finish enumerating
  sayHello();
}

void loop() {
  if (pollOSC()) {
    if (!bundleIN.hasError()) {
      bundleIN.dispatch("/enq",   routeEnq);
      bundleIN.dispatch("/state", routeState);
      bundleIN.route("/s",        routeSystem);
      bundleIN.route("/i2c",      routeI2c);
      bundleIN.route("/wii",      routeWii);
      bundleIN.dispatch("/rate",  routeRate);
    }
    bundleIN.empty();
  }

  if (reportInterval == 0) return;
  if (i2cOn) {
    i2cTick();
    return;
  }
  // The bus off: /state alone, the stream's heartbeat.
  const uint32_t now = millis();
  if ((uint32_t)(now - lastReport) < reportInterval) return;
  lastReport = now;
  bundleOUT.add("/state").add(seq++).add((intOSC_t) millis());
  flush();
}

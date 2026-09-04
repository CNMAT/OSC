// M5Stack Atom JoyStick (SKU K137): both sticks, both shoulder buttons, both
// stick clicks, the battery pair and the 128x128 screen, over OSC.
//
//   http://localhost/AtomJoyOscuino.html   (Web Serial; not file://)
//
// FQBN: m5stack:esp32:m5stack_atoms3 with STOCK DEFAULTS. This board is an
// AtomS3 (ESP32-S3FN8) whose boards.txt sets build.usb_mode=1 and
// build.cdc_on_boot=1, so Serial is HWCDCSerial -- the USB-Serial/JTAG
// peripheral, not TinyUSB. Two consequences worth knowing:
//
//   * USBSerial does not exist in this mode. M5Stack's own GetValue.ino uses
//     it and therefore does not compile with the default options ("'USBSerial'
//     was not declared in this scope"). Use Serial.
//   * HWCDC is the one stack in this repo's testing that DROPS rather than
//     NAKs when its receive ring fills. SLIPEncodedSerial::begin() already
//     enlarges that ring (OSC_SLIP_RX_BUFFER, 4096) on ESP32 cores, so this
//     sketch inherits the fix; see test/hardware/README.md for the
//     measurements behind it.
//
// THE STICKS ARE NOT ON THE ESP32'S ADC. They hang off an STM32F030F4P6
// co-processor at I2C 0x59, on the AtomS3's INTERNAL I2C -- SDA GPIO38, SCL
// GPIO39, 400 kHz. Reading the ESP32's own analog pins gets you nothing.
//
// Registers are read directly here rather than through M5Stack's
// Atom-JoyStick library, for two reasons: that library is NOT in the Arduino
// Library Manager (its library.properties is broken -- it names itself
// ATOMS3_FLY_REMOTER and points at a header that does not exist), and its
// battery accessors are crossed, getBattery1Voltage() reading 0x62 while
// getBattery2Voltage() reads 0x60. Seven registers are less trouble than a
// dependency you cannot install and would have to correct anyway.
//
// The map below is not read off a wiki: it is checked against M5Stack's own
// published STM32 firmware, whose whole I2C surface is one function --
// Slave_Complete_Callback() in code/Fly_Remoter/Core/Src/main.c of
// github.com/m5stack/Atom-JoyStick-Internal-FW.
//
//   0x00..0x03    left  stick X / Y, 12-bit (0..4095), LITTLE-endian
//   0x10 / 0x11   the same axes, 8-bit
//   0x20..0x23    right stick X / Y, 12-bit, little-endian
//   0x30 / 0x31   the same axes, 8-bit
//   0x40..0x43    raw battery ADC, 12-bit      0x50 / 0x51  the same, 8-bit
//   0x60..0x63    battery 1 / 2 in millivolts
//   0x70..0x73    buttons, one byte each, bit 0, ACTIVE LOW
//   0xFC          bootloader version   0xFE  firmware version (2 on shipping)
//   0xFF          I2C address (writable)
//
// EACH BLOCK IS SERVED WHOLE, so one 4-byte read gets both axes of a stick
// from the SAME ADC pass. That is why the reads below are blocks and not
// per-axis: two transactions cannot see one instant, and this whole repo's
// examples exist to put one instant in one message.
//
// DO NOT SWEEP THIS REGISTER SPACE. A two-byte write of 0xFD 0x01 makes the
// STM32 tear down I2C and ADC and jump to its bootloader; the joystick then
// stays dead until it is re-flashed. An innocent-looking probe loop bricks
// the base.
//
// BUTTON MAPPING, settled from that firmware rather than left to the bench:
// 0x70 = LEFT function button (PF0), 0x71 = RIGHT function button (PF1),
// 0x72 = LEFT stick press (PA4), 0x73 = RIGHT stick press (PA7). M5Stack's
// own atoms3joy.h carries two contradictory mappings -- a stale set of
// *_ADDRESS macros and a newer set behind #define NEW_ATOM_JOY (defined
// unconditionally on the line above the #ifdef that tests it, so it is the
// one that compiles). The stale set swaps the function buttons with the
// stick presses and is firmware-v1 naming for a board revision that never
// shipped: v1 read BTN_1 from PA3, which on production hardware is the left
// stick's Y axis. Read 0xFE and check it says 2 before trusting any of it.
//
// Active LOW is confirmed three ways: the firmware pulls those inputs up,
// M5's library inverts them, and the schematic carries external 10K pull-ups.
// One asymmetry to know about: 0x73 is the only button the firmware
// configures with NO internal pull-up, so its idle level depends entirely on
// the external part.
//
// M5's PUBLISHED PINMAP TABLE IS WRONG -- it shifts the left joystick by one
// STM32 pin and lists PA1 in two roles -- so it cannot be used to cross-check
// any of this. The firmware and the schematic agree with each other and not
// with the table.
//
// Addresses are the capability names of ADDRESSES.md, not a /joy dialect.
//
// Outbound
//   /enq reply, one bundle: /enq "AtomJoyOscuino", then one /enq line per
//   capability setup() actually proved present -- no booleans, no flags:
//     /enq/joy ,i      4        four axes, only if 0x59 answered
//     /enq/btn ,i      5 or 1   the base's four plus the front button, or the
//                               front button alone when the base is absent
//     /enq/bat                  both battery channels, only if 0x59 answered
//     /enq/display ,ii 128 128  only if the LCD initialised
//     /enq/diag                 free text follows in a separate /diag packet
//   /diag ,s...        which internal bus answered (SDA 38 = AtomS3, 45 =
//                      AtomS3R) and the STM32 firmware version from 0xFE,
//                      as sentences: a page shows them, it never parses them.
//   Stream, one bundle every /rate ms, sampled in one pass so every reading
//   in it belongs to the same instant:
//     /state ,ii       seq, millis            seq makes drops visible
//     /joy ,iiii       lx, ly, rx, ry         0..4095, only if the base answered
//     /btn ,iiiii      L shoulder, R shoulder, L click, R click, front
//                      1 = pressed; ,i front alone when the base is absent
//     /bat ,ii         bat1 mV, bat2 mV       only if the base answered
// Inbound
//   /display/text ,s...  up to five lines on the 128x128 LCD
//   /rate ,i ms          stream period, 20..2000; 0 stops; echoed
//   /joy, /btn, /bat     ask for one reading now: answered on the same
//                        address, in the shape above, whether or not the
//                        stream is running; /joy and /bat only when announced
//   /enq               ask again: the boot one is lost to USB
//                        re-enumeration before the host opens the port
//
// STATUS: RUN ON HARDWARE 2026-08-17, on an AtomS3 in a K137 base. What the
// bench established, as opposed to what the firmware source promised:
//
//   0xFE firmware version reads 2, so the v2 button map above is the one that
//   applies to this unit -- the check the header asks for, performed.
//
//   REST POSITION IS NOT 2048, and is not even the same on both sticks:
//       left  X 2079   left  Y 2123
//       right X 1973   right Y 2123
//   measured over 83 samples with the sticks untouched; noise was +/-1.5 LSB
//   peak to peak. So a client that assumes a 2048 centre draws every stick
//   permanently off-centre, by up to 75 counts. Rest must be sampled at
//   start-up, not assumed -- which is why nothing here or in the page hard
//   codes a centre.
//
//   TRAVEL IS THE FULL 12-BIT RANGE: over a deliberate sweep of both sticks,
//   lx 0..4082, ly 6..4092, rx 0..4087, ry 11..4092. No deadband, no clamped
//   sub-range.
//
//   BOTH battery registers are live on this unit: 4165 mV and 4185 mV. M5's
//   documentation describes one cell shipping, so a second reading near zero
//   would also be normal -- do not treat 0 as a fault.
//
//   All four button bits and the front button were each seen to assert. Which
//   physical control drives which bit is STILL NOT CONFIRMED BY PRESS: the one
//   run that produced an order was contaminated by stick clicks triggered
//   while sweeping. The mapping above is the firmware's, and the firmware is a
//   good source, but this comment does not claim a measurement it does not
//   have.
//
//   Not established: whether this unit is an AtomS3 or an AtomS3R -- it
//   answered on SDA 38, which is the AtomS3 bus, and esptool reports
//   ESP32-S3 (QFN56) with no PSRAM, which is also AtomS3 rather than the
//   PICO-package AtomS3R.
//
//   Addresses renamed onto ADDRESSES.md on 2026-09-03 (the board-named /joy
//   blob -> /state + /joy + /btn + /bat; the screen and rate addresses that
//   sat under the old /joy prefix -> /display/text and /rate; the /enq
//   booleans -> /enq/joy, /enq/btn, /enq/bat, /enq/display, /enq/diag +
//   /diag); that build is compile-checked and has not been re-run on the
//   board.
//
// M5Unified is OPT-IN, and off by default, because of a measurement:
// 2026-08-17, on an AtomS3 seated in the Atom JoyStick base, M5.begin() DOES
// NOT RETURN. setup() never reaches loop() and the board is silent on USB
// forever. A stripped sketch containing nothing but SLIPSerial.begin() and
// M5.begin() reproduced it exactly, so it is not this sketch's joystick code;
// the same binary streams happily on an M5Dial. Out of the base the same
// AtomS3 completes M5.begin() but reports M5.Display.width() == 0, so the
// panel never initialises on this unit either way -- consistent with the
// display, not the base, being the thing M5GFX is stuck on.
//
// NARROWED 2026-08-17: M5GFX ALONE IS FINE. Driving the panel with M5GFX
// directly -- display.begin() -- initialises the LCD, reports a real
// width, draws text, and the sketch goes on streaming. So the fault is
// not the display driver; it is the M5Unified wrapper around it, which
// also brings up power, I2C and the IMU. That is why the screen is on by
// default (ATOMJOY_USE_M5GFX) while M5Unified is not.
//
// Nothing here needs M5Unified: the joystick is plain I2C and the front
// button is a GPIO. So the default build talks to the hardware directly and
// always runs. Define ATOMJOY_USE_M5 to 1 to drive the on-board LCD on a unit
// where M5GFX behaves -- the HTML panel draws the screen regardless.
#ifndef ATOMJOY_USE_M5
#define ATOMJOY_USE_M5 0
#endif
#if ATOMJOY_USE_M5
#include <M5Unified.h>
#endif

// The LCD is driven through M5GFX ALONE, not through M5Unified. M5GFX is the
// display half; M5Unified is the whole-board wrapper that also brings up
// power, I2C and the IMU, and it is the wrapper that hangs here. Splitting
// them keeps the screen without the part that stops the sketch.
#ifndef ATOMJOY_USE_M5GFX
#define ATOMJOY_USE_M5GFX 1
#endif
#if ATOMJOY_USE_M5GFX && !ATOMJOY_USE_M5
#include <M5GFX.h>
static M5GFX display;
#define JOY_LCD display
#elif ATOMJOY_USE_M5
#define JOY_LCD M5.Display
#endif
#include <Wire.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define JOY_ADDR   0x59
// The internal I2C bus MOVES between modules, and the K137's Atom is a
// removable socketed module, so neither pin pair can be hardcoded: AtomS3 puts
// it on SDA 38 / SCL 39, AtomS3R on SDA 45 / SCL 0. M5Stack added AtomS3R
// support to their own JoyStick firmware in April 2026 without changing a line
// of it, so units of both kinds are in circulation and look alike in the base.
// Which pair answers at 0x59 is therefore also the board identification, and
// it is done by probing for the part rather than by trusting a build flag.
#define JOY_SDA_S3   38
#define JOY_SCL_S3   39
#define JOY_SDA_S3R  45
#define JOY_SCL_S3R   0
#define JOY_HZ     400000U

#define REG_L_BLOCK 0x00       // 4 bytes: X lo, X hi, Y lo, Y hi
#define REG_R_BLOCK 0x20       // 4 bytes, same shape
#define REG_BAT     0x60       // 4 bytes: bat1 lo/hi, bat2 lo/hi, millivolts
#define REG_BTN     0x70       // 4 bytes: L fn, R fn, L stick, R stick
#define REG_FW      0xFE       // 1 byte, 2 on shipping hardware

#define BTN_FRONT  41          // the AtomS3's screen doubles as a button,
                               // active LOW; M5.BtnA is just this pin
static bool     joyOK = false, dispOK = false;
static uint8_t  joyFw = 0;     // STM32 firmware version from 0xFE; 2 ships
static int8_t   joySda = -1;   // which internal bus answered: 38=S3, 45=S3R
static int32_t  seq = 0;
static uint32_t reportMs = 50;   // stream period in ms; 0 = stopped (/rate 0)

// The hello reply and every stream frame are bundles, built here and flushed
// as one SLIP packet each.
static OSCBundle bundleOUT;

static void flushBundle() {
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}
static void sendMessage(OSCMessage &m) {       // a single-message packet
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}
static char     lines[5][22] = { "AtomJoyOscuino", "OSC over USB", "", "", "" };

// Open a candidate internal bus and see whether 0x59 answers on it. Returns
// false without leaving the bus claimed, so the next pin pair can be tried.
static bool joyBusBegin(int sda, int scl) {
  Wire.end();
  if (!Wire.begin(sda, scl, JOY_HZ)) return false;
  Wire.beginTransmission(JOY_ADDR);
  if (Wire.endTransmission() == 0) { joySda = (int8_t) sda; return true; }
  Wire.end();
  return false;
}

// Every read is write-register / repeated-START / read, which is what the
// STM32 expects; endTransmission(false) is the repeated START and matters.
static bool joyRead(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(JOY_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int) JOY_ADDR, (int) n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }

// One transaction per block. Both axes of a stick therefore come from the
// same ADC pass on the STM32, which per-axis reads cannot promise -- and a
// message whose two halves are from different instants is the one thing
// these examples are built not to send.
static bool joyPair(uint8_t reg, uint16_t &a, uint16_t &b) {
  uint8_t r[4] = { 0, 0, 0, 0 };
  if (!joyRead(reg, r, 4)) return false;
  a = le16(r); b = le16(r + 2);
  return true;
}

static uint8_t joyButtons() {
  uint8_t r[4] = { 1, 1, 1, 1 };            // idle HIGH: released
  if (!joyRead(REG_BTN, r, 4)) return 0;
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 4; i++)
    if (!(r[i] & 0x01)) mask |= (uint8_t)(1 << i);   // active LOW
  return mask;
}

static void redraw() {
#ifdef JOY_LCD
  if (!dispOK) return;
  JOY_LCD.fillScreen(TFT_BLACK);
  JOY_LCD.setTextColor(TFT_WHITE, TFT_BLACK);
  JOY_LCD.setTextSize(2);
  for (uint8_t i = 0; i < 5; i++) {
    JOY_LCD.setCursor(2, (int)(4 + i * 20));
    JOY_LCD.print(lines[i]);
  }
#endif
}

/* ---------------------------------------------------------------- readings */

// One message per capability, in the shape the hello announced. The stream
// and the on-request replies share these, so a reading has one shape
// whichever way it was asked for.
static void fillJoy(OSCMessage &m) {           // /joy lx ly rx ry, 0..4095
  uint16_t lx = 0, ly = 0, rx = 0, ry = 0;
  joyPair(REG_L_BLOCK, lx, ly);                // two transactions, not four
  joyPair(REG_R_BLOCK, rx, ry);
  m.add((intOSC_t) lx).add((intOSC_t) ly).add((intOSC_t) rx).add((intOSC_t) ry);
}
static void fillBtn(OSCMessage &m) {           // /btn, 1 = pressed
  if (joyOK) {                                 // L, R, L click, R click
    const uint8_t btn = joyButtons();
    for (uint8_t i = 0; i < 4; i++) m.add((intOSC_t) ((btn >> i) & 1));
  }
  m.add((intOSC_t) (digitalRead(BTN_FRONT) == LOW ? 1 : 0));  // the screen itself, last
}
static void fillBat(OSCMessage &m) {           // /bat bat1 mV, bat2 mV
  uint16_t b1 = 0, b2 = 0;
  joyPair(REG_BAT, b1, b2);
  m.add((intOSC_t) b1).add((intOSC_t) b2);
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {         // /display/text "a" ["b" ...]
  for (uint8_t i = 0; i < 5; i++) lines[i][0] = '\0';
  const int n = m.size() < 5 ? m.size() : 5;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  redraw();
}

static void routeRate(OSCMessage &m) {         // /rate <ms>, 0 stops; echoed
  if (m.size() >= 1 && m.isInt(0)) {
    const int v = m.getInt(0);
    reportMs = v <= 0 ? 0 : constrain(v, 20, 2000);
  }
  OSCMessage r("/rate");
  r.add((intOSC_t) reportMs);
  sendMessage(r);
}

// A request for one capability answers on the same address, in the shape the
// stream uses, whether or not the stream is running. /joy and /bat exist only
// when the base answered, so they stay silent otherwise -- the hello did not
// announce them; /btn always has the front button to report.
static void routeJoy(OSCMessage &) { if (!joyOK) return; OSCMessage r("/joy"); fillJoy(r); sendMessage(r); }
static void routeBtn(OSCMessage &) {                     OSCMessage r("/btn"); fillBtn(r); sendMessage(r); }
static void routeBat(OSCMessage &) { if (!joyOK) return; OSCMessage r("/bat"); fillBat(r); sendMessage(r); }

// What setup() proved, and nothing else: no booleans, and no "4 buttons" on
// a base that never answered. The front button is the AtomS3's own GPIO41,
// so it is announced even without the base; the base's four buttons, both
// sticks and both battery channels are announced only if 0x59 answered on
// one of the internal buses. Firmware version and bus are diagnostics, so
// they follow as free text in /diag rather than as fields whose positions a
// page would have to know.
static void sendEnq() {
  bundleOUT.add("/enq").add("AtomJoyOscuino");
  if (joyOK) bundleOUT.add("/enq/joy").add((intOSC_t) 4);
  bundleOUT.add("/enq/btn").add((intOSC_t) (joyOK ? 5 : 1));
  if (joyOK) bundleOUT.add("/enq/bat");
#ifdef JOY_LCD
  if (dispOK) bundleOUT.add("/enq/display").add((intOSC_t) JOY_LCD.width())
                                           .add((intOSC_t) JOY_LCD.height());
#endif
  bundleOUT.add("/enq/diag");
  flushBundle();

  // Which internal I2C bus answered also names the module (38 = AtomS3,
  // 45 = AtomS3R), and 0xFE says whether the v2 button map above applies.
  // Sentences, not fields: a page displays them and never parses them.
  OSCMessage d("/diag");
  char s[64];
  if (joyOK) {
    snprintf(s, sizeof s, "joystick 0x59 on SDA %d (%s)", (int) joySda,
             joySda == JOY_SDA_S3 ? "AtomS3" : joySda == JOY_SDA_S3R ? "AtomS3R" : "?");
    d.add(s);
    if (joyFw) snprintf(s, sizeof s, "STM32 fw v%u", (unsigned) joyFw);
    else       snprintf(s, sizeof s, "STM32 fw unread");
    d.add(s);
    if (joyFw != 2) d.add("button map here is the v2 one: verify before trusting");
  } else {
    d.add("no joystick at 0x59 on SDA 38 or SDA 45");
  }
  sendMessage(d);
}
static void routeEnq(OSCMessage &) { sendEnq(); }

void setup() {
  // USB FIRST. A sketch that cannot report its own failure is undebuggable on
  // a board with no other output, and that is exactly the hole the M5.begin()
  // hang fell into.
  SLIPSerial.begin(115200);

  pinMode(BTN_FRONT, INPUT_PULLUP);

#if ATOMJOY_USE_M5
  auto cfg = M5.config();
  M5.begin(cfg);                       // may not return on some units -- see top
  dispOK = (M5.Display.width() > 0);
#elif ATOMJOY_USE_M5GFX
  dispOK = display.begin() && display.width() > 0;
  if (dispOK) { display.setRotation(0); display.fillScreen(TFT_BLACK); }
#endif

  // Probe for the part, not for the board: this base is sold with an Atom as
  // a unit, but the module pulls out, and a driver reading an absent device
  // returns zeros that look exactly like centred sticks. Whichever bus answers
  // identifies the module at the same time.
  joyOK = joyBusBegin(JOY_SDA_S3, JOY_SCL_S3)            // AtomS3
       || joyBusBegin(JOY_SDA_S3R, JOY_SCL_S3R);         // AtomS3R

  // Which firmware answers decides whether the button mapping above is the
  // v2 one. Reported in /diag rather than assumed: a v1 base would need the
  // other pinout and a different battery divisor, so the page can say so
  // instead of drawing confident nonsense.
  if (joyOK && !joyRead(REG_FW, &joyFw, 1)) joyFw = 0;

  if (dispOK) redraw();
  sendEnq();                         // usually lost; the page asks again
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

#if ATOMJOY_USE_M5
  M5.update();                       // services M5's own button state
#endif

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/display/text", routeText);
      inMsg.dispatch("/rate",         routeRate);
      inMsg.dispatch("/joy",          routeJoy);
      inMsg.dispatch("/btn",          routeBtn);
      inMsg.dispatch("/bat",          routeBat);
      inMsg.dispatch("/enq",        routeEnq);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (!reportMs || now - last < reportMs) return;
  last = now;

  // One bundle, sampled in one pass (four I2C transactions, not ten), so
  // every reading in it belongs to the same instant. /state carries the
  // sequence number and the millis it was taken at; the rest is one message
  // per capability, never a board blob, and only the capabilities the hello
  // announced.
  bundleOUT.add("/state").add((intOSC_t) seq++).add((intOSC_t) now);
  if (joyOK) fillJoy(bundleOUT.add("/joy"));
  fillBtn(bundleOUT.add("/btn"));
  if (joyOK) fillBat(bundleOUT.add("/bat"));
  flushBundle();
}

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
//   0x00 / 0x02   left  stick X / Y, 12-bit, 2 bytes, LITTLE-endian
//   0x20 / 0x22   right stick X / Y, 12-bit, 2 bytes, little-endian
//   0x60 / 0x62   battery 1 / 2 in millivolts
//   0x70..0x73    buttons, one byte each, bit 0, ACTIVE LOW
//
// BUTTON MAPPING IS THE ONE THING TO CHECK ON THE BENCH. M5Stack's own header
// carries two contradictory mappings -- a stale set of *_ADDRESS macros and a
// newer set enabled by #define NEW_ATOM_JOY -- which disagree about whether
// 0x70 is the left shoulder or the left stick click. The newer set is used
// below because it is the one their code actually compiles, but press each
// control and confirm before trusting it.
//
// Outbound
//   /hello ,siii  name, joyOK, displayOK, buttonCount
//   /joy ,iiiiiiiii  seq, lx, ly, rx, ry (0..4095), buttons bitmask,
//                    bat1 mV, bat2 mV, frontBtn
//                    bitmask: 0 L shoulder, 1 R shoulder,
//                             2 left click, 3 right click
//                    One message, sampled in one pass, so the axes belong to
//                    the same instant; seq makes drops visible.
// Inbound
//   /joy/screen ,s...   up to five lines on the 128x128 LCD
//   /joy/rate ,i ms     report interval, 20..2000
//   /hello              ask again: the boot one is lost to USB
//                       re-enumeration before the host opens the port
//
// STATUS: written from M5Stack's documentation and driver sources, and NOT
// yet run -- the board was not reachable when this was written. Unverified
// specifically: the button mapping above, the sticks' rest value and range
// (M5 publish no calibration; 12-bit implies 0..4095 but centre and deadband
// are unmeasured), which panel controller this unit has, and whether it is an
// AtomS3 or the newer AtomS3R. M5GFX detects the panel at runtime, so the
// display should work either way.
#include <M5Unified.h>
#include <Wire.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define JOY_ADDR   0x59
#define JOY_SDA    38          // AtomS3 internal I2C, not the Grove pins
#define JOY_SCL    39
#define JOY_HZ     400000U

#define REG_L_X    0x00        // 12-bit, little-endian, 2 bytes
#define REG_L_Y    0x02
#define REG_R_X    0x20
#define REG_R_Y    0x22
#define REG_BAT1   0x60        // millivolts
#define REG_BAT2   0x62
#define REG_BTN0   0x70        // 0x70..0x73, bit 0, active LOW

static bool     joyOK = false, dispOK = false;
static int32_t  seq = 0;
static uint32_t reportMs = 50;
static char     lines[5][22] = { "AtomJoyOscuino", "OSC over USB", "", "", "" };

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

static uint16_t joy16(uint8_t reg) {
  uint8_t b[2] = { 0, 0 };
  if (!joyRead(reg, b, 2)) return 0;
  return (uint16_t)(b[0] | (b[1] << 8));      // little-endian
}

static uint8_t joyButtons() {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t b = 0xFF;
    if (joyRead((uint8_t)(REG_BTN0 + i), &b, 1))
      if (!(b & 0x01)) mask |= (uint8_t)(1 << i);   // active LOW
  }
  return mask;
}

static void redraw() {
  if (!dispOK) return;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  for (uint8_t i = 0; i < 5; i++) {
    M5.Display.setCursor(2, (int)(4 + i * 12));
    M5.Display.print(lines[i]);
  }
}

/* ----------------------------------------------------------------- inbound */

static void routeScreen(OSCMessage &m) {
  for (uint8_t i = 0; i < 5; i++) lines[i][0] = '\0';
  const int n = m.size() < 5 ? m.size() : 5;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  redraw();
}

static void routeRate(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) reportMs = constrain(m.getInt(0), 20, 2000);
}

static void sendHello() {
  OSCMessage h("/hello");
  h.add("AtomJoyOscuino").add(joyOK).add(dispOK).add((intOSC_t) 4);
  SLIPSerial.beginPacket(); h.send(SLIPSerial); SLIPSerial.endPacket();
}
static void routeHello(OSCMessage &) { sendHello(); }

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);                       // display + the GPIO41 front button
  dispOK = (M5.Display.width() > 0);

  SLIPSerial.begin(115200);

  // The joystick MCU is on the internal bus, so the pins are explicit.
  Wire.begin(JOY_SDA, JOY_SCL, JOY_HZ);

  // Probe before trusting anything: this base is sold with the AtomS3 as a
  // unit, but the Atom can be pulled out, and a driver reading an absent
  // device returns zeros that look exactly like centred sticks.
  Wire.beginTransmission(JOY_ADDR);
  joyOK = (Wire.endTransmission() == 0);

  if (dispOK) redraw();
  sendHello();                         // usually lost; the page asks again
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

  M5.update();                         // services the front button

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/joy/screen", routeScreen);
      inMsg.dispatch("/joy/rate",   routeRate);
      inMsg.dispatch("/hello",      routeHello);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (now - last < reportMs) return;
  last = now;

  uint16_t lx = 0, ly = 0, rx = 0, ry = 0, b1 = 0, b2 = 0;
  uint8_t  btn = 0;
  if (joyOK) {
    lx = joy16(REG_L_X); ly = joy16(REG_L_Y);
    rx = joy16(REG_R_X); ry = joy16(REG_R_Y);
    b1 = joy16(REG_BAT1); b2 = joy16(REG_BAT2);
    btn = joyButtons();
  }

  OSCMessage m("/joy");
  m.add((intOSC_t) seq++)
   .add((intOSC_t) lx).add((intOSC_t) ly)
   .add((intOSC_t) rx).add((intOSC_t) ry)
   .add((intOSC_t) btn)
   .add((intOSC_t) b1).add((intOSC_t) b2)
   .add((intOSC_t) (M5.BtnA.isPressed() ? 1 : 0));
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

// Seeed XIAO ESP32-C6 on the XIAO Expansion Board: OLED, buzzer and button
// over OSC on USB serial.
//
//   http://localhost/XiaoC6ExpOscuino.html   (Web Serial; not file://)
//
// There is a WiFi twin of this sketch, XiaoC6ExpWiFi, speaking the same OSC
// vocabulary over UDP instead of SLIP-on-serial. Everything below about the
// hardware applies to both; only the transport differs.
//
// FQBN: esp32:esp32:XIAO_ESP32C6 with STOCK DEFAULTS. Do not add
// :CDCOnBoot=cdc here. The generic esp32c6 devkit needs it because its
// boards.txt has build.cdc_on_boot=0, but this variant already sets
// cdc_on_boot=1, and carrying another board's options over is exactly how
// an ESP32 ends up flashing successfully and then saying nothing at all.
//
// Peripherals, verified by scanning the actual bus rather than trusting a
// wiki: I2C at SDA=GPIO22 (D4) and SCL=GPIO23 (D5) answers at 0x3C for the
// SSD1306 OLED, 0x51 for the PCF8563 real-time clock and 0x57 for the
// EEPROM alongside it. The buzzer is D3 (GPIO21) and the user button is D1
// (GPIO1), active LOW, so it wants INPUT_PULLUP.
//
// Inbound
//   /disp/text ,s...   up to four lines; each string is one row
//   /disp/big ,s       one large-font line, centred
//   /disp/clear
//   /disp/invert ,i 0|1
//   /buzz ,ii freq ms  passive buzzer via tone(); freq 0 stops it
//   /led ,i 0|1        the XIAO's own LED (GPIO15)
//   /rate ,i ms        state reporting interval, 20..2000
//   /hello             ask for /hello again -- see below, this matters
// Outbound
//   /hello ,siii  name, displayOK, rtcOK, ledPin
//   /xc6 ,iiii    seq, button (1 = pressed), millis, buzzing
//
// The sequence counter is what makes drops visible; without it a gap in the
// stream is indistinguishable from a board that simply went quiet.
//
// /hello IS ALSO AN INBOUND ADDRESS, and on a native-USB board it has to be.
// setup() sends one, but nothing is listening yet: the board resets, the USB
// device re-enumerates, and the host only opens the port some hundreds of
// milliseconds later. Anything written before that is discarded -- on the
// ESP32's HWCDC the transmit path simply drops when no host is attached.
// Measured here: the boot /hello was never once seen by a probe that opened
// the port straight after flashing. So the page asks for it on connect
// instead of hoping to catch it, and any client should do the same.
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define OLED_W    128
#define OLED_H    64
#define OLED_ADDR 0x3C          // scanned, not assumed
#define PIN_BUZZ  D3            // GPIO21
#define PIN_BTN   D1            // GPIO1, active LOW
#define RTC_ADDR  0x51          // PCF8563, scanned

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);   // -1: no reset pin wired

static bool     displayOK = false;
static bool     rtcOK     = false;
static int32_t  seq       = 0;
static uint32_t reportMs  = 50;
static uint32_t buzzUntil = 0;         // 0 = silent; else millis() deadline
static char     lines[4][22] = { "XIAO ESP32-C6", "expansion board", "", "" };
static char     bigLine[12]  = "";
static bool     bigMode   = false;

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void redraw() {
  if (!displayOK) return;
  oled.clearDisplay();
  if (bigMode) {
    oled.setTextSize(2);
    // 12 px per character at size 2; centre what will fit
    const int16_t w = (int16_t) strlen(bigLine) * 12;
    oled.setCursor(w < OLED_W ? (OLED_W - w) / 2 : 0, (OLED_H - 16) / 2);
    oled.print(bigLine);
  } else {
    oled.setTextSize(1);
    for (uint8_t i = 0; i < 4; i++) {
      oled.setCursor(0, (int16_t)(i * 10));
      oled.print(lines[i]);
    }
  }
  oled.display();
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  const int n = m.size() < 4 ? m.size() : 4;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  bigMode = false;
  redraw();
}

static void routeBig(OSCMessage &m) {
  if (m.size() < 1 || !m.isString(0)) return;
  m.getString(0, bigLine, sizeof bigLine);
  bigMode = true;
  redraw();
}

static void routeClear(OSCMessage &) {
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  bigLine[0] = '\0';
  bigMode = false;
  redraw();
}

static void routeInvert(OSCMessage &m) {
  if (displayOK && m.size() >= 1 && m.isInt(0)) oled.invertDisplay(m.getInt(0) != 0);
}

static void routeBuzz(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t freq = m.getInt(0);
  const int32_t ms   = (m.size() > 1 && m.isInt(1)) ? m.getInt(1) : 150;
  if (freq <= 0) { noTone(PIN_BUZZ); buzzUntil = 0; return; }
  tone(PIN_BUZZ, (unsigned int) freq);
  buzzUntil = millis() + (uint32_t) constrain(ms, 10, 5000);
}

static void routeLed(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) digitalWrite(LED_BUILTIN, m.getInt(0) ? LOW : HIGH);
}

static void routeRate(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) reportMs = constrain(m.getInt(0), 20, 2000);
}

static void sendHello() {
  OSCMessage hello("/hello");
  hello.add("XiaoC6ExpOscuino").add(displayOK).add(rtcOK)
       .add((intOSC_t) LED_BUILTIN);
  SLIPSerial.beginPacket(); hello.send(SLIPSerial); SLIPSerial.endPacket();
}

static void routeHello(OSCMessage &) { sendHello(); }

void setup() {
  SLIPSerial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);          // the XIAO LED is active LOW

  Wire.begin();
  // Probe the bus before trusting begin(): SSD1306's begin() can report
  // success paths that do not mean a panel is really there, and this board
  // is sold both with and without the expansion base.
  displayOK = i2cPresent(OLED_ADDR) && oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  rtcOK     = i2cPresent(RTC_ADDR);

  if (displayOK) {
    oled.setTextColor(SSD1306_WHITE);
    oled.cp437(true);
    redraw();
  }

  sendHello();          // usually lost -- see the note above; ask for it
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
  static uint32_t lastReport = 0;

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/disp/text",   routeText);
      inMsg.dispatch("/disp/big",    routeBig);
      inMsg.dispatch("/disp/clear",  routeClear);
      inMsg.dispatch("/disp/invert", routeInvert);
      inMsg.dispatch("/buzz",        routeBuzz);
      inMsg.dispatch("/led",         routeLed);
      inMsg.dispatch("/rate",        routeRate);
      inMsg.dispatch("/hello",       routeHello);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();

  if (buzzUntil && now >= buzzUntil) { noTone(PIN_BUZZ); buzzUntil = 0; }

  if (now - lastReport >= reportMs) {
    lastReport = now;
    OSCMessage m("/xc6");
    m.add((intOSC_t) seq++)
     .add((intOSC_t) (digitalRead(PIN_BTN) == LOW ? 1 : 0))
     .add((intOSC_t) now)
     .add((intOSC_t) (buzzUntil ? 1 : 0));
    SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  }
}

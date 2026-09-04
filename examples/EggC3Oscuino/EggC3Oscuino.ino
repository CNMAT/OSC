/*
 * EggC3Oscuino — Oscuino over SLIP-encoded USB serial, for the EGG
 * ESP32-C3 board with the 0.42" OLED, hand-written like the other demos.
 * -----------------------------------------------------------------------------
 * Board : ESP32-C3 SuperMini (hardware marking 466ab), the generic C3 board
 *         also sold under seller names such as "EGG" — which is where this
 *         example's folder name comes from. Single core 160 MHz, 4 MB flash.
 *         Two units measured: one carrying the 0.42" OLED this sketch draws
 *         on, one bare. The bare one still serves everything else, because
 *         the display is probed by signal rather than assumed.
 * FQBN  : esp32:esp32:esp32c3:CDCOnBoot=cdc     <- the option is REQUIRED:
 *         without it Serial is UART0 and the board flashes, verifies and
 *         says nothing (the BRINGUP.md C3 trap).
 * Libs  : U8g2 (the display driver with a dedicated 72x40 constructor)
 * Page  : EggC3Oscuino.html, generated beside this sketch by extras/webserial
 *         (extras/webserial/oscuino.html is the same page for any board)
 *
 * Everything hardware below is what the bringup sweep MEASURED on the board
 * (2026-08-30): one I2C device, address 0x3C, on SDA=GPIO5 / SCL=GPIO6 —
 * the SSD1306-class 0.42" 72x40 OLED. The BOOT button is GPIO9 (also the
 * download strap; fine as an input at runtime). The board LED is a plain
 * ACTIVE-LOW LED on GPIO8, found by a sweep that announced each candidate
 * pin and level on the OLED until human eyes caught it lighting — the
 * silkscreen was too poor to read, and the first guess (a WS2812 on GPIO2)
 * was wrong.
 *
 * FLASHING (measured, in BRINGUP.md): esptool's post-flash "hard reset" is
 * a no-op on this board's bare USB-Serial-JTAG — the chip stays parked in
 * download mode and the app never starts. `esptool run` after the upload
 * boots it; so does a physical replug. To reach download mode manually,
 * hold BOOT (GPIO9) while plugging in.
 *
 * ADDRESSES — the standard Oscuino set (/d /a /tone /s, see ADDRESSES.md)
 * plus the capabilities this board announces in its /enq bundle:
 *
 *   /btn                    -> /btn <int>          (1 = BOOT pressed) [/enq/btn 1]
 *   /display/text <string>     a line on the OLED (5 lines of ~14 chars,
 *                           scrolls up; echoed as /display/text <1|0> for
 *                           display-present)                        [/enq/display 15 5]
 *
 * Nothing is named after the board; see ADDRESSES.md for why.

 * /enq answers with the capability bundle; /enq/display appears only when
 * dispOK, which is probed by signal (the 0x3C ACK) rather than by a
 * driver's begin() return.
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <Wire.h>
#include <U8g2lib.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static const unsigned long BAUD = 115200;  // ignored on native USB

// ---- EGG C3 hardware (measured pins) ---------------------------------------
static const int PIN_SDA = 5, PIN_SCL = 6;
static const int PIN_BOOT_BTN = 9;
static const int PIN_LED = 8;              // plain LED, ACTIVE LOW (measured)

U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

static bool dispOK = false;
static int32_t  seq      = 0;      // makes a dropped /state visible
static uint32_t reportMs = 50;     // same default as the WiFi twin; 0 stops

// Five lines of OLED text, scrolled up as /display/text messages arrive.
#define OLED_LINES 5
#define OLED_COLS  15
static char lines[OLED_LINES][OLED_COLS + 1];
static int nlines = 0;

static void oledRedraw() {
  if (!dispOK) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tr);
  for (int i = 0; i < nlines; i++)
    oled.drawStr(0, 8 * (i + 1) - 1, lines[i]);
  oled.sendBuffer();
}

static void oledLine(const char *s) {
  if (nlines == OLED_LINES) {
    memmove(lines[0], lines[1], sizeof(lines) - sizeof(lines[0]));
    nlines--;
  }
  strncpy(lines[nlines], s, OLED_COLS);
  lines[nlines][OLED_COLS] = '\0';
  nlines++;
  oledRedraw();
}

static OSCBundle bundleOUT;

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

static void pinAddress(char *out, const char *prefix, int pin, const char *suffix) {
  strcpy(out, prefix);
  strcat(out, numToOSCAddress(pin));
  if (suffix) strcat(out, suffix);
}

// ---- standard routes, same shapes as the generated template ----------------
void routeDigital(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < (int)NUM_DIGITAL_PINS; pin++) {
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

void routeAnalog(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < (int)NUM_ANALOG_INPUTS; pin++) {
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

void routeTone(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < (int)NUM_DIGITAL_PINS; pin++) {
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

void routeSystem(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/m", addrOffset)) bundleOUT.add("/s/m").add((intOSC_t)micros());
  if (msg.fullMatch("/d", addrOffset)) bundleOUT.add("/s/d").add((intOSC_t)NUM_DIGITAL_PINS);
  if (msg.fullMatch("/a", addrOffset)) bundleOUT.add("/s/a").add((intOSC_t)NUM_ANALOG_INPUTS);
  if (msg.fullMatch("/l", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, v > 0 ? LOW : HIGH);   // active low, measured
    bundleOUT.add("/s/l").add((intOSC_t)v);
  }
}

// ---- capability routes -------------------------------------------------------
static void addEnq() {
  bundleOUT.add("/enq").add("EggC3Oscuino");
  bundleOUT.add("/enq/btn").add((intOSC_t)1);
  if (dispOK) bundleOUT.add("/enq/display").add((intOSC_t)OLED_COLS).add((intOSC_t)OLED_LINES);
}

void routeEnq(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addEnq();
}

static void addBtn() {
  bundleOUT.add("/btn").add((intOSC_t)(digitalRead(PIN_BOOT_BTN) == LOW));
}

void routeBtn(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addBtn();
}

// /state and /rate are core (ADDRESSES.md), and this sketch answered neither
// until 2026-09-04 -- while its own WiFi twin streamed /state + /btn happily.
// The same board over two transports was speaking two vocabularies, which is
// the thing this address space exists to stop. Caught by contractprobe on a
// third C3 unit, not by reading the code.
static void addState() {
  bundleOUT.add("/state").add((intOSC_t)seq).add((intOSC_t)millis());
  addBtn();
}

void routeState(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addState();
}

void routeRate(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (msg.isInt(0)) {
    const int32_t v = msg.getInt(0);
    reportMs = (v <= 0) ? 0 : (uint32_t)constrain(v, 20, 2000);   // 0 stops
  }
  bundleOUT.add("/rate").add((intOSC_t)reportMs);
}

void routeDisplay(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/text", addrOffset) && msg.isString(0)) {
    char text[64];
    msg.getString(0, text, sizeof(text));
    oledLine(text);
    bundleOUT.add("/display/text").add((intOSC_t)(dispOK ? 1 : 0));
  }
}

// -----------------------------------------------------------------------------

void setup() {
  SLIPSerial.begin(BAUD);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  // Probe the display by signal — the 0x3C ACK — not by a begin() return.
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.beginTransmission(0x3C);
  dispOK = (Wire.endTransmission() == 0);
  Wire.end();
  if (dispOK) {
    oled.begin();
    oledLine("EggC3Oscuino");
    oledLine("OSC/SLIP/USB");
  }

  delay(300);
  addEnq();
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

static bool pollOSC(OSCBundle &bundleIN) {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t)c);
    }
  }
  return true;
}

static OSCBundle bundleIN;

void loop() {
  if (pollOSC(bundleIN)) {
    if (!bundleIN.hasError()) {
      bundleIN.route("/d", routeDigital);
      bundleIN.route("/a", routeAnalog);
      bundleIN.route("/tone", routeTone);
      bundleIN.route("/s", routeSystem);
      bundleIN.route("/enq", routeEnq);
      bundleIN.route("/btn",  routeBtn);
      bundleIN.route("/state", routeState);
      bundleIN.route("/rate",  routeRate);
      bundleIN.route("/display", routeDisplay);
    }
    bundleIN.empty();
  }

  static uint32_t lastReport = 0;
  const uint32_t now = millis();
  if (reportMs != 0 && now - lastReport >= reportMs) {
    lastReport = now;
    seq++;
    addState();
  }

  if (bundleOUT.size() > 0) {
    SLIPSerial.beginPacket();
    bundleOUT.send(SLIPSerial);
    SLIPSerial.endPacket();
    bundleOUT.empty();
  }
}

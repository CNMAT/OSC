/*
 * XiaoRoundOscuino — Oscuino over SLIP-encoded USB serial for a Seeed XIAO
 * on the Round Display for XIAO (GC9A01 240x240 round LCD, CHSC6X touch,
 * BM8563 RTC, SD, backlight switch), hand-written like the other demos.
 * -----------------------------------------------------------------------------
 * Board : any XIAO on the Round Display; brought up and verified on the
 *         original Seeeduino XIAO (SAMD21G18, VID 0x2886 PID 0x802F)
 * FQBN  : Seeeduino:samd:seeed_XIAO_m0   (for that XIAO)
 * Libs  : "Adafruit GC9A01A" (+GFX) and "I2C BM8563 RTC", both from the
 *         library index. Pin map per the Seeed wiki's Setup501: hardware
 *         SPI on D8/D9/D10, CS=D1, DC=D3, BL=D6, touch INT=D7, CHSC6X at
 *         I2C 0x2E, RTC at 0x51.
 *
 *         Seeed's own wiki stack (Seeed_GFX / LVGL) is NOT used: the wiki
 *         itself lists the XIAO SAMD21 as "may not be compatible ... due to
 *         insufficient memory", and on the bench their fork lit the
 *         backlight but never a pixel. The memory warning indicts LVGL's
 *         framebuffers, not the panel — Adafruit's direct-draw GC9A01A
 *         driver runs it fine from a 32 KB SAMD21 (measured). Touch is a
 *         direct 5-byte I2C read, the protocol from Seeed's own
 *         lv_xiao_round_screen.h.
 *
 * ADDRESSES — the standard Oscuino set (/d /a /tone /s, see template.ino)
 * plus the round display:
 *
 *   /rd/t <s> [...]     up to 4 strings, centered on the circle
 *   /rd/fill <r> <g> <b>  flood the screen, 0..255 each
 *   /rd/rtc             -> /rd/rtc <yy> <mo> <dd> <hh> <mm> <ss>
 *   /rd/rtc <6 ints>    sets the RTC (year month day hour min sec)
 *   /rd/rate <ms>       touch-poll pacing, 10..500 (default 30)
 *
 * Touch is streamed, not polled: while a finger is down the sketch sends
 *   /rd/touch <x> <y>   (240x240 coordinates, paced by /rd/rate)
 * and draws a dot at the point, so the finger paints on the glass while
 * the page mirrors it.
 *
 * /hello carries touchOK and rtcOK, probed by I2C ACK at 0x2E and 0x51.
 * touchOK deserves a caveat the hardware taught: the CHSC6X only answers
 * I2C while a finger is on the glass, so at boot it reads absent even when
 * present — the touch stream therefore never gates on it, and rtcOK (0x51,
 * always awake) is the better board-attached signal.
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <I2C_BM8563.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static const unsigned long BAUD = 115200;  // ignored on native USB

static const int PIN_CS = D1, PIN_DC = D3, PIN_BL = D6, PIN_TOUCH_INT = D7;
Adafruit_GC9A01A tft(&SPI, PIN_DC, PIN_CS);   // hardware SPI on D8/D9/D10
I2C_BM8563 rtc(I2C_BM8563_DEFAULT_ADDRESS, Wire);

// Center a string at (120, y) — Adafruit_GFX has no text datum.
static void drawCentered(const char *s, int y, int size) {
  int16_t bx, by; uint16_t bw, bh;
  tft.setTextSize(size);
  tft.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  tft.setCursor(120 - bw / 2, y);
  tft.print(s);
}

// CHSC6X touch, the 5-byte protocol from Seeed's lv_xiao_round_screen.h:
// INT low while touched (the chip only answers I2C then), byte0==0x01 marks
// a valid point, x=byte2, y=byte4 (240x240, rotation 0).
static bool readTouch(int *x, int *y) {
  if (digitalRead(PIN_TOUCH_INT) != LOW) {
    delay(1);
    if (digitalRead(PIN_TOUCH_INT) != LOW) return false;
  }
  uint8_t t[5] = {0};
  if (Wire.requestFrom((uint8_t)0x2E, (uint8_t)5) != 5) return false;
  Wire.readBytes(t, 5);
  if (t[0] != 0x01) return false;
  *x = t[2];
  *y = t[4];
  return true;
}

static bool touchOK = false, rtcOK = false;
static uint32_t touchMs = 30;

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

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
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
#ifdef BOARD_HAS_LED
  if (msg.fullMatch("/l", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(LED_BUILTIN, OUTPUT);
    // The XIAO M0's user LED is active LOW (its variant wires it that way).
    digitalWrite(LED_BUILTIN, v > 0 ? LOW : HIGH);
    bundleOUT.add("/s/l").add((intOSC_t)v);
  }
#endif
}

// ---- round display routes --------------------------------------------------
void routeRound(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/t", addrOffset)) {
    tft.fillScreen(GC9A01A_BLACK);
    tft.setTextColor(GC9A01A_WHITE);
    const int n = msg.size() < 4 ? msg.size() : 4;
    for (int i = 0; i < n; i++) {
      if (!msg.isString(i)) continue;
      char line[24];
      msg.getString(i, line, sizeof line);
      drawCentered(line, 120 - 14 * n + 28 * i, 2);
    }
    bundleOUT.add("/rd/t").add((intOSC_t)n);
    return;
  }
  if (msg.fullMatch("/fill", addrOffset) &&
      msg.isInt(0) && msg.isInt(1) && msg.isInt(2)) {
    tft.fillScreen(tft.color565(msg.getInt(0), msg.getInt(1), msg.getInt(2)));
    bundleOUT.add("/rd/fill")
        .add((intOSC_t)msg.getInt(0)).add((intOSC_t)msg.getInt(1))
        .add((intOSC_t)msg.getInt(2));
    return;
  }
  if (msg.fullMatch("/rtc", addrOffset)) {
    if (msg.isInt(0) && msg.isInt(5)) {      // six ints: set
      I2C_BM8563_DateTypeDef d;
      I2C_BM8563_TimeTypeDef t;
      d.year = msg.getInt(0); d.month = msg.getInt(1); d.date = msg.getInt(2);
      t.hours = msg.getInt(3); t.minutes = msg.getInt(4); t.seconds = msg.getInt(5);
      rtc.setDate(&d);
      rtc.setTime(&t);
    }
    I2C_BM8563_DateTypeDef d;
    I2C_BM8563_TimeTypeDef t;
    rtc.getDate(&d);
    rtc.getTime(&t);
    bundleOUT.add("/rd/rtc")
        .add((intOSC_t)d.year).add((intOSC_t)d.month).add((intOSC_t)d.date)
        .add((intOSC_t)t.hours).add((intOSC_t)t.minutes).add((intOSC_t)t.seconds);
    return;
  }
  if (msg.fullMatch("/rate", addrOffset) && msg.isInt(0)) {
    touchMs = constrain(msg.getInt(0), 10, 500);
    return;
  }
}

// -----------------------------------------------------------------------------

void setup() {
  SLIPSerial.begin(BAUD);

  Wire.begin();
  touchOK = i2cPresent(0x2E);                // CHSC6X
  rtcOK   = i2cPresent(0x51);                // BM8563
  if (rtcOK) rtc.begin();

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);                // backlight (the switch can veto)
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(GC9A01A_BLACK);
  tft.setTextColor(GC9A01A_WHITE);
  drawCentered("XiaoRound", 100, 3);
  drawCentered("OSC/SLIP/USB", 136, 2);

  delay(300);
  bundleOUT.add("/hello").add("XiaoRoundOscuino")
      .add((intOSC_t)touchOK).add((intOSC_t)rtcOK);
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
      bundleIN.route("/rd", routeRound);
    }
    bundleIN.empty();
  }

  // Touch stream: while a finger is down, send paced /rd/touch and paint.
  // NOT gated on the boot-time I2C probe: the CHSC6X only acknowledges I2C
  // while a touch is active (measured — 0x2E was silent at boot with the
  // panel attached), so a boot ACK can never be required. The fork's
  // getTouch() checks the INT line first, exactly as Seeed's own driver does.
  static uint32_t lastTouch = 0;
  if (millis() - lastTouch >= touchMs) {
    lastTouch = millis();
    int x = 0, y = 0;
    if (readTouch(&x, &y)) {
      tft.fillCircle(x, y, 3, GC9A01A_CYAN);
      bundleOUT.add("/rd/touch").add((intOSC_t)x).add((intOSC_t)y);
    }
  }

  if (bundleOUT.size() > 0) {
    SLIPSerial.beginPacket();
    bundleOUT.send(SLIPSerial);
    SLIPSerial.endPacket();
    bundleOUT.empty();
  }
}

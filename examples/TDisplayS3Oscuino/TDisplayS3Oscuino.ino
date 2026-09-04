// LilyGO T-Display-S3 Touch: the 320x170 screen drawn by OSC, and every touch
// reported back out.
//
//   http://localhost/TDisplayS3Oscuino.html   (Web Serial; not file://)
//
// FQBN: esp32:esp32:lilygo_t_display_s3 with STOCK DEFAULTS -- the core ships
// a real board definition (PSRAM, flash, USB are hardwired in it), so unlike
// the generic ESP32S3 Dev Module route there is nothing to configure and
// nothing to get silently wrong.
//
// THE PANEL IS NOT SPI. It is an ST7789V on an 8-bit Intel-8080 parallel bus,
// fixed by the display module itself. Drivers configured for SPI ST7789 will
// never light it. It is driven here through M5GFX, which vendors the full
// LovyanGFX engine including Bus_Parallel8 on the S3's LCD_CAM peripheral --
// already installed, no library files to hand-edit (TFT_eSPI's setup-file
// route silently reverts on every library update). M5GFX's autodetect only
// knows M5Stack hardware, so the panel is described explicitly below.
//
// Pin facts, each verified against BOTH LilyGO's pin_config.h and the
// schematic (independent derivations that agree):
//   data D0..D7 = 39 40 41 42 45 46 47 48, WR=8, RD=9, DC=7, CS=6, RST=5
//   backlight = 38 (EN of an AW9364 boost driver, PWM-able)
//   GPIO15 = PWR_EN, gating the V3V rail that feeds panel AND backlight.
//     On USB power the rail sneaks on through a diode, so forgetting GPIO15
//     works on the bench and fails on battery -- the classic blank-screen
//     report. It is driven HIGH here as the first statement of setup().
//   RD is idled HIGH; D4/D5 land on strapping pins GPIO45/46, which is fine
//     at runtime but is why a crashed image can wedge enumeration until the
//     BOOT+RESET sequence.
//   The controller's frame memory is 240x320; the glass shows 170x320 with a
//     35-column offset ((240-170)/2), which Panel_ST7789 handles as offset_x.
//
// TOUCH, measured on this unit before anything was written: an I2C scan of
// SDA=18/SCL=17 answers at 0x15 -- the CST816 family -- but ONLY after a
// reset pulse on GPIO21. Without that pulse the controller sits silent and
// the board is indistinguishable from the non-touch variant; a deep-sleep
// sketch can even hold GPIO21 low ACROSS reset, so absence of touch is never
// trusted until the reset has been tried. Boards also ship with a CST328 at
// 0x1A speaking a different protocol; this sketch probes both addresses and
// announces /enq/touch in the /enq bundle only if one of them answered,
// naming the part in a /diag line, rather than assuming.
//
// The CST816 is polled (the INT line on GPIO16 is wired but polling keeps
// loop() single-pathed); polling requires disabling its auto-sleep by writing
// 0x01 to register 0xFE, or reads start failing once it dozes.
//
// TOUCH COORDINATES arrive in portrait (x 0..169, y 0..319). LilyGO's own
// examples disagree with each other about the landscape transform, so it is
// settable at runtime instead of baked in: /touch/map swap mirX mirY. The
// default is swap+mirror-Y, MEASURED on this unit -- LilyGO's newest example
// says swap+mirror-X, which landed the dot at 180 degrees from the finger. The sketch draws a
// dot on the LCD where it believes the touch is -- if the dot tracks your
// finger the map is right, and that check takes one second instead of an
// argument with documentation.
//
// Inbound (the ADDRESSES.md vocabulary)
//   /display/text ,s...     up to 5 lines of text
//   /display/big ,s         one large centred line
//   /display/fill ,iii      r g b background fill
//   /display/rect ,iiiiiii  x y w h r g b (filled)
//   /display/circle ,iiiiii x y radius r g b (filled)
//   /display/bl ,i          backlight 0..255
//   /display/clear
//   /touch/map ,iii         swapXY mirrorX mirrorY (0|1 each)
//   /rate ,i                touch stream pacing ms, 10..500; 0 stops; echoed
//   /enq                  ask again -- the boot one is lost to enumeration
// Outbound
//   /enq bundle   /enq ,s "TDisplayS3Oscuino", then one /enq per thing
//                   that is actually here: /enq/display ,ii 320 170 if the
//                   panel initialised, /enq/touch ,ii 320 170 if a touch
//                   controller answered the probe, /enq/diag always. What
//                   failed to initialise is simply not listed.
//   /diag ,s        free text sent right after the hello: which touch part
//                   answered, from the CST816 0xA7 id (0xB4=CST816S,
//                   0xB5=CST816T, 0xB7=CST820), "CST328" if that part
//                   answered at 0x1A, "absent" if nothing did
//   touch stream    a bundle of /state ,ii seq millis plus /touch ,iii x y
//                   gesture (x/y already through the landscape map) while a
//                   finger is down. A press or release always sends; moves
//                   are paced by /rate. The release bundle carries /state
//                   alone: no /touch in it means no finger.
//
// STATUS: run on the board 2026-08-18 (commit 355e249): after flash the
// old-shape hello answered with display and touch present, 320x170, touch
// chip id 0xB5 (CST816T), and the board's dot tracked the finger under the
// swap+mirror-Y default with no page attached. Addresses renamed onto
// ADDRESSES.md on 2026-09-03 (/disp/text -> /display/text, /disp/big ->
// /display/big, /disp/fill -> /display/fill, /disp/rect -> /display/rect,
// /disp/circle -> /display/circle, /disp/bl -> /display/bl, /disp/clear ->
// /display/clear; the /enq ,sTTiiii flag list -> a /enq + /enq/display +
// /enq/touch + /enq/diag bundle with the chip id moved into /diag; the
// /touch ,iiiii seq/down/x/y/gesture message -> a /state + /touch bundle;
// /rate now echoed, and 0 stops); that build is compile-checked and has not
// been re-run on the board.
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>   // M5GFX.h includes no panel header
#include <Wire.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define PIN_POWER  15
#define PIN_BL     38
#define TP_SDA     18
#define TP_SCL     17
#define TP_RST     21
#define CST816_ADDR 0x15
#define CST328_ADDR 0x1A

class TDisplayS3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_Parallel8 _bus;
  lgfx::Light_PWM     _light;
public:
  TDisplayS3() {
    { auto cfg = _bus.config();
      cfg.freq_write = 16000000;          // LilyGO's own clock; faster tears
      cfg.pin_wr = 8; cfg.pin_rd = 9; cfg.pin_rs = 7;
      cfg.pin_d0 = 39; cfg.pin_d1 = 40; cfg.pin_d2 = 41; cfg.pin_d3 = 42;
      cfg.pin_d4 = 45; cfg.pin_d5 = 46; cfg.pin_d6 = 47; cfg.pin_d7 = 48;
      _bus.config(cfg); _panel.setBus(&_bus); }
    { auto cfg = _panel.config();
      cfg.pin_cs = 6; cfg.pin_rst = 5; cfg.pin_busy = -1;
      cfg.panel_width = 170; cfg.panel_height = 320;
      cfg.offset_x = 35; cfg.offset_y = 0;
      cfg.invert = true;                  // this glass runs inverted
      cfg.rgb_order = false; cfg.readable = false; cfg.bus_shared = false;
      _panel.config(cfg); }
    { auto cfg = _light.config();
      cfg.pin_bl = PIN_BL; cfg.freq = 12000; cfg.pwm_channel = 7;
      _light.config(cfg); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

static TDisplayS3 lcd;

static bool     dispOK = false, touchOK = false;
static uint8_t  touchChip = 0;            // 0xA7 chip id, 0x1A for CST328, 0
static bool     touchIs328 = false;
static int32_t  seq = 0;
static uint32_t reportMs = 30;
// Default measured on this unit 2026-08-18, confirmed by the dot tracking
// the finger: swap + mirror-Y. LilyGO's newest example says swap+mirror-X,
// which put the dot at 180 degrees from the finger on this glass -- their
// examples disagree with each other, which is why this is runtime-settable.
static bool     mapSwap = true, mapMirX = false, mapMirY = true;
static char     lines[5][30] = { "TDisplayS3Oscuino", "OSC over USB", "", "", "" };
static char     bigLine[16] = "";
static bool     bigMode = false;

/* ------------------------------------------------------------------- touch */

static bool tpWrite(uint8_t addr, const uint8_t *b, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(b, n);
  return Wire.endTransmission() == 0;
}
static bool tpRead(uint8_t addr, uint8_t reg, uint8_t *b, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int) addr, (int) n) != n) return false;
  for (uint8_t i = 0; i < n; i++) b[i] = Wire.read();
  return true;
}

static void touchBegin() {
  pinMode(TP_RST, OUTPUT);                 // the pulse is NOT optional: without
  digitalWrite(TP_RST, LOW);  delay(30);   // it the controller never answers
  digitalWrite(TP_RST, HIGH); delay(220);  // and looks unfitted (measured)
  Wire.begin(TP_SDA, TP_SCL, 400000);

  Wire.beginTransmission(CST816_ADDR);
  if (Wire.endTransmission() == 0) {
    touchOK = true;
    uint8_t id = 0;
    if (tpRead(CST816_ADDR, 0xA7, &id, 1)) touchChip = id;
    const uint8_t nosleep[2] = { 0xFE, 0x01 };   // we poll; stop it dozing
    tpWrite(CST816_ADDR, nosleep, 2);
    return;
  }
  Wire.beginTransmission(CST328_ADDR);     // the other part this board ships
  if (Wire.endTransmission() == 0) {       // with; different protocol
    touchOK = true; touchIs328 = true; touchChip = CST328_ADDR;
  }
}

// One poll: fills x/y (panel-portrait), returns finger count. CST816 report:
// 13 bytes from 0x00 -- [1]=gesture, [2]&0x0F=fingers, 12-bit big-endian
// nibble-split coordinates in [3..6].
static uint8_t touchPoll(int16_t &x, int16_t &y, uint8_t &gesture) {
  if (!touchOK) return 0;
  if (!touchIs328) {
    uint8_t b[7];
    if (!tpRead(CST816_ADDR, 0x00, b, 7)) return 0;
    const uint8_t n = b[2] & 0x0F;
    if (n) {
      x = (int16_t)(((b[3] & 0x0F) << 8) | b[4]);
      y = (int16_t)(((b[5] & 0x0F) << 8) | b[6]);
      gesture = b[1];
    }
    return n;
  }
  // CST328: 16-bit registers; first point at 0xD000, count at 0xD005 low bits
  uint8_t cmd[2] = { 0xD0, 0x00 };
  Wire.beginTransmission(CST328_ADDR);
  Wire.write(cmd, 2);
  if (Wire.endTransmission(false) != 0) return 0;
  uint8_t b[7];
  if (Wire.requestFrom((int) CST328_ADDR, 7) != 7) return 0;
  for (uint8_t i = 0; i < 7; i++) b[i] = Wire.read();
  const uint8_t n = b[5] & 0x0F;
  if (n) {
    x = (int16_t)((b[1] << 4) | (b[3] >> 4));
    y = (int16_t)((b[2] << 4) | (b[3] & 0x0F));
    gesture = 0;
  }
  const uint8_t sync[3] = { 0xD0, 0x05, 0xAB };  // required after each read
  tpWrite(CST328_ADDR, sync, 3);
  return n;
}

// Portrait -> landscape, settable because LilyGO's own examples disagree.
static void mapTouch(int16_t &x, int16_t &y) {
  int16_t px = x, py = y;
  if (mapSwap) { int16_t t = px; px = py; py = t; }
  if (mapMirX) px = (int16_t)(lcd.width()  - 1 - px);
  if (mapMirY) py = (int16_t)(lcd.height() - 1 - py);
  x = constrain(px, 0, lcd.width() - 1);
  y = constrain(py, 0, lcd.height() - 1);
}

/* ------------------------------------------------------------------ screen */

static void redraw() {
  if (!dispOK) return;
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  if (bigMode) {
    lcd.setTextSize(4);
    lcd.setCursor((lcd.width() - (int) strlen(bigLine) * 24) / 2, lcd.height() / 2 - 16);
    lcd.print(bigLine);
  } else {
    lcd.setTextSize(2);
    for (uint8_t i = 0; i < 5; i++) {
      lcd.setCursor(4, (int)(6 + i * 26));
      lcd.print(lines[i]);
    }
  }
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {
  for (uint8_t i = 0; i < 5; i++) lines[i][0] = '\0';
  const int n = m.size() < 5 ? m.size() : 5;
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
static void routeFill(OSCMessage &m) {
  if (!dispOK || m.size() < 3) return;
  lcd.fillScreen(lcd.color565(constrain(m.getInt(0), 0, 255),
                              constrain(m.getInt(1), 0, 255),
                              constrain(m.getInt(2), 0, 255)));
}
static void routeRect(OSCMessage &m) {
  if (!dispOK || m.size() < 7) return;
  lcd.fillRect(m.getInt(0), m.getInt(1), m.getInt(2), m.getInt(3),
               lcd.color565(constrain(m.getInt(4), 0, 255),
                            constrain(m.getInt(5), 0, 255),
                            constrain(m.getInt(6), 0, 255)));
}
static void routeCircle(OSCMessage &m) {
  if (!dispOK || m.size() < 6) return;
  lcd.fillCircle(m.getInt(0), m.getInt(1), m.getInt(2),
                 lcd.color565(constrain(m.getInt(3), 0, 255),
                              constrain(m.getInt(4), 0, 255),
                              constrain(m.getInt(5), 0, 255)));
}
static void routeBl(OSCMessage &m) {
  if (dispOK && m.size() >= 1 && m.isInt(0))
    lcd.setBrightness((uint8_t) constrain(m.getInt(0), 0, 255));
}
static void routeClear(OSCMessage &) {
  if (dispOK) lcd.fillScreen(TFT_BLACK);
}
static void routeMap(OSCMessage &m) {
  if (m.size() >= 3) {
    mapSwap = m.getInt(0) != 0; mapMirX = m.getInt(1) != 0; mapMirY = m.getInt(2) != 0;
  }
}
// Everything outbound goes through one file-scope bundle, emptied after each
// send, so the stream does not construct and free a bundle per report.
static OSCBundle outB;

static void sendOut() {
  SLIPSerial.beginPacket(); outB.send(SLIPSerial); SLIPSerial.endPacket();
  outB.empty();
}

static void routeRate(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t v = m.getInt(0);
  reportMs = v <= 0 ? 0 : (uint32_t) constrain(v, 10, 500);   // 0 stops the stream
  outB.add("/rate").add((intOSC_t) reportMs);                  // echoed, per the contract
  sendOut();
}

// The capability bundle of ADDRESSES.md: the name, then one /enq per thing
// that is actually here. What failed to initialise is simply not listed.
static void sendEnq() {
  outB.add("/enq").add("TDisplayS3Oscuino");
  if (dispOK)  outB.add("/enq/display").add((intOSC_t) lcd.width()).add((intOSC_t) lcd.height());
  if (touchOK) outB.add("/enq/touch").add((intOSC_t) lcd.width()).add((intOSC_t) lcd.height());
  outB.add("/enq/diag");
  sendOut();
  // Which touch part answered, as free text: the chip id has no slot in
  // /enq/touch, and a page should not have to parse it.
  char text[40];
  if (!touchOK)        snprintf(text, sizeof text, "touch absent");
  else if (touchIs328) snprintf(text, sizeof text, "touch CST328 at 0x1A");
  else                 snprintf(text, sizeof text, "touch %s id 0x%02X",
                                touchChip == 0xB4 ? "CST816S" : touchChip == 0xB5 ? "CST816T" :
                                touchChip == 0xB6 ? "CST816D" : touchChip == 0xB7 ? "CST820"  :
                                touchChip == 0x20 ? "CST716"  : "CST816-family",
                                (unsigned) touchChip);
  outB.add("/diag").add((const char *) text);
  sendOut();
}
static void routeEnq(OSCMessage &) { sendEnq(); }

void setup() {
  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, HIGH);           // FIRST: gates the V3V rail; on
                                           // battery nothing lights without it
  SLIPSerial.begin(115200);

  dispOK = lcd.init();
  if (dispOK) {
    lcd.setRotation(1);                    // 320x170 landscape, USB right
    lcd.setBrightness(180);
    redraw();
  }

  touchBegin();
  sendEnq();                             // usually lost; the page asks again
}

// Non-blocking receive, the extras/webserial/template.ino pattern:
// endofPacket() BEFORE available(); return when dry; file-scope message.
static OSCMessage inMsg;

static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) inMsg.fill((uint8_t) c);
    }
  }
  return true;
}

void loop() {
  static uint32_t lastSend = 0;
  static bool     wasDown = false;

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/display/text",   routeText);
      inMsg.dispatch("/display/big",    routeBig);
      inMsg.dispatch("/display/fill",   routeFill);
      inMsg.dispatch("/display/rect",   routeRect);
      inMsg.dispatch("/display/circle", routeCircle);
      inMsg.dispatch("/display/bl",     routeBl);
      inMsg.dispatch("/display/clear",  routeClear);
      inMsg.dispatch("/touch/map",      routeMap);
      inMsg.dispatch("/rate",           routeRate);
      inMsg.dispatch("/enq",          routeEnq);
    }
    inMsg.empty();
  }

  int16_t x = 0, y = 0; uint8_t g = 0;
  const bool down = touchPoll(x, y, g) > 0;
  const uint32_t now = millis();

  if (down) mapTouch(x, y);

  // A press or release always sends; movement is paced; /rate 0 stops it. The
  // dot on the LCD is the map check: if it tracks your finger the transform
  // is right.
  const bool edge = (down != wasDown);
  if (reportMs && (edge || (down && now - lastSend >= reportMs))) {
    if (down && dispOK) lcd.fillCircle(x, y, 3, TFT_GREEN);
    // The ADDRESSES.md stream: /state seq millis, then /touch x y gesture
    // while a finger is down. A release sends /state alone -- no /touch in
    // the bundle means no finger.
    outB.add("/state").add((intOSC_t) seq++).add((intOSC_t) now);
    if (down) outB.add("/touch").add((intOSC_t) x).add((intOSC_t) y).add((intOSC_t) g);
    sendOut();
    lastSend = now;
  }
  wasDown = down;
}

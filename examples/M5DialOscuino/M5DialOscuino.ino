// M5Stack Dial (M5Dial): the round 240x240 touch screen drawn by OSC, and the
// knob, touch, and button reported back out.
//
//   http://localhost/M5DialOscuino.html   (Web Serial; not file://)
//
// FQBN: m5stack:esp32:m5stack_dial with stock defaults. The main controller is
// a StampS3 (ESP32-S3, 8 MB embedded flash) on the back of the knob.
//
// THE FIRST STATEMENT OF setup() IS THE POWER LATCH. GPIO 46 is a hold pin:
// M5's own documentation says the program must set it HIGH after wake-up "to
// maintain power, otherwise the device will re-enter sleep mode". The factory
// firmware deep-sleeps, and a deep-sleeping S3 powers off its USB PHY -- which
// is why a fresh-from-the-box Dial looks ABSENT from the USB bus entirely and
// only enumerates for a moment after a RESET tap. That behaviour cost this
// bench a diagnosis session; the latch line below is why this sketch stays up.
//
// There IS a boot button, despite appearances: the G0 wake/user button doubles
// as the download strap ("hold the G0 button before powering on"). The same
// button is the sketch's user button, via M5.BtnA.
//
// Pin map, from M5's documentation for this product:
//   encoder A=G41 B=G40 · button G0 · buzzer G3 (M5.Speaker)
//   internal I2C: SDA G11, SCL G12 -- FT3267 touch 0x38, WS1850S RFID 0x28,
//   PCF8563 RTC 0x51 · LCD GC9A01 on SPI: DC G4, MOSI G5, SCK G6, CS G7,
//   RST G8, BL G9 · PORT.A G13/G15, PORT.B G2/G1
//
// M5Unified drives the display, touch, button and buzzer -- and on this
// product it is SAFE, which is a measured statement, not an assumption: on an
// AtomS3 in the Atom JoyStick base M5.begin() never returns, but the same
// diagnostic on a Dial completes and reports the 240x240 panel (getBoard()=12,
// board_M5Dial). The encoder is not in M5Unified; it is read here with a plain
// quadrature decoder on interrupts.
//
// RFID is NOT implemented -- the WS1850S needs its own driver -- but its
// presence is probed at 0x28 and reported in /hello, so a client knows the
// hardware is there even though this sketch does not read tags.
//
// Inbound
//   /disp/text ,s...      up to 4 lines, centred for the round face
//   /disp/big ,s          one large centred line
//   /disp/fill ,iii       r g b
//   /disp/circle ,iiiiii  x y radius r g b (filled)
//   /disp/rect ,iiiiiii   x y w h r g b (filled)
//   /disp/bl ,i           backlight 0..255
//   /disp/clear
//   /buzz ,ii             freq ms (M5.Speaker tone)
//   /enc/zero             reset the encoder position to 0
//   /rate ,i              report pacing ms, 10..500
//   /hello
// Outbound
//   /hello ,sTTTii  name, dispOK, touchOK, rfidPresent, width, height
//   /dial ,iiiiiii  seq, encPos, btn, touchDown, x, y, encDelta
//                   -- one message, sampled in one pass; a state change
//                   (press, release, touch edge, any encoder motion) always
//                   sends, otherwise paced by /rate
#include <M5Unified.h>
#include <Wire.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define PIN_HOLD  46
#define PIN_ENC_A 41
#define PIN_ENC_B 40
#define RFID_ADDR 0x28

static bool     dispOK = false, touchOK = false, rfidOK = false;
static int32_t  seq = 0;
static uint32_t reportMs = 30;

// Quadrature, interrupt-driven; the classic 4x table. volatile because the
// ISR and loop() share it.
static volatile int32_t encPos = 0;
static volatile uint8_t encPrev = 0;
static void IRAM_ATTR encISR() {
  static const int8_t T[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
  const uint8_t cur = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
  encPos += T[(encPrev << 2) | cur];
  encPrev = cur;
}

static char lines[4][26] = { "M5DialOscuino", "OSC over USB", "", "" };
static char bigLine[12] = "";
static bool bigMode = false;

static void redraw() {
  if (!dispOK) return;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  if (bigMode) {
    M5.Display.setTextSize(4);
    M5.Display.drawString(bigLine, 120, 120);
  } else {
    M5.Display.setTextSize(2);
    for (uint8_t i = 0; i < 4; i++)
      M5.Display.drawString(lines[i], 120, (int)(70 + i * 34));
  }
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
static void routeFill(OSCMessage &m) {
  if (!dispOK || m.size() < 3) return;
  M5.Display.fillScreen(M5.Display.color565(constrain(m.getInt(0), 0, 255),
                                            constrain(m.getInt(1), 0, 255),
                                            constrain(m.getInt(2), 0, 255)));
}
static void routeCircle(OSCMessage &m) {
  if (!dispOK || m.size() < 6) return;
  M5.Display.fillCircle(m.getInt(0), m.getInt(1), m.getInt(2),
                        M5.Display.color565(constrain(m.getInt(3), 0, 255),
                                            constrain(m.getInt(4), 0, 255),
                                            constrain(m.getInt(5), 0, 255)));
}
static void routeRect(OSCMessage &m) {
  if (!dispOK || m.size() < 7) return;
  M5.Display.fillRect(m.getInt(0), m.getInt(1), m.getInt(2), m.getInt(3),
                      M5.Display.color565(constrain(m.getInt(4), 0, 255),
                                          constrain(m.getInt(5), 0, 255),
                                          constrain(m.getInt(6), 0, 255)));
}
static void routeBl(OSCMessage &m) {
  if (dispOK && m.size() >= 1 && m.isInt(0))
    M5.Display.setBrightness((uint8_t) constrain(m.getInt(0), 0, 255));
}
static void routeClear(OSCMessage &) { if (dispOK) M5.Display.fillScreen(TFT_BLACK); }
static void routeBuzz(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int f  = constrain(m.getInt(0), 0, 10000);
  const int ms = (m.size() > 1 && m.isInt(1)) ? constrain(m.getInt(1), 10, 3000) : 120;
  if (f > 0) M5.Speaker.tone((float) f, (uint32_t) ms);
  else M5.Speaker.stop();
}
static void routeZero(OSCMessage &) { encPos = 0; }
static void routeRate(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) reportMs = constrain(m.getInt(0), 10, 500);
}

static void sendHello() {
  OSCMessage h("/hello");
  h.add("M5DialOscuino").add(dispOK).add(touchOK).add(rfidOK)
   .add((intOSC_t) (dispOK ? M5.Display.width() : 0))
   .add((intOSC_t) (dispOK ? M5.Display.height() : 0));
  SLIPSerial.beginPacket(); h.send(SLIPSerial); SLIPSerial.endPacket();
}
static void routeHello(OSCMessage &) { sendHello(); }

void setup() {
  pinMode(PIN_HOLD, OUTPUT);
  digitalWrite(PIN_HOLD, HIGH);          // FIRST: latch our own power, or the
                                         // Dial re-enters sleep and drops off
                                         // the USB bus entirely (documented)
  SLIPSerial.begin(115200);

  auto cfg = M5.config();
  cfg.internal_spk = true;               // the G3 buzzer via M5.Speaker
  M5.begin(cfg);
  dispOK  = (M5.Display.width() > 0);
  touchOK = (M5.Touch.isEnabled());
  if (dispOK) { M5.Display.setBrightness(150); redraw(); }

  // RFID: presence only. The WS1850S wants its own driver; a client at least
  // learns the hardware exists.
  Wire.begin(11, 12);
  Wire.beginTransmission(RFID_ADDR);
  rfidOK = (Wire.endTransmission() == 0);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  encPrev = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  sendHello();                           // usually lost; the page asks again
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
  static int32_t  lastPos = 0;
  static bool     wasDown = false, wasBtn = false;

  M5.update();                           // services BtnA and Touch

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/disp/text",   routeText);
      inMsg.dispatch("/disp/big",    routeBig);
      inMsg.dispatch("/disp/fill",   routeFill);
      inMsg.dispatch("/disp/circle", routeCircle);
      inMsg.dispatch("/disp/rect",   routeRect);
      inMsg.dispatch("/disp/bl",     routeBl);
      inMsg.dispatch("/disp/clear",  routeClear);
      inMsg.dispatch("/buzz",        routeBuzz);
      inMsg.dispatch("/enc/zero",    routeZero);
      inMsg.dispatch("/rate",        routeRate);
      inMsg.dispatch("/hello",       routeHello);
    }
    inMsg.empty();
  }

  const int32_t pos = encPos;
  const bool    btn = M5.BtnA.isPressed();
  const auto    t   = M5.Touch.getDetail();
  const bool    down = touchOK && t.isPressed();
  int16_t x = down ? t.x : -1, y = down ? t.y : -1;

  const uint32_t now = millis();
  const bool edge = (down != wasDown) || (btn != wasBtn) || (pos != lastPos);
  if (edge || now - lastSend >= reportMs) {
    if (down && dispOK) M5.Display.fillCircle(x, y, 3, TFT_GREEN);
    OSCMessage m("/dial");
    m.add((intOSC_t) seq++)
     .add((intOSC_t) pos)
     .add((intOSC_t) (btn ? 1 : 0))
     .add((intOSC_t) (down ? 1 : 0))
     .add((intOSC_t) x).add((intOSC_t) y)
     .add((intOSC_t) (pos - lastPos));
    SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
    lastSend = now;
    lastPos = pos;
  }
  wasDown = down; wasBtn = btn;
}

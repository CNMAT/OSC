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
// RFID: the WS1850S is register-compatible with the MFRC522, and it is driven
// here through M5Unified's OWN I2C driver (M5.In_I2C) with a minimal RC522
// transceiver -- about eighty lines -- rather than through an I2C library.
// That is not taste, it is a repair: the first version used MFRC522_I2C over
// the Arduino Wire object, and Wire.begin(11,12) put a SECOND driver on the
// pins M5Unified's In_I2C was already running for touch and the RTC. Two
// drivers arbitrating one bus made the whole sketch sluggish, starved the
// touch reads, and corrupted enough RFID transceives that no tag ever read.
// One bus, one driver, one lock.
//
// Tag presence is polled by cycling the RF field (field off resets every tag
// in range to IDLE, so REQA answers again), then REQA + anticollision for the
// UID. A tag arriving streams /rfid T with its UID; silence for two polls
// streams /rfid F. The RC522's own timer bounds every transceive at ~7 ms, so
// a no-tag poll costs ~12 ms every 250 ms -- invisible next to the 33 Hz
// /dial stream, where the old version blocked for multiples of that.
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
//   /rfid ,Ts | ,Fs  present flag + UID as hex text: T "a1b2c3d4" when a
//                    tag arrives, F with the same UID when it leaves. The
//                    flag is an OSC boolean (tag T or F, no payload)
//   /dial ,iiiiiii  seq, encPos, btn, touchDown, x, y, encDelta
//                   -- one message, sampled in one pass; a state change
//                   (press, release, touch edge, any encoder motion) always
//                   sends, otherwise paced by /rate
#include <M5Unified.h>

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define PIN_HOLD  46
// A/B swapped relative to M5's pin listing, deliberately: measured on the
// bench, the listed order counts DOWN for a clockwise turn. Convention here
// is clockwise = positive, and the swap is the whole fix.
#define PIN_ENC_A 40
#define PIN_ENC_B 41
#define RFID_ADDR 0x28
#define RFID_FREQ 400000

// Minimal MFRC522-over-In_I2C. Register names per the NXP datasheet.
enum { R_CMD=0x01, R_COMIRQ=0x04, R_ERROR=0x06, R_COLL=0x0E, R_FIFODATA=0x09,
       R_FIFOLEVEL=0x0A, R_BITFRAMING=0x0D, R_MODE=0x11, R_TXCONTROL=0x14,
       R_TXASK=0x15, R_TMODE=0x2A, R_TPRESCALER=0x2B, R_TRELOADH=0x2C,
       R_TRELOADL=0x2D, R_VERSION=0x37 };

static uint8_t rr(uint8_t reg) { return M5.In_I2C.readRegister8(RFID_ADDR, reg, RFID_FREQ); }
static void    wr(uint8_t reg, uint8_t v) { M5.In_I2C.writeRegister8(RFID_ADDR, reg, v, RFID_FREQ); }

static void rfidInit() {
  wr(R_CMD, 0x0F); delay(50);            // soft reset
  wr(R_TMODE, 0x80);                     // timer auto-starts at TX end
  wr(R_TPRESCALER, 0xA9);                // ~25 us tick
  wr(R_TRELOADH, 0x01); wr(R_TRELOADL, 0x2C);   // 300 ticks = ~7.5 ms timeout
  wr(R_TXASK, 0x40);                     // 100% ASK
  wr(R_MODE, 0x3D);                      // CRC preset 0x6363
  wr(R_COLL, rr(R_COLL) & 0x7F);         // ValuesAfterColl: keep all bits
  wr(0x26, 0x70);                        // RFCfgReg: max receiver gain, 48 dB
  wr(R_TXCONTROL, rr(R_TXCONTROL) | 0x03);      // antenna on
}

// One transceive, bounded by the chip's own timer. Returns bytes read into
// buf, or -1. txBits: 7 for REQA/WUPA short frames, 0 for full bytes.
static int rfidXcv(const uint8_t *send, uint8_t n, uint8_t txBits,
                   uint8_t *buf, uint8_t cap) {
  wr(R_CMD, 0x00);                       // idle
  wr(R_COMIRQ, 0x7F);                    // clear irqs
  wr(R_FIFOLEVEL, 0x80);                 // flush FIFO
  for (uint8_t i = 0; i < n; i++) wr(R_FIFODATA, send[i]);
  wr(R_CMD, 0x0C);                       // transceive
  wr(R_BITFRAMING, 0x80 | (txBits & 7)); // start send
  const uint32_t t0 = millis();
  for (;;) {
    const uint8_t irq = rr(R_COMIRQ);
    if (irq & 0x30) break;               // RxIRq or IdleIRq: done
    if ((irq & 0x01) || millis() - t0 > 15) { wr(R_BITFRAMING, 0); return -1; }
  }
  wr(R_BITFRAMING, 0);
  if (rr(R_ERROR) & 0x13) return -1;     // BufferOvfl | ParityErr | ProtocolErr
  uint8_t got = rr(R_FIFOLEVEL);
  if (got > cap) got = cap;
  for (uint8_t i = 0; i < got; i++) buf[i] = rr(R_FIFODATA);
  return got;
}

// Field-cycle, REQA, anticollision. Field off resets every tag in range to
// IDLE, which is what lets the SAME tag answer again on the next poll --
// without it a tag answers once and then sits silent in READY, which reads
// exactly like a departure.
static bool rfidReadUid(uint8_t *uid) {
  wr(R_TXCONTROL, rr(R_TXCONTROL) & ~0x03); delay(5);
  wr(R_TXCONTROL, rr(R_TXCONTROL) | 0x03);  delay(10);   // ISO guard time is
                                                         // ~5 ms; be generous
  uint8_t atqa[2];
  // REQA wakes IDLE tags; WUPA also wakes HALTed ones. A card that has been
  // through a payment terminal can be sitting in HALT, so try both.
  const uint8_t reqa = 0x26, wupa = 0x52;
  int n = rfidXcv(&reqa, 1, 7, atqa, 2);
  if (n < 2) n = rfidXcv(&wupa, 1, 7, atqa, 2);
  if (n < 2) return false;
  const uint8_t ac[2] = { 0x93, 0x20 };
  uint8_t r[5];
  if (rfidXcv(ac, 2, 0, r, 5) != 5) return false;
  if ((uint8_t)(r[0] ^ r[1] ^ r[2] ^ r[3]) != r[4]) return false;   // BCC
  memcpy(uid, r, 4);
  return true;
}

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

  // Presence by the version register, through M5Unified's own bus driver: a
  // missing part reads 0x00 or 0xFF, a real one reads a chip version byte.
  const uint8_t ver = rr(R_VERSION);
  rfidOK = (ver != 0x00 && ver != 0xFF);
  if (rfidOK) rfidInit();

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

// Tag watcher: 4 polls a second, each bounded by the RC522's timer. Arrival
// sends /rfid T with the UID; two consecutive silent polls send /rfid F.
static char     rfidUid[9] = "";
static bool     rfidTag = false;
static uint8_t  rfidMisses = 0;

static void rfidSend(bool present) {
  OSCMessage m("/rfid");
  m.add(present).add(rfidUid);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

// One instrumented attempt, reported as /diag with the verdict at each stage,
// so "no tag ever reads" turns into WHICH step fails: version, antenna,
// REQA/WUPA (no answer = nothing ISO14443-A in the field -- note a Type B
// bank card can never answer an MFRC522, which speaks Type A only), or
// anticollision (answered but UID failed = protocol bug on this side).
static void routeRfidDiag(OSCMessage &) {
  OSCMessage m("/diag");
  if (!rfidOK) { m.add("no-chip"); }
  else {
    m.add("ver").add((intOSC_t) rr(R_VERSION));
    m.add("txcontrol").add((intOSC_t) rr(R_TXCONTROL));
    wr(R_TXCONTROL, rr(R_TXCONTROL) & ~0x03); delay(5);
    wr(R_TXCONTROL, rr(R_TXCONTROL) | 0x03);  delay(10);
    uint8_t atqa[2] = {0,0};
    const uint8_t reqa = 0x26, wupa = 0x52;
    int n = rfidXcv(&reqa, 1, 7, atqa, 2);
    m.add("reqa").add((intOSC_t) n);
    if (n < 2) { n = rfidXcv(&wupa, 1, 7, atqa, 2); m.add("wupa").add((intOSC_t) n); }
    if (n >= 2) {
      m.add("atqa").add((intOSC_t)((atqa[1] << 8) | atqa[0]));
      const uint8_t ac[2] = { 0x93, 0x20 };
      uint8_t r[5];
      const int an = rfidXcv(ac, 2, 0, r, 5);
      m.add("anticoll").add((intOSC_t) an);
      if (an == 5) {
        char buf[11];
        sprintf(buf, "%02x%02x%02x%02x", r[0], r[1], r[2], r[3]);
        m.add("uid").add(buf)
         .add("bcc").add((intOSC_t)((uint8_t)(r[0]^r[1]^r[2]^r[3]) == r[4] ? 1 : 0));
      } else {
        m.add("err").add((intOSC_t) rr(R_ERROR));
      }
    }
  }
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

static void rfidPoll() {
  if (!rfidOK) return;
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last < 250) return;
  last = now;

  uint8_t uid[4];
  if (rfidReadUid(uid)) {
    char buf[9];
    sprintf(buf, "%02x%02x%02x%02x", uid[0], uid[1], uid[2], uid[3]);
    if (!rfidTag || strcmp(buf, rfidUid) != 0) {
      strcpy(rfidUid, buf);
      rfidTag = true;
      rfidSend(true);
    }
    rfidMisses = 0;
  } else if (rfidTag && ++rfidMisses >= 2) {
    rfidTag = false;
    rfidSend(false);
  }
}

void loop() {
  static uint32_t lastSend = 0;
  static int32_t  lastPos = 0;
  static bool     wasDown = false, wasBtn = false;

  M5.update();                           // services BtnA and Touch
  rfidPoll();

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
      inMsg.dispatch("/rfid/diag",   routeRfidDiag);
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

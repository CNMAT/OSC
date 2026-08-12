// UNO R4 WiFi: scroll an OSC-delivered message across the 12x8 LED matrix.
//
//   http://localhost/UnoR4MatrixOscuino.html   (Web Serial; not file://)
//
// Needs the ArduinoGraphics library (Library Manager, or
// `arduino-cli lib install ArduinoGraphics`). Arduino_LED_Matrix ships with
// the renesas_uno core and enables its drawing API only when ArduinoGraphics
// is present, so include ArduinoGraphics FIRST.
//
// Inbound
//   /matrix/text ,s "HELLO"     the message to scroll; restarts at the right
//   /matrix/rate ,i ms          column step, 20..1000 (default 90)
//   /matrix/bright ,i 0|1       whole-panel on/off (the matrix is 1-bit)
//   /matrix/pause ,i 0|1        freeze the scroll where it is
//   /matrix/pixels ,b <96>      raw frame, one byte per LED, row-major 12x8;
//                               takes over until the next /matrix/text
// Outbound
//   /hello ,sii  name, columns in the current message, matrixOK
//   /matrix ,siii text, scroll column, total columns, paused
//   /frame ,b <96>  the exact pixels being displayed, so the page mirrors
//                   the panel instead of re-implementing the scroll
//
// WHY THE SCROLL IS STEPPED HERE RATHER THAN BY THE LIBRARY. Arduino_LED_Matrix
// offers two ready-made scrolls and neither fits a message that arrives at
// runtime and must stay steerable:
//
//   * matrix.endText(SCROLL_LEFT) is BLOCKING -- ArduinoGraphics::endText()
//     runs `for (...) { ...; delay(_textScrollSpeed); }`, so the sketch stops
//     servicing OSC for the whole scroll. On this board in particular that
//     drops inbound frames (see the USB note below).
//   * TextAnimation (matrix.play()) is genuinely asynchronous -- an IRQ walks
//     a pre-rendered frame sequence -- but the sequence is rendered once into
//     a compile-time-sized buffer, so /matrix/rate and /matrix/pause could not
//     take effect mid-message without re-rendering it.
//
// So this drives the step itself and calls endText(NO_SCROLL) at a shifting x.
// The FONT and the glyph rendering are still the library's -- Font_5x7 via
// ArduinoGraphics -- because hand-rolling a font would be reinventing exactly
// what the dependency already provides, and would drift from it.
//
// THE PORT ON THIS BOARD IS NOT USB. The R4 WiFi builds with -DNO_USB, so the
// core does `#define Serial _UART1_` and Serial is a real UART wired to the
// on-board ESP32-S3, which bridges it to the host. Two consequences the rest
// of the examples never face: the baud rate is real and both ends must agree
// on 115200, and there is no end-to-end USB NAK -- flow control stops at the
// bridge. Measured with test/hardware/bench.py: clean at ordinary rates, but
// a burst arriving faster than the sketch drains overruns the core's 512-byte
// UART ring (Serial.h SERIAL_BUFFER_SIZE) rather than back-pressuring the
// host. Never block in loop() here. See BOARDS.md.
#include <ArduinoGraphics.h>                // must precede Arduino_LED_Matrix
#include "Arduino_LED_Matrix.h"

#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
SLIPEncodedSerial SLIPSerial(Serial);        // the R4 WiFi lands here
#endif

#define MW 12                                // matrix is 12 wide, 8 tall
#define MH 8

// The matrix keeps its canvas private, but set() is virtual, so overriding it
// mirrors every pixel the library draws into an array this sketch can send as
// /frame. That keeps the page showing what is actually lit rather than a
// second, drifting implementation of the same scroll.
class MirroredMatrix : public ArduinoLEDMatrix {
public:
  uint8_t shadow[MH][MW] = {{0}};
  void set(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
    if (x >= 0 && y >= 0 && x < MW && y < MH)
      shadow[y][x] = ((r | g | b) > 0) ? 1 : 0;
    ArduinoLEDMatrix::set(x, y, r, g, b);
  }
};

MirroredMatrix matrix;
static bool matrixOK = false;

static char     text[64] = "OSC";
static uint16_t textCols = 0;                // scroll length for this message
static int16_t  scrollAt = 0;                // leftmost visible column
static uint32_t stepMs   = 90;
static bool     paused   = false;
static bool     rawMode  = false;            // /matrix/pixels took over
static bool     lit      = true;

static void recomputeWidth() {
  // textFontWidth() rather than a literal 5: the width belongs to whichever
  // font is selected, and asking keeps this correct if that ever changes.
  textCols = (uint16_t)(strlen(text) * matrix.textFontWidth() + MW);
  if (textCols == 0) textCols = MW;
  if (scrollAt >= (int16_t) textCols) scrollAt = 0;
}

static void renderScroll() {
  memset(matrix.shadow, 0, sizeof matrix.shadow);
  matrix.beginDraw();
  matrix.clear();
  if (lit) {
    matrix.stroke(0xFFFFFFFF);
    matrix.textFont(Font_5x7);
    // Negative x scrolls the message leftwards. set() bounds-checks, so the
    // columns hanging off either edge are clipped rather than written. y=1
    // centres a 7-row font in the 8-row panel -- measured at y=0 the glyphs
    // sat on rows 0-5 with two dead rows underneath.
    matrix.beginText(-scrollAt, 1, 0xFFFFFF);
    matrix.print(text);
    matrix.endText(NO_SCROLL);               // NOT SCROLL_LEFT: that blocks
  }
  matrix.endDraw();
}

static void sendFrame() {                    // /frame ,b <96>
  OSCMessage m("/frame");
  m.add(&matrix.shadow[0][0], MW * MH);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

static void sendState() {                    // /matrix ,siii
  OSCMessage m("/matrix");
  m.add(text).add((intOSC_t) scrollAt).add((intOSC_t) textCols)
   .add((intOSC_t) (paused ? 1 : 0));
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {
  if (m.size() < 1 || !m.isString(0)) return;
  m.getString(0, text, sizeof text);
  rawMode = false;
  scrollAt = 0;
  recomputeWidth();
  renderScroll();
  sendState();
}

static void routeRate(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) stepMs = constrain(m.getInt(0), 20, 1000);
}

static void routeBright(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  lit = m.getInt(0) != 0;
  if (rawMode) {
    if (!lit) {
      memset(matrix.shadow, 0, sizeof matrix.shadow);
      matrix.loadPixels(&matrix.shadow[0][0], MW * MH);
    }
  } else {
    renderScroll();
  }
}

static void routePause(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) { paused = m.getInt(0) != 0; sendState(); }
}

static void routePixels(OSCMessage &m) {     // /matrix/pixels ,b <96>
  if (m.size() < 1 || !m.isBlob(0)) return;
  uint8_t in[MW * MH];
  const int n = m.getBlob(0, in, sizeof in);
  if (n < (int) sizeof in) return;           // partial frame: ignore, no tear
  memcpy(&matrix.shadow[0][0], in, sizeof in);
  rawMode = true;
  matrix.loadPixels(&matrix.shadow[0][0], MW * MH);
}

void setup() {
  SLIPSerial.begin(115200);

  // begin() returns 0 on success. There is no way to tell a wired-up matrix
  // from a bare RA4M1 -- the driver talks to on-chip peripherals either way --
  // so this reports the DRIVER, not the panel, and /hello says so.
  matrixOK = (matrix.begin() == 0);

  recomputeWidth();
  renderScroll();

  OSCMessage hello("/hello");
  hello.add("UnoR4MatrixOscuino").add((intOSC_t) textCols).add(matrixOK);
  SLIPSerial.beginPacket(); hello.send(SLIPSerial); SLIPSerial.endPacket();
}

void loop() {
  static uint32_t lastStep = 0, lastState = 0;

  // inbound first: a /matrix/text should land before the next scroll step
  if (SLIPSerial.available()) {
    static OSCMessage msg;
    msg.empty();
    while (!SLIPSerial.endofPacket()) {
      if (SLIPSerial.available()) {
        int c = SLIPSerial.read();           // int, -1 for "no byte"
        if (c >= 0) msg.fill((uint8_t) c);
      }
    }
    if (!msg.hasError()) {
      msg.dispatch("/matrix/text",   routeText);
      msg.dispatch("/matrix/rate",   routeRate);
      msg.dispatch("/matrix/bright", routeBright);
      msg.dispatch("/matrix/pause",  routePause);
      msg.dispatch("/matrix/pixels", routePixels);
    }
  }

  const uint32_t now = millis();

  if (!paused && !rawMode && now - lastStep >= stepMs) {
    lastStep = now;
    scrollAt = (int16_t)((scrollAt + 1) % textCols);
    renderScroll();
    sendFrame();                             // the page mirrors what is lit
  }

  if (now - lastState >= 500) { lastState = now; sendState(); }
}

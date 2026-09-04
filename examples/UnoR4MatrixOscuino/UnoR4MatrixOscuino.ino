// UNO R4 WiFi: scroll an OSC-delivered message across the 12x8 LED matrix.
//
//   http://localhost/UnoR4MatrixOscuino.html   (Web Serial; not file://)
//
// Needs the ArduinoGraphics library (Library Manager, or
// `arduino-cli lib install ArduinoGraphics`). Arduino_LED_Matrix ships with
// the renesas_uno core and enables its drawing API only when ArduinoGraphics
// is present, so include ArduinoGraphics FIRST.
//
// Speaks the capability namespace in ADDRESSES.md: the matrix is a `display`
// of 12x8 pixels, and nothing on the wire is named after the board.
//
// Inbound
//   /enq                      answer with the enq bundle below
//   /display/text ,s "HELLO"    the message to scroll; restarts at the right
//   /display/scroll ,i ms       column step, 20..1000 (default 90); NOT the
//                               stream rate -- that is /rate
//   /display/bl ,i 0|1          whole-panel on/off (the matrix is 1-bit)
//   /display/pause ,i 0|1       freeze the scroll where it is
//   /display/pixels ,b <96>     raw frame, one byte per LED, row-major 12x8;
//                               takes over until the next /display/text
//   /rate ,i ms                 period of the /state bundle, 20..5000;
//                               0 stops it (default 500)
// Outbound (every packet is an OSC bundle)
//   /enq ,s "UnoR4MatrixOscuino", then one /enq line per capability:
//     /enq/display ,ii 12 8     only when ArduinoLEDMatrix::begin() succeeded
//                               (the driver, not the panel -- see setup())
//     /enq/diag                 the free-text scroll position below
//   /display/text ,i lines drawn -- the reply to /display/text, and in the stream
//   /display/scroll, /display/bl, /display/pause, /rate ,i   echoed as set
//   /display/frame ,b <96>      the exact pixels being displayed, after every
//                               scroll step and after /display/pixels, so the
//                               page mirrors the panel instead of
//                               re-implementing the scroll
//   every /rate ms, one bundle:
//     /state ,ii seq millis
//     /display/text ,i lines drawn (1 while a lit message scrolls, else 0)
//     /diag ,s "\"<text>\" <column>/<columns>"  free text, never parsed
//
// STATUS: verified on the board when first added (commit 8adbc39,
// 2026-08-12): /enq answered, a live text change scrolled, the step rate
// took effect (31 frames/s at 40 ms against 4 at 300 ms), pause held the
// frame count at 0, and a captured frame blob showed a correctly formed
// glyph. The audit passes of 2026-08-16/17 (matrixOK polarity, /enq made
// inbound, boolean-tag comments) recorded no re-run. Addresses renamed onto
// ADDRESSES.md on 2026-09-03 (/matrix/text -> /display/text, /matrix/rate ->
// /display/scroll, /matrix/bright -> /display/bl, /matrix/pause ->
// /display/pause, /matrix/pixels -> /display/pixels, /frame -> /display/frame,
// the /matrix state message -> a /state + /display/text + /diag bundle, the
// /enq columns-and-boolean -> /enq lines, and /rate added for the stream
// period); that build is compile-checked and has not been re-run on the board.
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
//     a compile-time-sized buffer, so /display/scroll and /display/pause could
//     not take effect mid-message without re-rendering it.
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
// /display/frame. That keeps the page showing what is actually lit rather
// than a second, drifting implementation of the same scroll.
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
static uint32_t stepMs   = 90;               // /display/scroll
static uint32_t statePeriod = 500;           // /rate: the /state bundle, ms
static bool     paused   = false;
static bool     rawMode  = false;            // /display/pixels took over
static bool     lit      = true;

// Everything outbound goes through one bundle, flushed after a dispatch pass,
// a scroll step, or a stream tick, so a reply and its echoes share a packet.
static OSCBundle bundleOUT;

static void flushOut() {
  if (bundleOUT.size() == 0) return;
  SLIPSerial.beginPacket(); bundleOUT.send(SLIPSerial); SLIPSerial.endPacket();
  bundleOUT.empty();
}

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

static void addFrame() {                     // /display/frame ,b <96>
  bundleOUT.add("/display/frame").add(&matrix.shadow[0][0], MW * MH);
}

// /display/text ,i -- lines drawn. One line of text is on the panel while a
// lit message scrolls; a raw frame or a blanked panel draws none.
static void addTextLines() {
  bundleOUT.add("/display/text").add((intOSC_t) ((lit && !rawMode) ? 1 : 0));
}

// /diag ,s -- where the scroll is, as free text for a page's diagnostics
// row. The contract has no address for a scroll column, and this is exactly
// what /diag is for: readable, never parsed.
static void addDiag() {
  char line[sizeof text + 24];
  snprintf(line, sizeof line, "\"%s\" %d/%u", text, (int) scrollAt, (unsigned) textCols);
  bundleOUT.add("/diag").add(line);
}

static void addState() {                     // the stream: /state first
  static uint32_t seq = 0;
  bundleOUT.add("/state").add((intOSC_t) seq++).add((intOSC_t) millis());
  addTextLines();
  addDiag();
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {       // /display/text ,s
  if (m.size() < 1 || !m.isString(0)) return;
  m.getString(0, text, sizeof text);
  rawMode = false;
  scrollAt = 0;
  recomputeWidth();
  renderScroll();
  addTextLines();                            // the reply: lines drawn
}

static void routeScroll(OSCMessage &m) {     // /display/scroll ,i ms per column
  if (m.size() >= 1 && m.isInt(0)) stepMs = constrain(m.getInt(0), 20, 1000);
  bundleOUT.add("/display/scroll").add((intOSC_t) stepMs);
}

static void routeBl(OSCMessage &m) {         // /display/bl ,i 0|1
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
  bundleOUT.add("/display/bl").add((intOSC_t) (lit ? 1 : 0));
}

static void routePause(OSCMessage &m) {      // /display/pause ,i 0|1
  if (m.size() >= 1 && m.isInt(0)) paused = m.getInt(0) != 0;
  bundleOUT.add("/display/pause").add((intOSC_t) (paused ? 1 : 0));
}

static void routePixels(OSCMessage &m) {     // /display/pixels ,b <96>
  if (m.size() < 1 || !m.isBlob(0)) return;
  uint8_t in[MW * MH];
  const int n = m.getBlob(0, in, sizeof in);
  if (n < (int) sizeof in) return;           // partial frame: ignore, no tear
  memcpy(&matrix.shadow[0][0], in, sizeof in);
  rawMode = true;
  matrix.loadPixels(&matrix.shadow[0][0], MW * MH);
  addFrame();                                // the frame now shown
}

static void routeRate(OSCMessage &m) {       // /rate ,i ms; 0 stops the stream
  if (m.size() >= 1 && m.isInt(0)) {
    const int r = m.getInt(0);
    statePeriod = (r <= 0) ? 0 : constrain(r, 20, 5000);
  }
  bundleOUT.add("/rate").add((intOSC_t) statePeriod);
}

// The boot /enq is very nearly always lost: the board resets, its USB
// device re-enumerates, and the host opens the port hundreds of milliseconds
// later, long after setup() has finished. So /enq is an INBOUND address
// too, and the page asks for it on connect.
//
// No booleans: a capability is announced by being present. When the matrix
// driver failed to start there is no /enq/display line, and a page that
// knows the contract shows no display panel.
static void addEnq() {
  bundleOUT.add("/enq").add("UnoR4MatrixOscuino");
  if (matrixOK)
    bundleOUT.add("/enq/display").add((intOSC_t) MW).add((intOSC_t) MH);
  bundleOUT.add("/enq/diag");
}

static void routeEnq(OSCMessage &) { addEnq(); }

void setup() {
  SLIPSerial.begin(115200);

  // ArduinoLEDMatrix::begin() returns TRUE on success (it accumulates rv and
  // returns it; only the no-free-timer path returns false). An earlier line
  // here tested == 0 and so set matrixOK true exactly when the driver had
  // failed. It went unnoticed because /enq was unreachable -- see the
  // routeEnq added below -- so nothing ever displayed the flag. Two bugs
  // concealing each other.
  //
  // There is still no way to tell a wired-up matrix from a bare RA4M1: the
  // driver talks to on-chip peripherals either way, so this reports the
  // DRIVER, not the panel, and the /enq/display line means exactly that.
  matrixOK = (matrix.begin() != 0);

  recomputeWidth();
  renderScroll();

  addEnq();           // nearly always lost; the page asks again
  flushOut();
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
  static uint32_t lastStep = 0, lastState = 0;

  // inbound first: a /display/text should land before the next scroll step
  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/display/text",   routeText);
      inMsg.dispatch("/display/scroll", routeScroll);
      inMsg.dispatch("/display/bl",     routeBl);
      inMsg.dispatch("/display/pause",  routePause);
      inMsg.dispatch("/display/pixels", routePixels);
      inMsg.dispatch("/rate",           routeRate);
      inMsg.dispatch("/enq",          routeEnq);
    }
    inMsg.empty();
    flushOut();
  }

  const uint32_t now = millis();

  if (!paused && !rawMode && now - lastStep >= stepMs) {
    lastStep = now;
    scrollAt = (int16_t)((scrollAt + 1) % textCols);
    renderScroll();
    addFrame();                              // the page mirrors what is lit
    flushOut();
  }

  if (statePeriod && now - lastState >= statePeriod) {
    lastState = now;
    addState();
    flushOut();
  }
}

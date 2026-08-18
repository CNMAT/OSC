// Seeeduino XIAO (SAMD21) driving and READING a 6 x 5 Baudot LED array.
//
// This is the original XIAO variant of BaudotOscuino, which was developed on a
// Waveshare RP2350-Zero. Everything about the measurement is the same; what
// changes is the pin budget and the fact that nothing here uses a chip-specific
// SDK call, so it is ordinary Arduino and ports further without edits.
//
//   arduino-cli compile -b Seeeduino:samd:seeed_XIAO_m0 examples/BaudotXiaoOscuino
//
// NO SOUND ON THIS BOARD, and it is not an oversight. The original XIAO breaks
// out exactly ELEVEN GPIO (D0..D10), and a 6-row by 5-column array needs
// exactly eleven. The RP2350 build drives a speaker from two PWM channels of
// one slice; there is no twelfth pin here to do it with. If you want the
// melody on a XIAO, either drop to five symbols and free D0 -- which is also
// the DAC, so it can drive a speaker properly rather than with a square wave --
// or use a XIAO with more pins.
//
// WIRING, unchanged from the RP2350 build:
//   COLUMNS are the five code bits, LSB first:  D6 D7 D8 D9 D10
//   ROWS are the six symbol positions:          D0 D1 D2 D3 D4 D5
//   Every LED has its ANODE on its ROW and its CATHODE on its COLUMN, so a
//   cell lights when its row is HIGH and its column is LOW: the data is
//   ACTIVE LOW.
//
// There are no series resistors in this build; current is set by the pad
// impedance, and only one row is ever lit at a time to bound it.
//
// THE ARRAY IS THE MESSAGE. LEDs are fitted only where a code bit is 1, so the
// population of the array spells something and the board reads it back.
//
// HOW THE READ WORKS, and why it is not the obvious thing. The LEDs are
// YELLOW: forward drop above about 2.1 V. Drive a row and a conducting cell
// leaves only ~1.0-1.3 V on the sense node, which falls between a 3.3 V part's
// low and high input thresholds -- so "drive a row and see which columns go
// high" reads nothing, a pull-down cannot hold a clean zero against it, and
// hysteresis cannot latch it either way. Every method that ends in a digital
// threshold fails, and no amount of cleverness in the probe fixes it.
//
// What works is to stop asking about the LEVEL and ask about the TIME:
//
//   1. ground every pin                      -- node at a known 0
//   2. float the column, drive the row HIGH  -- a fitted LED charges the node
//                                               to 3.3 - Vf; an empty one does
//                                               not move
//   3. release the row to INPUT              -- current paths off, charge held
//   4. switch the column to INPUT_PULLUP and count until it reads HIGH
//
// The pull-up climbs to 3.3 V through the same RC either way, so the only
// variable is where the node started. From 1.2 V it crosses the threshold in
// about 57% of the time it takes from 0 V.
//
// Two things that are easy to get wrong and were, on the bench:
//
//   GROUND THE PINS YOU ARE NOT USING. Leaving the other columns floating lets
//   the driven row's edge couple into them, and charge on a node with no
//   discharge path is indistinguishable from conduction. That produces a
//   stable, repeatable and completely fictional reading -- stable across runs,
//   which is exactly what makes it convincing.
//
//   MEASURE DIFFERENTIALLY. Each cell is timed twice, identically, except that
//   one pass precharges through the diode and the other does not. Subtracting
//   cancels the pad's own capacitance and leakage, which otherwise vary far
//   more between pins than the signal does between cells.
//
// The read runs ONCE at start-up, with the display dark and settled. The array
// is soldered: it cannot change while running, and re-reading mid-display
// replaces good values with worse ones.
//
// THE CODE IS ITA1 -- the original Baudot alphabet, not the ITA2 that
// teleprinters later used. They are not compatible; the same bits spell
// different words. The table below is transcribed from the ITA1 Continental
// assignment.
//
// Inbound
//   /baudot/text ,s     six characters, ITA1
//   /baudot/raw ,i...   six raw codes 0..31
//   /baudot/read        report the start-up read (does NOT re-measure)
//   /baudot/reread      force a fresh measurement
//   /baudot/show ,i     1 = run the display sequence, 0 = stop it
//   /hello
// Outbound
//   /hello ,siii    name, rows, cols, 0 (no speaker on this board)
//   /present ,i...  30 presence flags then the six 5-bit codes
//   /baudot ,i...   seq then the six codes currently displayed
#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define NROWS 6
#define NCOLS 5

// Rows: first symbol to last. Columns: code bit 1 (LSB) to bit 5 (MSB).
static const uint8_t ROW_PIN[NROWS] = { 0, 1, 2, 3, 4, 5 };
static const uint8_t COL_PIN[NCOLS] = { 6, 7, 8, 9, 10 };

// ITA1 Continental letters, bit 1 in the least significant position so the
// value can be written straight to the columns.
//
// G IS NOT IN THIS ALPHABET. The ITA1 Continental assignment puts E-acute at
// the code point that ITA2 later gave to G, so there is no G to transcribe and
// the entry below is 0 -- charToCode('G') therefore blanks that symbol rather
// than inventing a code. The UK variant of ITA1 differs again at that point.
// This is a real property of the 1870s alphabet, not a gap in the table.
static const uint8_t ITA1_LTRS[26] = { 0x01, 0x0C, 0x0D, 0x0F, 0x02, 0x0E, 0x00, 0x0B, 0x06, 0x09, 0x19, 0x1B, 0x1A, 0x1E, 0x07, 0x1F, 0x1D, 0x1C, 0x14, 0x15, 0x05, 0x17, 0x16, 0x12, 0x04, 0x13 };

static uint8_t  code[NROWS] = { 0, 0, 0, 0, 0, 0 };
static uint8_t  present[NROWS][NCOLS];
static uint8_t  bootCode[NROWS];
static int32_t  seq = 0;
static bool     showing = true;

static uint8_t charToCode(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if (c >= 'A' && c <= 'Z') return ITA1_LTRS[c - 'A'];
  return 0;
}

/* ----------------------------------------------------------- static drive */

static void lightNone() {
  for (uint8_t i = 0; i < NROWS; i++) { pinMode(ROW_PIN[i], OUTPUT); digitalWrite(ROW_PIN[i], LOW); }
  for (uint8_t k = 0; k < NCOLS; k++) { pinMode(COL_PIN[k], OUTPUT); digitalWrite(COL_PIN[k], HIGH); }
}
static void lightRow(uint8_t r) {
  for (uint8_t i = 0; i < NROWS; i++) digitalWrite(ROW_PIN[i], i == r ? HIGH : LOW);
  for (uint8_t k = 0; k < NCOLS; k++) digitalWrite(COL_PIN[k], LOW);
}
static void lightCell(uint8_t r, uint8_t c) {
  for (uint8_t i = 0; i < NROWS; i++) digitalWrite(ROW_PIN[i], i == r ? HIGH : LOW);
  for (uint8_t k = 0; k < NCOLS; k++) digitalWrite(COL_PIN[k], k == c ? LOW : HIGH);
}

/* -------------------------------------------------------- reading the array */

#define CHG_TRIALS 90
#define CHG_LIMIT  20000

static uint32_t chargeOnce(uint8_t r, uint8_t c, bool precharge) {
  uint32_t total = 0;
  for (uint16_t t = 0; t < CHG_TRIALS; t++) {
    for (uint8_t i = 0; i < NROWS; i++) { pinMode(ROW_PIN[i], OUTPUT); digitalWrite(ROW_PIN[i], LOW); }
    for (uint8_t k = 0; k < NCOLS; k++) { pinMode(COL_PIN[k], OUTPUT); digitalWrite(COL_PIN[k], LOW); }
    delayMicroseconds(250);                  // drain: the junctions hold charge

    pinMode(COL_PIN[c], INPUT);              // float the node being measured
    if (precharge) digitalWrite(ROW_PIN[r], HIGH);
    delayMicroseconds(300);                  // matched to the drain -- shortening
                                             // this alone kills the sensitivity
    pinMode(ROW_PIN[r], INPUT);              // isolate: charge is trapped

    pinMode(COL_PIN[c], INPUT_PULLUP);       // race the pull-up to the threshold
    uint32_t n = 0;
    while (n < CHG_LIMIT && !digitalRead(COL_PIN[c])) n++;
    total += n;
  }
  return total;
}

static void readAll() {
  lightNone();
  delay(400);                                // settle: lit cells bias the read

  uint8_t votes[NROWS][NCOLS];
  int32_t last[NROWS][NCOLS];
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++) votes[r][c] = 0;

  for (uint8_t pass = 0; pass < 3; pass++)   // best of three
    for (uint8_t r = 0; r < NROWS; r++)
      for (uint8_t c = 0; c < NCOLS; c++) {
        const int32_t d = (int32_t) chargeOnce(r, c, false) - (int32_t) chargeOnce(r, c, true);
        last[r][c] = d;
        if (d > 100) votes[r][c]++;
      }

  for (uint8_t r = 0; r < NROWS; r++) {
    bootCode[r] = 0;
    for (uint8_t c = 0; c < NCOLS; c++) {
      present[r][c] = (votes[r][c] >= 2) ? 1 : 0;
      if (present[r][c]) bootCode[r] |= (uint8_t)(1u << c);
    }
    code[r] = bootCode[r];
  }
  lightNone();
}

static void reportPresent() {
  OSCMessage m("/present");
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++) m.add((intOSC_t) present[r][c]);
  for (uint8_t r = 0; r < NROWS; r++) m.add((intOSC_t) bootCode[r]);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

/* ------------------------------------------------------------- the display */

#define ROW_MS     500
#define BLOCK_MS  1500
#define SNAKE_MS   125
#define BLOCKS_PER_SNAKE 3

enum { PH_ROWS, PH_GAP, PH_SNAKE };
static uint8_t  phase = PH_ROWS, rowIdx = 0, blocks = 0, snakeIdx = 0;
static uint32_t phaseT = 0;

// Non-blocking: no delay() anywhere in the sequence, so inbound OSC is still
// serviced between steps.
static void animate() {
  if (!showing) return;
  const uint32_t now = millis();
  switch (phase) {
    case PH_ROWS:
      if (now - phaseT < ROW_MS) return;
      phaseT = now;
      if (++rowIdx >= NROWS) { rowIdx = 0; phase = PH_GAP; lightNone(); }
      else lightRow(rowIdx);
      return;
    case PH_GAP:
      if (now - phaseT < BLOCK_MS) return;
      phaseT = now;
      if (++blocks >= BLOCKS_PER_SNAKE) { blocks = 0; phase = PH_SNAKE; snakeIdx = 0; lightCell(0, 0); }
      else { phase = PH_ROWS; rowIdx = 0; lightRow(0); }
      return;
    case PH_SNAKE:
      if (now - phaseT < SNAKE_MS) return;
      phaseT = now;
      if (++snakeIdx >= NROWS * NCOLS) { phase = PH_ROWS; rowIdx = 0; lightRow(0); }
      else lightCell(snakeIdx / NCOLS, snakeIdx % NCOLS);
      return;
  }
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {
  if (m.size() < 1 || !m.isString(0)) return;
  char buf[NROWS + 1];
  m.getString(0, buf, sizeof buf);
  for (uint8_t r = 0; r < NROWS; r++) code[r] = buf[r] ? charToCode(buf[r]) : 0;
}
static void routeRaw(OSCMessage &m) {
  const int n = m.size() < NROWS ? m.size() : NROWS;
  for (int i = 0; i < n; i++) if (m.isInt(i)) code[i] = (uint8_t)(m.getInt(i) & 0x1F);
}
static void routeRead(OSCMessage &)   { reportPresent(); }
static void routeReread(OSCMessage &) { const bool w = showing; showing = false; readAll(); showing = w; phaseT = millis(); }
static void routeShow(OSCMessage &m)  {
  showing = !(m.size() >= 1 && m.isInt(0) && m.getInt(0) == 0);
  if (!showing) lightNone();
  else { phase = PH_ROWS; rowIdx = 0; blocks = 0; phaseT = millis(); lightRow(0); }
}

static void sendHello() {
  OSCMessage h("/hello");
  h.add("BaudotXiaoOscuino").add((intOSC_t) NROWS).add((intOSC_t) NCOLS)
   .add((intOSC_t) 0);                       // 0 = no speaker: no pin spare
  SLIPSerial.beginPacket(); h.send(SLIPSerial); SLIPSerial.endPacket();
}
static void routeHello(OSCMessage &) { sendHello(); }

void setup() {
  SLIPSerial.begin(115200);                  // USB first: a sketch that cannot
                                             // report its own failure is not
                                             // debuggable on a headless board
  lightNone();
  delay(200);
  readAll();                                 // ONCE, dark and settled

  sendHello();                               // usually lost; the page asks again
  phase = PH_ROWS; rowIdx = 0; blocks = 0; phaseT = millis();
  lightRow(0);
}

// Non-blocking receive, the extras/webserial/template.ino pattern:
// endofPacket() BEFORE available(), and RETURN rather than block when the
// buffer runs dry, so an unplug mid-frame cannot wedge loop().
static OSCMessage inMsg;

static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;
    while (size--) { int c = SLIPSerial.read(); if (c >= 0) inMsg.fill((uint8_t) c); }
  }
  return true;
}

void loop() {
  static uint32_t lastReport = 0;

  animate();

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/baudot/text",   routeText);
      inMsg.dispatch("/baudot/raw",    routeRaw);
      inMsg.dispatch("/baudot/read",   routeRead);
      inMsg.dispatch("/baudot/reread", routeReread);
      inMsg.dispatch("/baudot/show",   routeShow);
      inMsg.dispatch("/hello",         routeHello);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (now - lastReport < 100) return;
  lastReport = now;

  OSCMessage m("/baudot");
  m.add((intOSC_t) seq++);
  for (uint8_t r = 0; r < NROWS; r++) m.add((intOSC_t) code[r]);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

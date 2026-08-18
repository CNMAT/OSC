// Waveshare RP2350-Zero driving a 6 x 5 multiplexed LED array as a Baudot
// display: six symbols, each shown as its five-bit telegraph code.
//
//   http://localhost/BaudotOscuino.html   (Web Serial; not file://)
//
// FQBN: rp2040:rp2040:rpipico2. There is NO Waveshare board definition in the
// arduino-pico core, and the board's own bootloader identifies only as the
// generic "Board-ID: RP2350" in INFO_UF2.TXT. Waveshare's wiki says to select
// "Raspberry Pi Pico 2", and the silicon matches: RP2350A, 520 KB SRAM, 4 MB
// on-chip flash. After flashing it enumerates with USB product name "Pico 2" --
// that is the FQBN's descriptor, not the board introducing itself, so do not
// identify this board from its USB strings.
//
// To flash: hold BOOT (or press RESET and BOOT together, release RESET first),
// copy the .uf2 to the volume that appears. Waveshare's procedure, not a
// local invention.
//
// THE WIRING, as built on the bench:
//
//   COLUMNS are the five code bits, LSB first:
//       bit 1 (LSB) = GPIO 13
//       bit 2       = GPIO 12
//       bit 3       = GPIO 11
//       bit 4       = GPIO 10
//       bit 5 (MSB) = GPIO  9
//   ROWS are the six symbol positions, first to last:
//       GPIO 0, 1, 2, 3, 4, 5
//
//   Every LED has its ANODE (+) on its ROW and its CATHODE (-) on its COLUMN.
//   So a diode conducts only when its row is driven HIGH and its column is
//   pulled LOW: row HIGH selects the symbol, column LOW lights the bits that
//   are 1 in that symbol's code.
//
// That polarity is what decides the whole driver: rows are the scan, columns
// carry the data, and the data is ACTIVE LOW. Getting it backwards lights the
// complement of every character, which looks like a code-table bug and is not.
//
// ONE LED IS LIT PER COLUMN AT A TIME, but up to five at once in a row, so the
// ROW pin carries the sum of its lit columns. Size the series resistors for
// the column pins and remember the row pin sees up to 5x that. RP2350 GPIO is
// specified for a few mA per pin; this sketch keeps one row on at a time,
// which bounds it, but it cannot supply a missing resistor.
//
// MULTIPLEXING: one row is lit at a time for ROW_US, so a full frame is
// 6 * ROW_US. At the default 1500 us that is 9 ms, about 111 Hz -- above
// flicker, and slow enough that the scan costs almost nothing. The scan is
// driven from loop() off micros(), never from delay(), because loop() also has
// to pump OSC; a blocking scan would trade the display against the transport.
//
// THE CODE IS ITA2 (International Telegraph Alphabet No. 2), which is what
// "Baudot" almost always means in practice -- the real Baudot code of 1870 had
// a different assignment and is not what teleprinters used. ITA2 is a SHIFTED
// code: the same five bits mean a letter after a LTRS (11111) shift and a
// figure after FIGS (11011). This display shows six symbols at once rather
// than a stream, so there is no shift state on the wire; instead each symbol
// is resolved to its code when the text is set, digits resolving through the
// FIGS table. The bit numbering below is the usual one, bit 1 as the LSB, so
// E = 10000 written out is bit 1 alone = 1, and T = 00001 is bit 5 alone = 16.
//
// Inbound
//   /baudot/text ,s     up to six characters; shorter pads with blanks
//   /baudot/raw ,i...   up to six raw codes 0..31, bypassing the tables --
//                       this is how you show LTRS, FIGS, CR, LF or a
//                       deliberate invalid pattern
//   /baudot/probe       sense which cells actually have an LED fitted and
//                       report the raw charge counts -- the array is itself
//                       the message, so this READS it
//   /baudot/rowus ,i    row dwell in microseconds, 200..20000 (flicker vs
//                       brightness; the default 1500 is ~111 Hz)
//   /hello              ask again -- the boot one is lost to USB enumeration
// Outbound
//   /hello ,siii  name, rows, cols, rowMicros
//   /probe ,i x30     raw charge counts, row-major, 6 rows x 5 columns:
//                     near zero = no diode fitted, at the limit = diode present
//   /baudot ,iiiiiii  seq then the six live codes, so a client can see
//                     exactly what the array is displaying rather than
//                     assuming its own text was accepted
#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define NROWS 6
#define NCOLS 5

// Rows: first symbol to last. Columns: code bit 1 (LSB) to bit 5 (MSB).
static const uint8_t ROW_PIN[NROWS] = { 0, 1, 2, 3, 4, 5 };
static const uint8_t COL_PIN[NCOLS] = { 13, 12, 11, 10, 9 };

static uint8_t  code[NROWS] = { 0, 0, 0, 0, 0, 0 };   // 5-bit code per symbol
static uint32_t rowUs = 1500;
static int32_t  seq = 0;

/* ------------------------------------------------------------------- ITA2 */
// Letters shift. Index is 'A'..'Z'. Values are the five bits with bit 1 as the
// LSB, so they can be written straight to the columns.
static const uint8_t ITA2_LTRS[26] = {
  0x03, 0x19, 0x0E, 0x09, 0x01, 0x0D, 0x1A, 0x14, 0x06, 0x0B, 0x0F, 0x12, 0x1C,
  0x0C, 0x18, 0x16, 0x17, 0x0A, 0x05, 0x10, 0x07, 0x1E, 0x13, 0x1D, 0x15, 0x11
};
// Figures shift, digits only: '0'..'9'. The rest of the FIGS row is
// national-variant soup and is deliberately not guessed at here; use
// /baudot/raw for anything else.
static const uint8_t ITA2_FIGS_DIGIT[10] = {
  0x16, 0x17, 0x13, 0x01, 0x0A, 0x10, 0x15, 0x07, 0x06, 0x18
};
#define ITA2_SPACE 0x04
#define ITA2_LTRS_SHIFT 0x1F
#define ITA2_FIGS_SHIFT 0x1B

static uint8_t charToCode(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if (c >= 'A' && c <= 'Z') return ITA2_LTRS[c - 'A'];
  if (c >= '0' && c <= '9') return ITA2_FIGS_DIGIT[c - '0'];
  return ITA2_SPACE;                     // blank rather than invent a code
}

/* --------------------------------------------------------------- the scan */

static void blank() {
  for (uint8_t r = 0; r < NROWS; r++) digitalWrite(ROW_PIN[r], LOW);
  for (uint8_t c = 0; c < NCOLS; c++) digitalWrite(COL_PIN[c], HIGH);
}

// Advance at most one row per call and return immediately otherwise. Called
// from loop() beside the OSC pump: the display must never own the CPU, or a
// burst of inbound packets would tear the frame and a long frame would drop
// packets.
static void scan() {
  static uint8_t row = 0;
  static uint32_t last = 0;
  const uint32_t now = micros();
  if ((uint32_t)(now - last) < rowUs) return;
  last = now;

  blank();                               // kill the previous row before
                                         // selecting the next, or its charge
                                         // ghosts into the new one
  row = (uint8_t)((row + 1) % NROWS);
  const uint8_t bits = code[row];
  for (uint8_t c = 0; c < NCOLS; c++)
    digitalWrite(COL_PIN[c], (bits & (1u << c)) ? LOW : HIGH);   // ACTIVE LOW
  digitalWrite(ROW_PIN[row], HIGH);
}

/* ------------------------------------------------------- reading the array */
// The LEDs themselves carry the message: a cell is populated only where its
// symbol's code has a 1. So the array can be READ, not just written, by asking
// each cell whether a diode is present.
//
// Method, and why this one. Every cell has its anode on the row and its
// cathode on the column, so:
//
//   1. drive the COLUMN low   -- the cathode is now at 0 V
//   2. drive the ROW low too  -- discharges the node to a known state
//   3. release the ROW to INPUT_PULLUP and count until it reads HIGH
//
// With NO diode fitted, the row node is just stray capacitance on a ~55k
// pull-up: it reaches VIH in about a microsecond and the count comes back at
// essentially zero. With a diode fitted, the diode conducts as soon as the
// node passes its forward voltage and CLAMPS it there -- around 1.6-2.0 V for
// a red or green part, which is below the input's high threshold -- so the pin
// never reads HIGH and the count runs to the limit.
//
// The discriminator is therefore "did it ever get there", not a voltage
// measurement, which matters because none of these pins are ADC-capable on an
// RP2350 (only GPIO 26-29 are). It also degrades honestly: a blue or white LED
// with a forward voltage above the input threshold would read as absent, and
// that shows up as a whole array reading empty rather than as plausible
// nonsense.
//
// Each cell is sampled several times and the median-ish count reported raw, so
// the host can see the separation between populated and empty for itself
// instead of trusting a threshold chosen here.
#define PROBE_LIMIT 4000

// dir 0: assume anode on the ROW  -- drive column low, sense the row.
// dir 1: assume anode on the COLUMN -- drive row low, sense the column.
// Running both is how the orientation gets established rather than assumed:
// whichever direction forward-biases the diodes is the one that shows a
// separation, and the other reverse-biases them and sees nothing.
static uint32_t probeCell(uint8_t r, uint8_t c, uint8_t dir) {
  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], INPUT);   // all high-Z
  for (uint8_t i = 0; i < NCOLS; i++) pinMode(COL_PIN[i], INPUT);

  const uint8_t drive = dir ? ROW_PIN[r] : COL_PIN[c];   // cathode side
  const uint8_t sense = dir ? COL_PIN[c] : ROW_PIN[r];   // anode side

  pinMode(drive, OUTPUT); digitalWrite(drive, LOW);
  pinMode(sense, OUTPUT); digitalWrite(sense, LOW);      // drain the node
  delayMicroseconds(200);
  pinMode(sense, INPUT_PULLUP);                          // let it rise

  uint32_t n = 0;
  while (n < PROBE_LIMIT && digitalRead(sense) == LOW) n++;
  return n;
}

// The pull-up probe above only works if the diode's forward voltage at ~40 uA
// sits BELOW the input's high threshold. With no series resistors and an
// unknown LED colour that is not guaranteed -- a green, blue or white part can
// hold above the threshold and read as empty. This probe does not depend on
// forward voltage at all.
//
// An LED is a photodiode run backwards. Reverse-bias the junction, release the
// pin, and the cell's own photocurrent discharges the charge it is holding:
//
//   1. anode LOW, cathode HIGH   -- reverse-biased, junction charged
//   2. release the cathode to INPUT, no pull at all
//   3. count until it reads LOW
//
// A fitted LED bleeds that charge away in microseconds to milliseconds under
// room light, so the count comes back well under the limit and gets SMALLER
// the brighter the room. An empty cell has no discharge path, holds its stray
// charge, and runs to the limit. The sense is therefore inverted from the
// pull-up probe -- small means PRESENT here -- and the light dependence is a
// feature: shine a lamp on the array and the separation widens.
#define PHOTO_LIMIT 150000

static uint32_t photoCell(uint8_t r, uint8_t c, uint8_t dir) {
  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], INPUT);
  for (uint8_t i = 0; i < NCOLS; i++) pinMode(COL_PIN[i], INPUT);

  const uint8_t anode   = dir ? COL_PIN[c] : ROW_PIN[r];
  const uint8_t cathode = dir ? ROW_PIN[r] : COL_PIN[c];

  pinMode(anode, OUTPUT);   digitalWrite(anode, LOW);
  pinMode(cathode, OUTPUT); digitalWrite(cathode, HIGH);   // reverse bias
  delayMicroseconds(1000);                                 // charge the junction
  pinMode(cathode, INPUT);                                 // release, NO pull

  uint32_t n = 0;
  while (n < PHOTO_LIMIT && digitalRead(cathode) == HIGH) n++;
  return n;
}

// The simplest test there is, and the one to try before anything clever:
// drive one side, pull the other side the opposite way, and read the level.
// No timing, no capacitance, no light.
//
//   pol 0: ROW driven HIGH, COLUMNS pulled DOWN  -> a fitted LED drags its
//          column UP toward (3.3 - Vf), so PRESENT reads HIGH
//   pol 1: COLUMN driven LOW, ROWS pulled UP     -> a fitted LED clamps its
//          row DOWN to Vf, so PRESENT reads LOW
//
// Both are read here because which one resolves depends on the forward
// voltage, and the two fail in opposite directions: with a low-Vf part pol 1
// separates cleanly, with a high-Vf part pol 0 does. Reporting both means the
// data says which, instead of a threshold chosen in advance deciding it.
static void routeDC(OSCMessage &msg_in) {
  const uint8_t pol = (msg_in.size() >= 1 && msg_in.isInt(0) && msg_in.getInt(0)) ? 1 : 0;
  OSCMessage m("/dc");
  m.add((intOSC_t) pol);
  for (uint8_t r = 0; r < NROWS; r++) {
    for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], INPUT);
    for (uint8_t i = 0; i < NCOLS; i++) pinMode(COL_PIN[i], INPUT);
    if (pol == 0) {
      // Rows float. Ground the columns to drain them to a known 0, then let
      // them FLOAT as plain inputs -- no pull-down, because a pull-down would
      // fight the very current being sensed. Drive one row high: wherever a
      // diode is fitted it conducts and charges its column node up; wherever
      // there is none the node stays where it was grounded.
      for (uint8_t c = 0; c < NCOLS; c++) { pinMode(COL_PIN[c], OUTPUT); digitalWrite(COL_PIN[c], LOW); }
      delayMicroseconds(1000);
      for (uint8_t c = 0; c < NCOLS; c++) pinMode(COL_PIN[c], INPUT);
      pinMode(ROW_PIN[r], OUTPUT); digitalWrite(ROW_PIN[r], HIGH);
      delayMicroseconds(3000);
      for (uint8_t c = 0; c < NCOLS; c++) m.add((intOSC_t) digitalRead(COL_PIN[c]));
    } else {
      for (uint8_t c = 0; c < NCOLS; c++) { pinMode(COL_PIN[c], OUTPUT); digitalWrite(COL_PIN[c], LOW); }
      pinMode(ROW_PIN[r], INPUT_PULLUP);
      delayMicroseconds(2000);
      // one row pin, so read it once per column position with only that column
      // driven low -- otherwise every column shares the same answer
      for (uint8_t c = 0; c < NCOLS; c++) {
        for (uint8_t i = 0; i < NCOLS; i++) pinMode(COL_PIN[i], INPUT);
        pinMode(COL_PIN[c], OUTPUT); digitalWrite(COL_PIN[c], LOW);
        pinMode(ROW_PIN[r], INPUT_PULLUP);
        delayMicroseconds(2000);
        m.add((intOSC_t) digitalRead(ROW_PIN[r]));
      }
    }
  }
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], OUTPUT);
  for (uint8_t i = 0; i < NCOLS; i++) pinMode(COL_PIN[i], OUTPUT);
  blank();
}

static void routePhoto(OSCMessage &msg_in) {
  const uint8_t dir = (msg_in.size() >= 1 && msg_in.isInt(0) && msg_in.getInt(0)) ? 1 : 0;
  OSCMessage m("/photo");
  m.add((intOSC_t) dir);
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++)
      m.add((intOSC_t) photoCell(r, c, dir));
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();

  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], OUTPUT);
  for (uint8_t i = 0; i < NCOLS; i++) pinMode(COL_PIN[i], OUTPUT);
  blank();
}

static void routeProbe(OSCMessage &msg_in) {
  const uint8_t dir = (msg_in.size() >= 1 && msg_in.isInt(0) && msg_in.getInt(0)) ? 1 : 0;
  OSCMessage m("/probe");
  m.add((intOSC_t) dir);
  for (uint8_t r = 0; r < NROWS; r++) {
    for (uint8_t c = 0; c < NCOLS; c++) {
      uint32_t a = probeCell(r, c, dir), b = probeCell(r, c, dir), d = probeCell(r, c, dir);
      uint32_t mid = a > b ? (b > d ? b : (a > d ? d : a)) : (a > d ? a : (b > d ? d : b));
      m.add((intOSC_t) mid);
    }
  }
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();

  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], OUTPUT);  // back to
  for (uint8_t i = 0; i < NCOLS; i++) pinMode(COL_PIN[i], OUTPUT);  // driving
  blank();
}

/* ----------------------------------------------------------------- inbound */

static void routeText(OSCMessage &m) {
  if (m.size() < 1 || !m.isString(0)) return;
  char buf[NROWS + 1];
  m.getString(0, buf, sizeof buf);
  for (uint8_t r = 0; r < NROWS; r++)
    code[r] = buf[r] ? charToCode(buf[r]) : ITA2_SPACE;
}

static void routeRaw(OSCMessage &m) {
  const int n = m.size() < NROWS ? m.size() : NROWS;
  for (int i = 0; i < n; i++)
    if (m.isInt(i)) code[i] = (uint8_t)(m.getInt(i) & 0x1F);
}

static void routeRowUs(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) rowUs = (uint32_t)constrain(m.getInt(0), 200, 20000);
}

static void sendHello() {
  OSCMessage h("/hello");
  h.add("BaudotOscuino").add((intOSC_t) NROWS).add((intOSC_t) NCOLS)
   .add((intOSC_t) rowUs);
  SLIPSerial.beginPacket(); h.send(SLIPSerial); SLIPSerial.endPacket();
}
static void routeHello(OSCMessage &) { sendHello(); }

void setup() {
  SLIPSerial.begin(115200);              // USB first: a sketch that cannot
                                         // report its own failure is not
                                         // debuggable on a headless board
  for (uint8_t r = 0; r < NROWS; r++) pinMode(ROW_PIN[r], OUTPUT);
  for (uint8_t c = 0; c < NCOLS; c++) pinMode(COL_PIN[c], OUTPUT);
  blank();

  const char *boot = "BAUDOT";           // something visible before any host
  for (uint8_t r = 0; r < NROWS; r++) code[r] = charToCode(boot[r]);

  sendHello();                           // usually lost; the page asks again
}

// Non-blocking receive, the extras/webserial/template.ino pattern:
// endofPacket() BEFORE available() on every pass, and RETURN rather than block
// when the buffer runs dry, so an unplug mid-frame cannot wedge loop() and
// stop the display with it.
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
  static uint32_t lastReport = 0;

  scan();                                // every pass, so the frame is steady

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/baudot/text",  routeText);
      inMsg.dispatch("/baudot/raw",   routeRaw);
      inMsg.dispatch("/baudot/rowus", routeRowUs);
      inMsg.dispatch("/baudot/probe", routeProbe);
      inMsg.dispatch("/baudot/photo", routePhoto);
      inMsg.dispatch("/baudot/dc",    routeDC);
      inMsg.dispatch("/hello",        routeHello);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (now - lastReport < 100) return;
  lastReport = now;

  // Report what is actually on the array, not what was asked for.
  OSCMessage m("/baudot");
  m.add((intOSC_t) seq++);
  for (uint8_t r = 0; r < NROWS; r++) m.add((intOSC_t) code[r]);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

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
static bool allOn;   // set by /baudot/allon; freezes the multiplexer

static void blank() {
  for (uint8_t r = 0; r < NROWS; r++) digitalWrite(ROW_PIN[r], LOW);
  for (uint8_t c = 0; c < NCOLS; c++) digitalWrite(COL_PIN[c], HIGH);
}

// Advance at most one row per call and return immediately otherwise. Called
// from loop() beside the OSC pump: the display must never own the CPU, or a
// burst of inbound packets would tear the frame and a long frame would drop
// packets.
static void scan() {
  if (allOn) return;                     // held static; do not multiplex
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
// Map the array instead of assuming it. This makes NO assumption about which
// pins are rows, which are columns, or which way the diodes face: it tries
// every ORDERED pair of the eleven pins and reports where current flows.
//
// For each pair (drive, sense): float everything, ground the sense pin to
// drain it to a known 0, release it as a plain input, drive the other pin
// high, and read. Current can only flow if a diode sits between them with its
// ANODE on the driven pin -- so a hit names both the connection and its
// polarity. The reverse pair stays dark, which is what distinguishes a diode
// from a wire or a resistor: a wire conducts BOTH ways and shows up as a hit
// in each direction.
//
// Reported as one bitmask per driven pin, bit j set when PIN_ALL[j] responded.
static const uint8_t PIN_ALL[11] = { 0, 1, 2, 3, 4, 5, 9, 10, 11, 12, 13 };

// YELLOW LEDs are the hard case, and they are what is fitted here. Forward
// drop is about 2.0 V, so driving a row at 3.3 V leaves only ~1.3 V on the
// cathode. The input's HIGH threshold is around 2.0 V, so a perfectly good
// conducting cell reads LOW -- which is why "drive a row and see which columns
// go high" finds nothing. The signal is there; the comparator is in the wrong
// place.
//
// So use the pad's HYSTERESIS rather than its high threshold. A Schmitt input
// that is already in the HIGH state does not fall back to LOW until the level
// drops below the LOW threshold, around 1.0 V:
//
//   1. ground every uninvolved pin (no sneak paths, no coupling)
//   2. drive the row HIGH
//   3. PRESET the sense column by driving it HIGH -- the pad latches high
//   4. release it to INPUT_PULLDOWN and count until it reads LOW
//
// A fitted LED sources current continuously and holds the node near 1.3-1.5 V,
// which is above the low threshold, so the pad never flips: the count runs to
// the limit. An empty cell has nothing to hold it, the pull-down wins in
// microseconds, and the count comes back near zero. Present = LARGE here.
//
// This also fails honestly: if the pull-down were to win everywhere, the answer
// is "no cells found", not a plausible pattern -- the failure that produced a
// stable, repeatable and entirely fictional message when five columns were left
// floating at once and simply held the driven row's coupled charge.
#define HYST_LIMIT 20000

static uint32_t hystCell(uint8_t r, uint8_t c) {
  for (uint8_t i = 0; i < NROWS; i++) { pinMode(ROW_PIN[i], OUTPUT); digitalWrite(ROW_PIN[i], LOW); }
  for (uint8_t k = 0; k < NCOLS; k++) { pinMode(COL_PIN[k], OUTPUT); digitalWrite(COL_PIN[k], LOW); }
  delayMicroseconds(500);

  digitalWrite(ROW_PIN[r], HIGH);            // anode side driven
  digitalWrite(COL_PIN[c], HIGH);            // preset: latch the pad HIGH
  delayMicroseconds(500);
  pinMode(COL_PIN[c], INPUT_PULLDOWN);       // now try to drag it down

  uint32_t n = 0;
  while (n < HYST_LIMIT && digitalRead(COL_PIN[c])) n++;
  return n;                                  // large = held up = LED present
}

// MEASURE the level instead of thresholding it. With a yellow LED the sense
// node lands around 1.0-1.3 V -- below every logic threshold on this part, but
// a perfectly large signal for the ADC, against an empty cell's 0 V.
//
// The ADC lives on GPIO 26/27/28 only, and none of those are in the array, so
// this needs ONE JUMPER from the column being read to GPIO 26. The column
// stays wired where it is; its own pin is simply released to INPUT so it does
// not fight, and GPIO 26 watches the same node.
//
//   /baudot/adc ,i <column 0..4>   column 0 is bit 1, the GPIO 13 column
//
// Per row: ground everything, release the column under test, drive that row
// high, and read. A populated cell charges the node through its diode to
// 3.3 - Vf; an empty one stays at the 0 V it was grounded to. No threshold is
// involved anywhere, which is the whole point.
#define ADC_SENSE_PIN 26

// Sense the array with NO extra wiring at all, by making the driving pin its
// own instrument.
//
// The RP2350's pads have selectable drive strength, and the input buffer keeps
// sampling the pad even while the pad is driving. Set a row to the WEAKEST
// drive (2 mA) and ask it to source a yellow LED that has no series resistor:
// it cannot. The pad droops until its current matches what it can supply,
// which parks it near the LED's forward voltage, about 1.9 V -- below the
// input's high threshold, so reading that same pin back returns LOW.
//
// A cell with no LED draws nothing, the pad sits at a full 3.3 V, and reads
// HIGH. The discriminator is "did the pin fail to hold itself up", and it
// needs no ADC, no jumper and no spare pin.
//
// Per cell: float every row but this one, float every column but this one,
// ground the column under test, drive the row high at 2 mA, read the ROW back.
//   LOW  = the pad sagged  = current flowed  = LED FITTED
//   HIGH = the pad held up = no current      = cell empty
#include <hardware/gpio.h>

static uint32_t sagCell(uint8_t r, uint8_t c) {
  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], INPUT);
  for (uint8_t k = 0; k < NCOLS; k++) pinMode(COL_PIN[k], INPUT);

  pinMode(COL_PIN[c], OUTPUT); digitalWrite(COL_PIN[c], LOW);   // cathode sink
  gpio_set_drive_strength(COL_PIN[c], GPIO_DRIVE_STRENGTH_12MA);

  pinMode(ROW_PIN[r], OUTPUT);
  gpio_set_drive_strength(ROW_PIN[r], GPIO_DRIVE_STRENGTH_2MA); // weakest
  digitalWrite(ROW_PIN[r], HIGH);
  delayMicroseconds(500);

  uint32_t low = 0;                       // count LOW samples: sag = LED
  for (uint8_t k = 0; k < 32; k++) if (!digitalRead(ROW_PIN[r])) low++;

  digitalWrite(ROW_PIN[r], LOW);
  gpio_set_drive_strength(ROW_PIN[r], GPIO_DRIVE_STRENGTH_4MA);
  return low;
}

// Every row HIGH and every column LOW at once, held there, with the scan
// stopped. Full drive, full brightness, no multiplexing and no duty cycle:
// whatever is fitted lights. /baudot/allon 0 releases it back to scanning.
// PRECHARGE, ISOLATE, THEN TIME A PULL-UP. This is the measurement that works
// on a yellow array, and it sidesteps the thing that defeated everything else:
// it never asks the pad to resolve 1.2 V. It only asks WHEN the pad crossed
// its threshold, and the answer depends on where the node started.
//
//   1. ground every pin           -- node at a known 0
//   2. float the column, drive the row HIGH -- a fitted LED charges the node
//      to 3.3 - Vf, about 1.2 V; an empty cell stays at 0
//   3. release the row to INPUT   -- current paths disabled, charge trapped
//   4. switch the column to INPUT_PULLUP and count until it reads HIGH
//
// The pull-up drags the node to 3.3 V through the same RC either way, so the
// only variable is the starting voltage. From 1.2 V the pad crosses its ~2.15 V
// threshold after RC*ln(2.10/1.15) = 0.60 RC; from 0 V it needs
// RC*ln(3.30/1.15) = 1.05 RC. A populated cell should therefore come back at
// roughly 57% of an empty one -- a ratio, not a level, which is why no
// threshold has to sit anywhere near the yellow drop.
//
// One pass of that is far too coarse: the whole rise is about a microsecond and
// a single count lands in the 3-7 range. So each cell is measured TRIALS times
// and the counts are summed. The quantisation averages out and the ratio
// survives; the absolute number means nothing, only present-vs-empty within a
// column.
#define CHG_TRIALS 96
#define CHG_LIMIT  2000

// DIFFERENTIAL: each cell is timed twice, identically, except that on one pass
// the row is driven HIGH during the precharge window and on the other it is
// not. Everything else -- the pin, its capacitance, its leakage, the pull-up,
// the loop -- is the same in both passes, so subtracting them cancels the pad
// differences that swamped every previous attempt. What survives is only the
// charge the diode delivered.
//
//   fitted : precharge lifts the node to ~1.2 V, so the pull-up crosses the
//            threshold sooner and the "hot" count is clearly LOWER
//   empty  : nothing to precharge; both passes start at 0 and the counts match
//
// A single pass has no resolution at all (the whole ramp is about a
// microsecond), so each is summed over many trials and only the DIFFERENCE is
// reported.
#define CHG_TRIALS 90        // x3 passes below; keeps a full read ~30 s

static uint32_t chargeOnce(uint8_t r, uint8_t c, bool precharge) {
  uint32_t total = 0;
  for (uint16_t t = 0; t < CHG_TRIALS; t++) {
    for (uint8_t i = 0; i < NROWS; i++) { pinMode(ROW_PIN[i], OUTPUT); digitalWrite(ROW_PIN[i], LOW); }
    for (uint8_t k = 0; k < NCOLS; k++) { pinMode(COL_PIN[k], OUTPUT); digitalWrite(COL_PIN[k], LOW); }
    delayMicroseconds(250);                  // drain to a known zero -- the
                                             // diode junctions hold charge, and
                                             // 60 us did not fully clear them

    pinMode(COL_PIN[c], INPUT);              // float the node
    if (precharge) digitalWrite(ROW_PIN[r], HIGH);   // charge THROUGH the diode
    // The precharge window must be at least as long as the drain, or the
    // diode never fully lifts the node and a fitted cell reads empty. These
    // two delays are a matched pair; changing one alone silently kills the
    // sensitivity.
    delayMicroseconds(300);
    pinMode(ROW_PIN[r], INPUT);              // isolate: charge is now trapped

    pinMode(COL_PIN[c], INPUT_PULLUP);       // race the pull-up to the threshold
    uint32_t n = 0;
    // Ceiling generously above the slowest column. At 400 the two highest-
    // capacitance columns hit the limit on BOTH passes, so the difference
    // cancelled to exactly zero and read as "empty" -- a saturation artefact,
    // not a measurement.
    while (n < 20000 && !digitalRead(COL_PIN[c])) n++;
    total += n;
  }
  return total;
}

/* --------------------------------------------------------------- the sound */
// A 150 ohm speaker sits across GPIO 28 and GPIO 29. Those two are channels A
// and B of the SAME PWM slice, so they can be driven in antiphase from one
// timer: B's output polarity is inverted, the speaker sees a full +/-3.3 V
// square wave instead of 0..3.3 V, and the drive is balanced with no DC
// through the coil.
//
// Pad drive is held at 2 mA deliberately. 3.3 V across 150 ohm asks for 22 mA,
// well past what a pad should source; at the 2 mA setting the pads current
// limit and the output simply sags, which is quieter but keeps the driver
// inside its ratings. There is no series resistor to do it for us.
#include <hardware/pwm.h>

#define SPK_A 28
#define SPK_B 29

// Eight notes, one diatonic octave: the symbol value mod 8 picks a scale
// degree. C4 up to C5.
static const uint16_t SCALE_HZ[8] = { 262, 294, 330, 349, 392, 440, 494, 523 };

static void soundOff() {
  const uint slice = pwm_gpio_to_slice_num(SPK_A);
  pwm_set_enabled(slice, false);
  gpio_set_function(SPK_A, GPIO_FUNC_SIO); gpio_set_dir(SPK_A, true); gpio_put(SPK_A, 0);
  gpio_set_function(SPK_B, GPIO_FUNC_SIO); gpio_set_dir(SPK_B, true); gpio_put(SPK_B, 0);
}

static void soundNote(uint16_t hz) {
  if (!hz) { soundOff(); return; }
  gpio_set_function(SPK_A, GPIO_FUNC_PWM);
  gpio_set_function(SPK_B, GPIO_FUNC_PWM);
  gpio_set_drive_strength(SPK_A, GPIO_DRIVE_STRENGTH_2MA);
  gpio_set_drive_strength(SPK_B, GPIO_DRIVE_STRENGTH_2MA);

  const uint slice = pwm_gpio_to_slice_num(SPK_A);
  uint32_t div = 1, wrap = (F_CPU / hz) - 1;
  while (wrap > 65535) { div++; wrap = (F_CPU / (div * hz)) - 1; }
  pwm_set_clkdiv(slice, (float) div);
  pwm_set_wrap(slice, (uint16_t) wrap);
  pwm_set_chan_level(slice, PWM_CHAN_A, (uint16_t)(wrap / 2));
  pwm_set_chan_level(slice, PWM_CHAN_B, (uint16_t)(wrap / 2));
  pwm_set_output_polarity(slice, false, true);      // B inverted: differential
  pwm_set_enabled(slice, true);
}

/* ------------------------------------------------------------- the display */
// Rows and columns are driven STATICALLY here rather than multiplexed: only
// one row is ever lit at a time in this sequence, so there is nothing to
// time-slice, and full duty means full brightness.

static uint8_t present[NROWS][NCOLS];      // filled by the boot-time read
static uint8_t bootCode[NROWS];            // the six symbols, read ONCE at
                                           // start-up: the array is soldered,
                                           // it cannot change while running

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

// Sequence: each row in turn, a dark gap between blocks, and after every third
// block one pass of a single lit cell travelling left to right, top to bottom.
// Rates are the original 1 s / 3 s / 250 ms halved, so the whole sequence runs
// at double speed.
#define ROW_MS     500        // 1 s per row, halved
#define BLOCK_MS  1500        // 3 s between blocks, halved
#define SNAKE_MS   125        // 1/4 s per cell, halved
#define BLOCKS_PER_SNAKE 3

enum { PH_ROWS, PH_GAP, PH_SNAKE };
static uint8_t  phase   = PH_ROWS;
static uint8_t  rowIdx  = 0;
static uint8_t  blocks  = 0;
static uint8_t  snakeIdx = 0;
static uint32_t phaseT  = 0;
static bool     showing = true;

// Non-blocking throughout -- no delay() anywhere in the sequence, so inbound
// OSC is still serviced between steps.
static void animate() {
  if (!showing) return;
  const uint32_t now = millis();
  switch (phase) {
    case PH_ROWS:
      if (now - phaseT < ROW_MS) return;
      phaseT = now;
      if (++rowIdx >= NROWS) { rowIdx = 0; phase = PH_GAP; lightNone(); soundOff(); }
      else { lightRow(rowIdx); soundNote(SCALE_HZ[bootCode[rowIdx] & 7]); }
      return;

    case PH_GAP:
      if (now - phaseT < BLOCK_MS) return;
      phaseT = now;
      if (++blocks >= BLOCKS_PER_SNAKE) {
        blocks = 0; phase = PH_SNAKE; snakeIdx = 0; lightCell(0, 0);
        soundNote(SCALE_HZ[bootCode[0] & 7]);
      } else {
        phase = PH_ROWS; rowIdx = 0; lightRow(0);
        soundNote(SCALE_HZ[bootCode[0] & 7]);
      }
      return;

    case PH_SNAKE:
      if (now - phaseT < SNAKE_MS) return;
      phaseT = now;
      if (++snakeIdx >= NROWS * NCOLS) {
        phase = PH_ROWS; rowIdx = 0; lightRow(0);
        soundNote(SCALE_HZ[bootCode[0] & 7]);
      } else {
        lightCell(snakeIdx / NCOLS, snakeIdx % NCOLS);
        soundNote(SCALE_HZ[bootCode[snakeIdx / NCOLS] & 7]);   // the row's note
      }
      return;
  }
}

// One full read of the array into present[][], and report it. Called once at
// start-up, after the display has been blanked, and available on demand.
static void readAll() {
  // Blank and let the array settle FIRST. Reading straight out of the display
  // sequence gave a different answer every time: lit cells leave their diode
  // junctions charged, which biases the very charge measurement being made.
  lightNone();
  delay(400);

  uint8_t votes[NROWS][NCOLS];
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++) votes[r][c] = 0;

  int32_t last[NROWS][NCOLS];
  // Best of three. A single pass is marginal on some cells, and a cell that
  // cannot make up its mind across three passes is reported as the majority
  // rather than as whatever the last pass happened to say.
  for (uint8_t pass = 0; pass < 3; pass++) {
    for (uint8_t r = 0; r < NROWS; r++) {
      for (uint8_t c = 0; c < NCOLS; c++) {
        const uint32_t cold = chargeOnce(r, c, false);
        const uint32_t hot  = chargeOnce(r, c, true);
        const int32_t  d    = (int32_t) cold - (int32_t) hot;
        last[r][c] = d;
        if (d > 100) votes[r][c]++;           // empty sits near 0, fitted far up
      }
    }
  }

  OSCMessage m("/present");
  for (uint8_t r = 0; r < NROWS; r++) {
    bootCode[r] = 0;
    for (uint8_t c = 0; c < NCOLS; c++) {
      present[r][c] = (votes[r][c] >= 2) ? 1 : 0;
      if (present[r][c]) bootCode[r] |= (uint8_t)(1u << c);        // bit 1 = LSB
      m.add((intOSC_t)(present[r][c] ? last[r][c] : -last[r][c])); // sign = verdict
    }
  }
  for (uint8_t r = 0; r < NROWS; r++) m.add((intOSC_t) bootCode[r]);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  lightNone();
}

// Report the stored start-up read. It does NOT re-measure: the array is
// soldered and cannot change while running, the boot read is taken from a
// cold, dark, settled array -- the only condition in which this measurement is
// repeatable -- and re-running it mid-display would replace good values with
// worse ones. /baudot/reread forces a fresh measurement if you really want it.
static void reportPresent() {
  OSCMessage m("/present");
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++) m.add((intOSC_t) present[r][c]);
  for (uint8_t r = 0; r < NROWS; r++) m.add((intOSC_t) bootCode[r]);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}
static void routeRead(OSCMessage &) { reportPresent(); }

static void routeReread(OSCMessage &) {
  const bool wasShowing = showing;
  showing = false; soundOff();
  readAll();
  showing = wasShowing; phaseT = millis();
}
static void routeShow(OSCMessage &m) {
  showing = !(m.size() >= 1 && m.isInt(0) && m.getInt(0) == 0);
  if (!showing) { lightNone(); soundOff(); }
  else { phase = PH_ROWS; rowIdx = 0; blocks = 0; phaseT = millis(); lightRow(0); }
}

static void routeCharge(OSCMessage &) {
  OSCMessage m("/charge");
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++) {
      const uint32_t cold = chargeOnce(r, c, false);
      const uint32_t hot  = chargeOnce(r, c, true);
      m.add((intOSC_t)((int32_t) cold - (int32_t) hot));   // >0 means precharged
    }
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], OUTPUT);
  for (uint8_t k = 0; k < NCOLS; k++) pinMode(COL_PIN[k], OUTPUT);
  blank();
}

static void routeAllOn(OSCMessage &m) {
  allOn = !(m.size() >= 1 && m.isInt(0) && m.getInt(0) == 0);
  for (uint8_t i = 0; i < NROWS; i++) {
    pinMode(ROW_PIN[i], OUTPUT);
    gpio_set_drive_strength(ROW_PIN[i], GPIO_DRIVE_STRENGTH_12MA);
    digitalWrite(ROW_PIN[i], allOn ? HIGH : LOW);
  }
  for (uint8_t k = 0; k < NCOLS; k++) {
    pinMode(COL_PIN[k], OUTPUT);
    gpio_set_drive_strength(COL_PIN[k], GPIO_DRIVE_STRENGTH_12MA);
    digitalWrite(COL_PIN[k], allOn ? LOW : HIGH);
  }
}

static void routeSag(OSCMessage &) {
  OSCMessage m("/sag");
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++)
      m.add((intOSC_t) sagCell(r, c));
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  for (uint8_t i = 0; i < NROWS; i++) { pinMode(ROW_PIN[i], OUTPUT); gpio_set_drive_strength(ROW_PIN[i], GPIO_DRIVE_STRENGTH_4MA); }
  for (uint8_t k = 0; k < NCOLS; k++) pinMode(COL_PIN[k], OUTPUT);
  blank();
}

static void routeAdc(OSCMessage &msg_in) {
  const uint8_t col = (msg_in.size() >= 1 && msg_in.isInt(0))
                    ? (uint8_t) constrain(msg_in.getInt(0), 0, NCOLS - 1) : 0;
  analogReadResolution(12);

  OSCMessage m("/adc");
  m.add((intOSC_t) col);
  for (uint8_t r = 0; r < NROWS; r++) {
    for (uint8_t i = 0; i < NROWS; i++) { pinMode(ROW_PIN[i], OUTPUT); digitalWrite(ROW_PIN[i], LOW); }
    for (uint8_t k = 0; k < NCOLS; k++) { pinMode(COL_PIN[k], OUTPUT); digitalWrite(COL_PIN[k], LOW); }
    delayMicroseconds(1500);                 // drain the node to a known 0
    pinMode(COL_PIN[col], INPUT);            // release it; GPIO 26 sees it too
    digitalWrite(ROW_PIN[r], HIGH);          // forward-bias this row's cell
    delay(3);                                // let it settle
    uint32_t acc = 0;
    for (uint8_t k = 0; k < 16; k++) acc += analogRead(ADC_SENSE_PIN);
    m.add((intOSC_t)(acc / 16));             // 12-bit, 0..4095 over 0..3.3 V
  }
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], OUTPUT);
  for (uint8_t k = 0; k < NCOLS; k++) pinMode(COL_PIN[k], OUTPUT);
  blank();
}

static void routeHyst(OSCMessage &) {
  OSCMessage m("/hyst");
  for (uint8_t r = 0; r < NROWS; r++)
    for (uint8_t c = 0; c < NCOLS; c++)
      m.add((intOSC_t) hystCell(r, c));
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  for (uint8_t i = 0; i < NROWS; i++) pinMode(ROW_PIN[i], OUTPUT);
  for (uint8_t k = 0; k < NCOLS; k++) pinMode(COL_PIN[k], OUTPUT);
  blank();
}

static void routeMap(OSCMessage &) {
  OSCMessage m("/map");
  for (uint8_t i = 0; i < 11; i++) {
    uint32_t mask = 0;
    for (uint8_t j = 0; j < 11; j++) {
      if (i == j) continue;
      // Hold EVERY uninvolved pin at ground rather than floating it: floating
      // neighbours both carry sneak paths and couple the driven edge into the
      // sense node, and a coupled kick on a node with no discharge path is
      // indistinguishable from conduction.
      for (uint8_t k = 0; k < 11; k++) { pinMode(PIN_ALL[k], OUTPUT); digitalWrite(PIN_ALL[k], LOW); }
      delayMicroseconds(800);
      pinMode(PIN_ALL[j], INPUT);                                   // float sense
      digitalWrite(PIN_ALL[i], HIGH);                               // drive
      delayMicroseconds(2500);
      if (digitalRead(PIN_ALL[j])) mask |= (1uL << j);
    }
    m.add((intOSC_t) mask);
  }
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  for (uint8_t k = 0; k < NROWS; k++) pinMode(ROW_PIN[k], OUTPUT);
  for (uint8_t k = 0; k < NCOLS; k++) pinMode(COL_PIN[k], OUTPUT);
  blank();
}

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
      // Every row that is not being driven is HELD AT GROUND, not left
      // floating. Two things that buys: an LED from an unused row to the
      // sensed column is reverse-biased and cannot form a sneak path, and the
      // grounded rows shield the floating column from the driven edge -- which
      // is what made a bare capacitive kick look like conduction before.
      // ONE column floats at a time and the other four stay grounded. Floating
      // all five together was the bug: adjacent floating columns picked up the
      // driven row's edge and held it, and a charge with nowhere to drain
      // reads exactly like conduction. That produced a stable, repeatable,
      // entirely fictional message. Grounded neighbours shield the one node
      // being measured.
      for (uint8_t c = 0; c < NCOLS; c++) {
        for (uint8_t i = 0; i < NROWS; i++) { pinMode(ROW_PIN[i], OUTPUT); digitalWrite(ROW_PIN[i], LOW); }
        for (uint8_t k = 0; k < NCOLS; k++) { pinMode(COL_PIN[k], OUTPUT); digitalWrite(COL_PIN[k], LOW); }
        delayMicroseconds(1000);
        pinMode(COL_PIN[c], INPUT);          // only this one floats
        digitalWrite(ROW_PIN[r], HIGH);
        delayMicroseconds(3000);
        m.add((intOSC_t) digitalRead(COL_PIN[c]));
      }
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

  // Start dark, read the whole array, then begin the sequence. The read has
  // to happen with nothing lit: it works by charging each cell's column
  // through the diode, which any driven row would swamp.
  lightNone();
  soundOff();
  delay(200);
  readAll();          // ONCE. The array is soldered; it cannot change while
                      // running, and reading it needs the display dark.

  sendHello();                           // usually lost; the page asks again
  phase = PH_ROWS; rowIdx = 0; blocks = 0; phaseT = millis();
  lightRow(0);
  soundNote(SCALE_HZ[bootCode[0] & 7]);
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

  animate();                             // the display sequence, non-blocking
  if (!showing) scan();                  // fall back to the multiplexed text mode

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/baudot/text",  routeText);
      inMsg.dispatch("/baudot/raw",   routeRaw);
      inMsg.dispatch("/baudot/rowus", routeRowUs);
      inMsg.dispatch("/baudot/probe", routeProbe);
      inMsg.dispatch("/baudot/photo", routePhoto);
      inMsg.dispatch("/baudot/dc",    routeDC);
      inMsg.dispatch("/baudot/map",   routeMap);
      inMsg.dispatch("/baudot/hyst",  routeHyst);
      inMsg.dispatch("/baudot/adc",   routeAdc);
      inMsg.dispatch("/baudot/sag",   routeSag);
      inMsg.dispatch("/baudot/allon", routeAllOn);
      inMsg.dispatch("/baudot/charge", routeCharge);
      inMsg.dispatch("/baudot/read",  routeRead);
      inMsg.dispatch("/baudot/reread", routeReread);
      inMsg.dispatch("/baudot/show",  routeShow);
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

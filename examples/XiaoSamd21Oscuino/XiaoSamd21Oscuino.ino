/*
 * XiaoSamd21Oscuino — every PTC-capable pin of a Seeed XIAO SAMD21 as an
 * Adafruit FreeTouch self-capacitance pad, streamed as OSC over SLIP-encoded
 * USB serial, with the PTC acquisition settings configurable over OSC.
 * -----------------------------------------------------------------------------
 * Board : Seeed XIAO SAMD21 (Seeeduino XIAO, ATSAMD21G18, VID 0x2886 PID 0x802F)
 * FQBN  : Seeeduino:samd:seeed_XIAO_m0
 * Libs  : "Adafruit FreeTouch Library" (1.1.3 here) and "OSC" (CNMAT, 4.0.0 here)
 * Page  : XiaoSamd21Oscuino.html beside this sketch; serve this folder
 *         (python3 -m http.server) and open it in Chrome or Edge
 * Tests : test/hardware/contractprobe.py PORT (the contract) and
 *         test/hardware/capsetupprobe.py PORT (the set-up addresses) on the
 *         board; extras/webserial/test/test-xiaosamd21-contract.mjs holds the
 *         sketch, the page and ADDRESSES.md to one parameter list.
 *         test/hardware/oscsend.py sends one message; touchwatch.py records
 *         the stream to a CSV.
 *
 * WHICH PINS. FreeTouch measures self-capacitance on the PTC's Y lines only.
 * Its rule (Adafruit_FreeTouch::getYLine) is PA02..PA07 -> Y0..Y5 and
 * PB00..PB09 -> Y6..Y15. Applied to the XIAO_m0 variant's g_APinDescription:
 *
 *   pad  pin  port  Y line  shared with
 *   0    D0   PA02  Y0      A0, DAC
 *   1    D1   PA04  Y2      A1
 *   2    D6   PB08  Y14     Serial1 TX
 *   3    D7   PB09  Y15     Serial1 RX
 *   4    D8   PA07  Y5      SPI SCK
 *   5    D9   PA05  Y3      SPI MISO
 *   6    D10  PA06  Y4      SPI MOSI
 *
 * D2..D5 are PA10, PA11, PA08, PA09, which are PTC X lines (mutual
 * capacitance). FreeTouch does not drive X lines, so this library can reach
 * seven of the eleven pins. The LED and USB pins have no Y line either.
 * The table is not hard-coded: setup() walks every pin in the variant and
 * keeps each one that has a Y line and whose library begin() succeeds;
 * /cap/pins and /diag report the result. A pad cannot also serve as Serial1 or
 * SPI, and this sketch starts neither.
 * The Y lines in the table are the library's rule, and Seeed's front pinout
 * (github.com/Seeed-Studio/OSHW-XIAO-Series, pinout/XIAO_SAMD21_front_pinout.png
 * at deb2f88e) labels all seven identically, D9 as "PA05 ... PTC-Y3" included.
 * The Arduino Zero variant's comment table prints Y[5] for PA05, which
 * collides with PA07's Y[5]; two sources to one, but D9 alone has not yet been
 * seen to move pad 5 on hardware. Seeed's Seeeduino-XIAO.md in the same
 * repository lists QTouch on exactly these seven pins (A0, A1, A6..A10).
 *
 * ADDRESSES. The OSC library's ADDRESSES.md is the contract for these:
 *
 *   /enq                  -> /enq "XiaoSamd21Oscuino", /enq/cap <n>, /enq/diag,
 *                            then /diag <s>: the pads found and the duration
 *                            of the last full scan in microseconds
 *   /cap                  -> /cap <i>xN, a fresh reading of every pad
 *   /state                -> /state <seq> <millis>, seq = the next report's
 *   /s/m                  -> /s/m <micros>
 *   /rate <ms>            stream period, 0..1000; 0 stops the stream. Echoed
 *   /heartbeat <ms>       with a deadband set, report at least this often,
 *                         0..60000; 0 disables
 *   /deadband <counts>    0 (the default): report every /rate tick. N > 0: a
 *                         tick reports only when some pad has moved by at
 *                         least N counts since the last report, 1..64
 *
 *   STREAM: one bundle per report, /state <seq> <millis> and /cap <i>xN.
 *   With the default /deadband 0 that is one bundle every /rate ms, which is
 *   what ADDRESSES.md and the repo's contractprobe.py require (at least 10
 *   bundles in about 1 s at /rate 50). Untouched pads hold their counts
 *   exactly, so change-driven reporting, CircuitPlaygroundSensors' default,
 *   would go silent at rest. Here it is opt-in, through /deadband.
 *
 * TOUCH SET-UP, ADDRESSES.md's "cap set-up". The addresses sit under the
 * capability's own root, as /mic/gain and /touch/map do. <p> is one of the
 * six parameter names below. N is the pad count.
 *
 *   /cap/<p>                 ask -> /cap/<p> <i>xN, every pad's setting
 *   /cap/<p> <v>             set every pad to v              -> echoed as above
 *   /cap/<p> <v0> .. <vN-1>  one value per pad, in /cap order -> echoed
 *   /cap/<n>/<p> <v>         pad n only, 0..N-1; incoming OSC patterns work,
 *                            so /cap/[0-3]/compcap 4000 sets four -> echoed
 *   /cap/pins                -> /cap/pins <i>xN, the Arduino pin of each pad
 *   /cap/config              -> every /cap/<p> line, then /cap/pins
 *   /cap/defaults            every pad back to the defaults  -> as /cap/config
 *
 *   p           values                   default  FreeTouch field -> PTC register
 *   oversample  1 2 4 8 16 32 64         4        oversample -> CONVCTRL.ADCACCUM
 *   resistor    0 20 50 100 (kOhm)       50       seriesres  -> SERRES.RESISTOR
 *   freq        0 none, 1 hop, 2 spread, 0        freqhop    -> FREQCTRL.FREQSPREADEN
 *               3 spread-median
 *   hop         1..16                    1        hops       -> FREQCTRL.SAMPLEDELAY = hop-1
 *   compcap     0..16383                 8192     compcap    -> COMPCAPH:COMPCAPL
 *   intcap      0..63                    63       intcap     -> INTCAP
 *
 * A value outside its set changes nothing, and the echo then shows the
 * setting still in force. Ints and floats are both accepted (floats are
 * rounded), because Max sends either.
 *
 * WHERE THE DEFAULTS COME FROM. oversample 4, resistor 50 kOhm and freq 0 are
 * what the library's own example (examples/freetouch) and the OSC repo's
 * CircuitPlaygroundSensors pass. The constructor's default resistor is 0; this
 * sketch follows the example. compcap 0x2000 and intcap 0x3F come from
 * adafruit_ptc_get_config_default(). The library never initialises `hops`;
 * hop 1 (FREQ_HOP_1) is the value a zero-initialised object holds.
 * /rate 20 and /heartbeat 2000, with their ranges and the deadband's range,
 * are CircuitPlaygroundSensors' values. Its default deadband of 4 is not
 * used, for the reason given under STREAM.
 *
 * THE READING is what the library's measure() returns: the PTC result divided
 * by the oversample count. That keeps it on one scale when oversample changes.
 * It is a count that rises with the capacitance on the pin, not picofarads,
 * and no conversion to picofarads is attempted. For scale: on a Circuit
 * Playground Express with the same library and the same defaults, untouched
 * pads read 196..275 and touched pads 686..1014 (CircuitPlaygroundSensors'
 * header). That was measured on that board, not this one.
 *
 * WHY THE SKETCH CALLS THE LIBRARY'S C LAYER. Adafruit_FreeTouch's setters
 * (setCompCap, setIntCap, setFreqHopping, ...) are private, and its
 * constructor takes only oversample, resistor and frequency mode, so compcap,
 * intcap and hop cannot be set through the class. The library's adafruit_ptc.h
 * is public and takes the whole struct adafruit_ptc_config, and
 * adafruit_ptc_start_conversion() writes every one of these settings before
 * each conversion. So the class's begin() still does the bring-up (PTC clock,
 * pin mux, Y-line enable), once per pad as every FreeTouch sketch does, and
 * each pad then keeps its own config struct for its conversions.
 * Reconfiguring is only a write to that struct; nothing is re-initialised.
 *
 * From reading the library source, not from measurement: for any freq other
 * than 0 the library writes the same registers (FREQSPREADEN = 1,
 * SAMPLEDELAY = hop-1), so in FreeTouch 1.1.3 modes 1, 2 and 3 behave
 * identically.
 *
 * STATUS, 2026-10-28, on a Seeeduino XIAO (USB serial 3316130B...302F), Seeed
 * SAMD core 1.8.6, arduino-cli 1.5.1, this file built against this
 * repository's library at 6f640f4 (what was on the pins was not recorded):
 *   - test/hardware/contractprobe.py: 10 passed, 0 failed, 1 skipped (/diag,
 *     free text), on three consecutive runs
 *   - test/hardware/capsetupprobe.py: 30 passed, 0 failed. Every set-up form
 *     is echoed, illegal values are refused, and readings move on the pad
 *     addressed and no other: compcap 4000 raised all seven by 82..87 counts
 *     (means of 5 asks), intcap 10 on pad 3 took pad 3 to 0 and left six
 *     unchanged
 *   - resting counts at the defaults: D0 460..461, D1 348..355, D6 195,
 *     D7 166..167, D8 171..172, D9 173..175, D10 208..209
 *   - one full scan of seven pads, one /diag reading at each setting: 249 us
 *     at oversample 1, 413 at 4, 1038 at 16, 3556 at 64
 *   - all seven pads rose under a finger, D0 by up to 161 counts and D6 by up
 *     to 540 (test/hardware/xiaosamd21-touch-2026-10-28.csv, taken with the
 *     build before the move, whose readPad() is this one). The touches were not one pin
 *     at a time, so that capture does not tie each pad to its pin.
 */

#include <Adafruit_FreeTouch.h>
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static const unsigned long BAUD = 115200;  // ignored on native USB

/* ----------------------------------------------------------------- state
 * Above every function on purpose: the Arduino builder puts its generated
 * prototypes before the first function definition, so any type a signature
 * mentions has to be declared before that point.
 */
static const uint8_t MAX_PADS = 16;        // the PTC has sixteen Y lines

struct Pad {
  uint8_t pin;                             // Arduino pin number
  struct adafruit_ptc_config cfg;          // everything a conversion uses
};

struct Sample {
  uint16_t v[MAX_PADS];
};

static Pad pads[MAX_PADS];
static uint8_t padCount = 0;

enum Param { P_OVERSAMPLE, P_RESISTOR, P_FREQ, P_HOP, P_COMPCAP, P_INTCAP, P_COUNT };
// Whole addresses, so the table reads as what goes on the wire; the router
// matches the part after "/cap" against the offset route("/cap") hands it.
static const char *const PARAM_ADDR[P_COUNT] = {
  "/cap/oversample", "/cap/resistor", "/cap/freq", "/cap/hop", "/cap/compcap", "/cap/intcap"
};
static const size_t CAP_ROOT = 4;          // strlen("/cap")
static const int32_t RESISTOR_KOHM[] = { 0, 20, 50, 100 };  // series_resistor_t order

// bundleIN must outlive one pass through loop(): pollOSC() returns as soon as
// the serial buffer runs dry, often part way through a packet.
static OSCBundle bundleIN;
static OSCBundle bundleOUT;

static int32_t  seq = 0;
static uint16_t reportInterval = 20;       // stream period, ms; 0 = stream off
static uint16_t heartbeatMs = 2000;        // with a deadband: report even when nothing moves
static uint16_t deadband = 0;              // 0 = every tick; N = change of N counts needed
static uint32_t lastReport = 0;
static uint32_t scanMicros = 0;            // duration of the last full scan
static bool havePrev = false;
static Sample prev;
static char bootNote[48] = "";             // a disagreement found at boot, if any

/* ------------------------------------------------------------------- PTC */

// Adafruit_FreeTouch::getYLine(), which is private, so the rule is repeated
// here: PA02..PA07 -> Y0..Y5, PB00..PB09 -> Y6..Y15, any other pin has none.
static int8_t yLineOf(uint8_t pin) {
  const PinDescription &d = g_APinDescription[pin];
  if (d.ulPort == PORTA && d.ulPin >= 2 && d.ulPin <= 7) return d.ulPin - 2;
  if (d.ulPort == PORTB && d.ulPin <= 9) return d.ulPin + 6;
  return -1;
}

static void setDefaults(struct adafruit_ptc_config &c) {
  const uint8_t pin = c.pin;
  const int8_t yline = c.yline;
  adafruit_ptc_get_config_default(&c);     // compcap 0x2000, intcap 0x3F
  c.pin = pin;
  c.yline = yline;
  c.oversample = OVERSAMPLE_4;             // as the library's example passes
  c.seriesres  = RESISTOR_50K;
  c.freqhop    = FREQ_MODE_NONE;
  c.hops       = FREQ_HOP_1;               // the library leaves this unset
}

// Every pin in the variant that has a Y line becomes a pad. The library's
// begin() returns false exactly when the pin has none, before touching any
// register, so it is safe on the USB and LED pins, and the two answers must
// agree.
static void findPads() {
  for (uint8_t pin = 0; pin < NUM_DIGITAL_PINS && padCount < MAX_PADS; pin++) {
    const int8_t y = yLineOf(pin);
    Adafruit_FreeTouch bringup(pin, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
    const bool ok = bringup.begin();
    if (ok != (y >= 0)) {
      snprintf(bootNote, sizeof bootNote, "; begin() and Y rule disagree on pin %u", pin);
      continue;
    }
    if (!ok) continue;
    Pad &p = pads[padCount++];
    const PinDescription &d = g_APinDescription[pin];
    p.pin = pin;
    p.cfg.pin = (d.ulPort == PORTB ? 32 : 0) + d.ulPin;   // ASF numbering, as the library
    p.cfg.yline = y;
    setDefaults(p.cfg);
  }
}

// One conversion on one pad: Adafruit_FreeTouch::measure(), with this pad's
// own config. The shift is measure()'s division by the oversample count.
static uint16_t readPad(const Pad &pad) {
  adafruit_ptc_start_conversion(PTC, &pad.cfg);
  while (!adafruit_ptc_is_conversion_finished(PTC)) yield();
  return adafruit_ptc_get_conversion_result(PTC) >> pad.cfg.oversample;
}

static void scan(Sample &s) {
  const uint32_t t0 = micros();
  for (uint8_t i = 0; i < padCount; i++) s.v[i] = readPad(pads[i]);
  scanMicros = micros() - t0;
}

/* ------------------------------------------------------------ parameters */

static int32_t getParam(const struct adafruit_ptc_config &c, uint8_t p) {
  switch (p) {
  case P_OVERSAMPLE: return 1L << c.oversample;
  case P_RESISTOR:   return RESISTOR_KOHM[c.seriesres];
  case P_FREQ:       return c.freqhop;
  case P_HOP:        return c.hops + 1;
  case P_COMPCAP:    return c.compcap;
  case P_INTCAP:     return c.intcap;
  }
  return 0;
}

// Writes v into c when it is one of the parameter's legal values. An illegal
// value writes nothing, so applying it to every pad leaves every pad as it was.
static bool setParam(struct adafruit_ptc_config &c, uint8_t p, int32_t v) {
  switch (p) {
  case P_OVERSAMPLE:
    for (uint8_t k = OVERSAMPLE_1; k <= OVERSAMPLE_64; k++)
      if (v == (1L << k)) { c.oversample = (oversample_t) k; return true; }
    return false;
  case P_RESISTOR:
    for (uint8_t k = RESISTOR_0; k <= RESISTOR_100K; k++)
      if (v == RESISTOR_KOHM[k]) { c.seriesres = (series_resistor_t) k; return true; }
    return false;
  case P_FREQ:
    if (v < FREQ_MODE_NONE || v > FREQ_MODE_SPREAD_MEDIAN) return false;
    c.freqhop = (freq_mode_t) v;
    return true;
  case P_HOP:
    if (v < 1 || v > 16) return false;
    c.hops = (freq_hop_t) (v - 1);
    return true;
  case P_COMPCAP:
    if (v < 0 || v > 0x3FFF) return false;     // 14 bits: COMPCAPH[5:0]:COMPCAPL
    c.compcap = (uint16_t) v;
    return true;
  case P_INTCAP:
    if (v < 0 || v > 0x3F) return false;       // 6 bits
    c.intcap = (uint8_t) v;
    return true;
  }
  return false;
}

/* --------------------------------------------------------------- outbound */

static void flush() {
  if (bundleOUT.size() == 0) return;
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

static void addCap(const Sample &s) {
  OSCMessage &m = bundleOUT.add("/cap");
  for (uint8_t i = 0; i < padCount; i++) m.add((intOSC_t) s.v[i]);
}

static void addParam(uint8_t p) {
  OSCMessage &m = bundleOUT.add(PARAM_ADDR[p]);
  for (uint8_t i = 0; i < padCount; i++) m.add((intOSC_t) getParam(pads[i].cfg, p));
}

static void addPins() {
  OSCMessage &m = bundleOUT.add("/cap/pins");
  for (uint8_t i = 0; i < padCount; i++) m.add((intOSC_t) pads[i].pin);
}

static void addConfig() {
  for (uint8_t p = 0; p < P_COUNT; p++) addParam(p);
  addPins();
}

static void sayHello() {
  bundleOUT.add("/enq").add("XiaoSamd21Oscuino");
  if (padCount) bundleOUT.add("/enq/cap").add((intOSC_t) padCount);
  bundleOUT.add("/enq/diag");
  char diag[128];
  int n = snprintf(diag, sizeof diag, "pads");
  for (uint8_t i = 0; i < padCount; i++)
    n += snprintf(diag + n, sizeof diag - n, " D%u", pads[i].pin);
  snprintf(diag + n, sizeof diag - n, "; last scan %lu us%s",
           (unsigned long) scanMicros, bootNote);
  bundleOUT.add("/diag").add(diag);
  flush();
}

static void report(const Sample &s) {
  bundleOUT.add("/state").add(seq++).add((intOSC_t) millis());
  addCap(s);
  flush();
}

/* ---------------------------------------------------------------- inbound */

static bool argInt(OSCMessage &m, int i, int32_t &v) {
  if (m.isInt(i))   { v = m.getInt(i); return true; }
  if (m.isFloat(i)) { v = lroundf(m.getFloat(i)); return true; }
  return false;
}

static char *numToOSCAddress(int n) {
  static char s[10];
  int i = 9;
  s[i--] = '\0';
  do {
    s[i--] = "0123456789"[n % 10];
    n /= 10;
  } while (n && i);
  s[i] = '/';
  return &s[i];
}

// Every /cap... message. Bare /cap is the ask; everything below it is set-up.
static void routeCap(OSCMessage &m, int off) {
  if (m.fullMatch("/cap")) {
    Sample s;
    scan(s);
    addCap(s);
    flush();
    return;
  }

  uint8_t echo = 0;                        // one bit per parameter to echo
  int32_t v;

  // /cap/<p> [<v> | <v0> .. <vN-1>]
  for (uint8_t p = 0; p < P_COUNT; p++) {
    if (!m.fullMatch(PARAM_ADDR[p] + CAP_ROOT, off)) continue;
    echo |= 1 << p;
    if (m.size() == 1 && argInt(m, 0, v)) {
      for (uint8_t i = 0; i < padCount; i++) setParam(pads[i].cfg, p, v);
    } else if (m.size() == padCount) {
      for (uint8_t i = 0; i < padCount; i++)
        if (argInt(m, i, v)) setParam(pads[i].cfg, p, v);
    }
  }

  // /cap/<n>/<p> <v>. No early return, so a pattern can select several pads.
  for (uint8_t i = 0; i < padCount; i++) {
    const int k = m.match(numToOSCAddress(i), off);
    if (!k) continue;
    for (uint8_t p = 0; p < P_COUNT; p++) {
      if (!m.fullMatch(PARAM_ADDR[p] + CAP_ROOT, off + k)) continue;
      echo |= 1 << p;
      if (argInt(m, 0, v)) setParam(pads[i].cfg, p, v);
    }
  }

  bool config = m.fullMatch("/config", off);
  if (m.fullMatch("/defaults", off) && m.size() == 0) {
    for (uint8_t i = 0; i < padCount; i++) setDefaults(pads[i].cfg);
    config = true;
  }

  if (config) {
    addConfig();
  } else {
    for (uint8_t p = 0; p < P_COUNT; p++)
      if (echo & (1 << p)) addParam(p);
    if (m.fullMatch("/pins", off)) addPins();
  }
  if (echo || config) havePrev = false;    // next report is a full one
  flush();
}

static void routeEnq(OSCMessage &m) {
  (void) m;
  sayHello();
  havePrev = false;                        // and a full report right behind it
}

// Answered with the sequence number the next report will carry, without
// consuming it, as the OSC repo's template.ino does.
static void routeState(OSCMessage &m) {
  (void) m;
  bundleOUT.add("/state").add((intOSC_t) seq).add((intOSC_t) millis());
  flush();
}

static void routeSystem(OSCMessage &m, int off) {
  if (m.fullMatch("/m", off)) bundleOUT.add("/s/m").add((intOSC_t) micros());
  flush();
}

static void routeRate(OSCMessage &m) {
  int32_t v;
  if (!argInt(m, 0, v)) return;
  reportInterval = (uint16_t) constrain(v, 0, 1000);
  havePrev = false;                        // restarting begins with a full report
  bundleOUT.add("/rate").add((intOSC_t) reportInterval);
  flush();
}

static void routeHeartbeat(OSCMessage &m) {
  int32_t v;
  if (!argInt(m, 0, v)) return;
  heartbeatMs = (uint16_t) constrain(v, 0, 60000);
}

static void routeDeadband(OSCMessage &m) {
  int32_t v;
  if (!argInt(m, 0, v)) return;
  deadband = (uint16_t) constrain(v, 0, 64);
  havePrev = false;                        // one full report at the new setting
}

// Returns true once a whole packet has been accumulated in bundleIN.
static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int avail = SLIPSerial.available();
    if (avail <= 0) return false;          // nothing buffered; try next loop()
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t) c);   // read() returns -1 on a SLIP error
    }
  }
  return true;
}

// Deadband 0 reports every tick; otherwise a move of at least N counts on any
// pad is needed.
static bool changed(const Sample &a, const Sample &b) {
  if (deadband == 0) return true;
  for (uint8_t i = 0; i < padCount; i++) {
    int32_t d = (int32_t) a.v[i] - (int32_t) b.v[i];
    if (d < 0) d = -d;
    if (d >= (int32_t) deadband) return true;
  }
  return false;
}

/* ---------------------------------------------------------------------- */

void setup() {
  SLIPSerial.begin(BAUD);
  findPads();
  Sample s;
  scan(s);                                 // so /diag carries a real scan time
  delay(300);                              // let the host finish enumerating
  sayHello();
}

void loop() {
  if (pollOSC()) {
    if (!bundleIN.hasError()) {
      bundleIN.dispatch("/enq",       routeEnq);
      bundleIN.dispatch("/state",     routeState);
      bundleIN.route("/s",            routeSystem);
      bundleIN.route("/cap",          routeCap);
      bundleIN.dispatch("/rate",      routeRate);
      bundleIN.dispatch("/heartbeat", routeHeartbeat);
      bundleIN.dispatch("/deadband",  routeDeadband);
    }
    bundleIN.empty();
  }

  if (reportInterval == 0 || padCount == 0) return;
  const uint32_t now = millis();
  if ((uint32_t)(now - lastReport) < reportInterval) return;

  Sample s;
  scan(s);
  const bool due = heartbeatMs && (uint32_t)(now - lastReport) >= heartbeatMs;
  if (!havePrev || due || changed(s, prev)) {
    lastReport = now;
    prev = s;
    havePrev = true;
    report(s);
  }
}

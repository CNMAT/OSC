/*
 * XiaoSamd21Oscuino — the pins of a Seeed XIAO SAMD21 as capacitive touch
 * pads, measured by one of three libraries chosen live over OSC, streamed as
 * OSC over SLIP-encoded USB serial, with each library's settings configurable
 * over OSC.
 * -----------------------------------------------------------------------------
 * Board : Seeed XIAO SAMD21 (Seeeduino XIAO, ATSAMD21G18, VID 0x2886 PID 0x802F)
 * FQBN  : Seeeduino:samd:seeed_XIAO_m0
 * Libs  : "Adafruit FreeTouch Library" (1.1.3), "CapacitiveSensor" (0.5.1)
 *         and "Nintendo Extension Ctrl" (0.8.3) from the Library Manager;
 *         FastTouch (0.91) from github.com/adrianfreed/FastTouch, which is not
 *         in the Library Manager; "OSC" (CNMAT, 4.0.0). Built here against
 *         FastTouch at fef329c and CapacitiveSensor at aa01848.
 * Page  : XiaoSamd21Oscuino.html beside this sketch; serve this folder
 *         (python3 -m http.server) and open it in Chrome or Edge
 * Tests : test/hardware/contractprobe.py PORT (the contract) and
 *         test/hardware/capsetupprobe.py PORT (the set-up addresses and the
 *         library switch) on the board; extras/webserial/test/
 *         test-xiaosamd21-contract.mjs holds the sketch, the page and
 *         ADDRESSES.md to one list of libraries and parameters.
 *         test/hardware/oscsend.py sends one message; touchwatch.py records
 *         the stream to a CSV; captouchprobe.py PORT LIB... waits for a touch
 *         and says which pad rose; xiaosamd21capture.py records the bytes the
 *         page test decodes.
 *
 * THREE LIBRARIES, ONE AT A TIME. /cap/lib <s> chooses which measures:
 *
 *   FreeTouch         Adafruit's driver for the SAMD21's PTC (peripheral touch
 *                     controller): self-capacitance on the PTC's Y lines. The
 *                     library at power-up.
 *   FastTouch         Adrian Freed's: drive the pin LOW for 1 ms, release it to
 *                     its internal pull-up, and count how many of 23
 *                     back-to-back port reads still see it LOW; 0..23
 *                     (fastTouchMax() on SAMD21). Any pin with a pull-up; no
 *                     settings.
 *   CapacitiveSensor  Paul Badger's, maintained by Paul Stoffregen: a send pin
 *                     drives each receive pin through an external resistor,
 *                     and the count is how long the receive pin takes to
 *                     follow. Needs that wiring, so it has no pins until you
 *                     choose them.
 *
 * A switch turns the PTC off and releases every header pin to a plain input
 * first, because the libraries hold pins differently: FreeTouch hands them to
 * the PTC through the pin multiplexer, and the core's pinMode(), which
 * FastTouch and CapacitiveSensor both call, rewrites the whole PINCFG register
 * and so takes them back. That was not enough on its own: with the PTC left
 * enabled and its Y lines on, FastTouch read nothing on the seven Y-line pins
 * (see ptcRelease()). Returning to FreeTouch reruns its bring-up. Each
 * library's settings survive a switch. After a switch the board sends its /enq
 * bundle again, because the pad count and /cap/pins change with the library.
 *
 * WHICH PINS.
 *
 * FreeTouch measures self-capacitance on the PTC's Y lines only. Its rule
 * (Adafruit_FreeTouch::getYLine) is PA02..PA07 -> Y0..Y5 and PB00..PB09 ->
 * Y6..Y15. Applied to the XIAO_m0 variant's g_APinDescription:
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
 * capacitance). FreeTouch does not drive X lines, so it reaches seven of the
 * eleven pins. The LED and USB pins have no Y line either. The table is not
 * hard-coded: setup() walks every pin in the variant and keeps each one that
 * has a Y line and whose library begin() succeeds. The Y lines are the
 * library's rule, and Seeed's front pinout (github.com/Seeed-Studio/
 * OSHW-XIAO-Series, pinout/XIAO_SAMD21_front_pinout.png at deb2f88e) labels
 * all seven identically, D9 as "PA05 ... PTC-Y3" included. The Arduino Zero
 * variant's comment table prints Y[5] for PA05, which collides with PA07's
 * Y[5]; two sources to one, but D9 alone has not yet been seen to move pad 5
 * on hardware. Seeed's Seeeduino-XIAO.md in the same repository lists QTouch on
 * exactly these seven pins (A0, A1, A6..A10).
 *
 * FastTouch measures D0..D10, every header pin: the variant's pins 0..10, whose
 * table calls them the "Digital & Analog pins" (11..13 are the LEDs, 14..16
 * USB).
 *
 * CapacitiveSensor measures the receive pins you choose with /cap/recv, each
 * wired to the send pin you choose with /cap/send through a resistor, all from
 * D0..D10. Its example sketch uses "a high value resistor e.g. 10M between send
 * pin and receive pin" and says "50K - 50M. Larger resistor values yield larger
 * sensor values."
 *
 * A pad cannot also serve as Serial1 or SPI, and this sketch starts neither.
 *
 * ADDRESSES. The OSC library's ADDRESSES.md is the contract for these:
 *
 *   /enq                  -> /enq "XiaoSamd21Oscuino", /enq/cap <n> when the
 *                            library measuring has pads, /enq/i2c, /enq/wii
 *                            <s> while a Wii extension is plugged in, then
 *                            /enq/diag and /diag <s>: the library, its pads,
 *                            the duration of its last full scan in
 *                            microseconds, and the PTC read back ("PTC on, Y
 *                            0xc03d": enabled, and YENABLE's sixteen Y lines
 *                            as a mask); in I2C mode, the pull-ups and the
 *                            extension's kind and identity bytes instead
 *   /cap                  -> /cap <i>xN, a fresh reading of every pad (no
 *                            reply when the library measuring has no pads)
 *   /state                -> /state <seq> <millis>, seq = the next report's
 *   /s/m                  -> /s/m <micros>
 *   /rate <ms>            stream period, 0..1000; 0 stops the stream. Echoed
 *   /heartbeat <ms>       with a deadband set, report at least this often,
 *                         0..60000; 0 disables
 *   /deadband <counts>    0 (the default): report every /rate tick. N > 0: a
 *                         tick reports only when some pad has moved by at
 *                         least N counts since the last report, 1..64
 *   /i2c [<i>]            ask, or 1 to give D4 and D5 to the I2C bus and 0 to
 *                         give them back to touch -> /i2c <i>, /i2c/pull
 *   /i2c/scan             -> /i2c/scan <i>..., each 7-bit address from 0x08
 *                            to 0x77 that answered a one-byte read (only
 *                            while on)
 *   /i2c/pull             -> /i2c/pull <sda> <scl>, 1 where something outside
 *                            the board held the line HIGH against the SAMD21's
 *                            own pull-down, read when the bus was switched on
 *   /wii                  -> /wii <i>..., the extension's fields in its kind's
 *                            order (ADDRESSES.md "wii fields"); no reply when
 *                            none is plugged in
 *   /wii/raw              -> /wii/raw <i>..., the bytes the library read, and
 *                            the /wii from that same update()
 *   /wii/id               -> /wii/id <s> <i>x6, the kind and the six identity
 *                            bytes; the bare address when none is plugged in
 *
 *   STREAM: one bundle per report, /state <seq> <millis> and /cap <i>xN;
 *   /state alone while the library measuring has no pads (CapacitiveSensor
 *   before its pins are chosen), because it is the stream's heartbeat.
 *   With the default /deadband 0 that is one bundle every /rate ms, which is
 *   what ADDRESSES.md and the repo's contractprobe.py require (at least 10
 *   bundles in about 1 s at /rate 50). Untouched pads hold their counts
 *   exactly, so change-driven reporting, CircuitPlaygroundSensors' default,
 *   would go silent at rest. Here it is opt-in, through /deadband. A scan
 *   longer than /rate sets the pace instead: FastTouch's 1 ms discharge per
 *   pin makes its scan of eleven at least 11 ms.
 *
 *   In I2C mode the bundle is /state and, while an extension is plugged in,
 *   /wii, every /rate tick; /deadband and /heartbeat apply to touch only.
 *
 * TOUCH SET-UP, ADDRESSES.md's "cap set-up". The addresses sit under the
 * capability's own root, as /mic/gain and /touch/map do. <p> is one of the
 * parameter names below; it belongs to one library, and N is the number of
 * that library's pads, whichever library is measuring.
 *
 *   /cap/lib [<s>]           ask, or choose FreeTouch, FastTouch or
 *                            CapacitiveSensor          -> /cap/lib <s>
 *   /cap/<p>                 ask -> /cap/<p> <i>xN, every pad's setting
 *   /cap/<p> <v>             set every pad to v              -> echoed as above
 *   /cap/<p> <v0> .. <vN-1>  one value per pad, in its library's pad order
 *   /cap/<n>/<p> <v>         pad n only, 0..N-1; incoming OSC patterns work,
 *                            so /cap/[0-3]/compcap 4000 sets four -> echoed
 *   /cap/send [<i>]          CapacitiveSensor's send pin, 0..10
 *                                                       -> /cap/send [<i>]
 *   /cap/recv [<i> ...]      its receive pins, one pad each, 0..10
 *                                                       -> /cap/recv <i>...
 *   /cap/pins                -> /cap/pins <i>xN, the measuring library's pins
 *   /cap/config              -> /cap/lib, the measuring library's /cap/<p>
 *                            lines, /cap/send and /cap/recv for
 *                            CapacitiveSensor, then /cap/pins
 *   /cap/defaults            the measuring library back to its defaults
 *                            (CapacitiveSensor: no pins) -> as /cap/config
 *
 *   p           library           values                  default  writes / calls
 *   oversample  FreeTouch         1 2 4 8 16 32 64        4        CONVCTRL.ADCACCUM
 *   resistor    FreeTouch         0 20 50 100 (kOhm)      50       SERRES.RESISTOR
 *   freq        FreeTouch         0 none, 1 hop,          0        FREQCTRL.FREQSPREADEN
 *                                 2 spread, 3 spread-median
 *   hop         FreeTouch         1..16                   1        FREQCTRL.SAMPLEDELAY = hop-1
 *   compcap     FreeTouch         0..16383                8192     COMPCAPH:COMPCAPL
 *   intcap      FreeTouch         0..63                   63       INTCAP
 *   samples     CapacitiveSensor  1..255                  30       capacitiveSensorRaw(samples)
 *   timeout     CapacitiveSensor  1..60000                2000     set_CS_Timeout_Millis()
 *
 * A value outside its set changes nothing, and the echo then shows the
 * setting still in force. Ints and floats are both accepted (floats are
 * rounded), because Max sends either. A send pin among the receive pins, a
 * receive pin given twice, or a pin outside 0..10 is refused the same way.
 *
 * WHERE THE DEFAULTS COME FROM. oversample 4, resistor 50 kOhm and freq 0 are
 * what FreeTouch's own example (examples/freetouch) and the OSC repo's
 * CircuitPlaygroundSensors pass. FreeTouch's constructor defaults the resistor
 * to 0; this sketch follows the example. compcap 0x2000 and intcap 0x3F come
 * from adafruit_ptc_get_config_default(). FreeTouch never initialises `hops`;
 * hop 1 (FREQ_HOP_1) is the value a zero-initialised object holds.
 * samples 30 is what CapacitiveSensor's example (CapacitiveSensorSketch)
 * passes; timeout 2000 is its constructor's, a count that the library scales
 * by F_CPU and a loopTimingFactor of 310 its source calls "determined
 * empirically - a hack", so it is milliseconds only approximately. The upper
 * bound of 60000 is /heartbeat's. /rate 20 and /heartbeat 2000, with their
 * ranges and the deadband's range, are CircuitPlaygroundSensors' values. Its
 * default deadband of 4 is not used, for the reason given under STREAM.
 *
 * THE READINGS are each library's own count, none of them picofarads, and no
 * conversion is attempted.
 *   FreeTouch: what measure() returns, the PTC result divided by the
 *   oversample count, so it stays on one scale when oversample changes. On a
 *   Circuit Playground Express with the same library and defaults, untouched
 *   pads read 196..275 and touched ones 686..1014 (CircuitPlaygroundSensors'
 *   header), measured on that board, not this one.
 *   FastTouch: fastTouchRead(), 0..23; its README: "0 = no touch, higher =
 *   more capacitance".
 *   CapacitiveSensor: capacitiveSensorRaw(samples), the total over `samples`
 *   charge cycles, or the library's own -2 when a cycle timed out (no
 *   resistor, or more capacitance than the timeout allows), passed through
 *   unchanged.
 *
 * WHY FREETOUCH IS DRIVEN THROUGH ITS C LAYER. Adafruit_FreeTouch's setters
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
 * than 0 FreeTouch writes the same registers (FREQSPREADEN = 1,
 * SAMPLEDELAY = hop-1), so in FreeTouch 1.1.3 modes 1, 2 and 3 behave
 * identically.
 *
 * WII EXTENSIONS, through NintendoExtensionCtrl (Library Manager "Nintendo
 * Extension Ctrl" 0.8.3, by David Madison, LGPL-3.0, used as installed). The
 * library finds, initialises, identifies and reads the extension, with
 * ExtensionPort::connect(), getControllerType() and update() as its
 * MultipleTypes example uses them; this sketch has no protocol code of its
 * own. WiiExtension.h binds the library's getters to /wii: each field is one
 * getter, named as the library names it, in the order its header declares
 * it. ADDRESSES.md's "wii fields" lists them, and the page draws each kind.
 * A type the library does not know is sent as "Unknown", with the six bytes
 * the library read for it.
 *
 * Wiring, from Adafruit's guide to its Wii Nunchuck Breakout Adapter (product
 * 4836) and the adapter's schematic (adafruit/Adafruit-Wii-Nunchuck-Breakout-
 * Adapter-PCB): the header is VIN, 3V, GND, SCL, SDA. VIN goes to the XIAO's
 * 3V3, not 5V: the adapter pulls the microcontroller side of SCL and SDA up
 * to VIN through 10K, and Seeed's wiki says of the XIAO SAMD21's pins
 * "Voltage input connected to general I/O pins may cause chip damage if it'
 * higher than 3.3V." SCL goes to D5 and SDA to D4, the variant's
 * PIN_WIRE_SCL and PIN_WIRE_SDA, on SERCOM2.
 *
 * The bus runs at 100 kHz, the core's TWI_CLOCK. Measured on this board with
 * a Nunchuk through that adapter, VIN on 3V3, 2026-10-29, by
 * test/hardware/WiiLibProbe (the library alone, no OSC): at 100 kHz,
 * connect() succeeded and update() succeeded 90 times of 90 a second, over
 * 8 s; at 400 kHz, connect() 7..18 of 9..23 and update() 1..56 of 19..62 a
 * second; back at 100 kHz, 90 of 90 again.
 *
 * The scan, /i2c/scan, is not the library's: a one-byte read of each address
 * from 0x08 to 0x77, 1 ms apart. Same board, Nunchuk and 100 kHz, ten probes
 * of 0x52 alone, each 1 ms apart: an empty write (what most scanners send)
 * was answered 9 times of 10, a one-byte read 10 of 10. In scans that went
 * from address to address with no gap it answered neither (how many such
 * scans ran was not recorded).
 *
 * Every transaction is followed by 1 ms of idle bus (I2C_GAP_MS), the
 * library's included. Same board and Nunchuk, three runs of ten connects
 * each: a requestIdentity() straight after connect() returned succeeded 0
 * times of 30, and the update() straight after that 0 of 30; 1 ms after each,
 * 30 of 30 both, and 2 ms after, 30 of 30 both. Why the extension needs the
 * gap was not looked into.
 *
 * STATUS, 2026-10-29: this build (63,908 bytes; arduino-cli 1.5.1, Seeeduino
 * SAMD core 1.8.6, NintendoExtensionCtrl 0.8.3) on one XIAO SAMD21 with touch
 * wires on its pins, no resistors, and on D4/D5 Adafruit's adapter (VIN on
 * 3V3) holding a Nunchuk:
 *   capsetupprobe.py  54 passed, 0 failed: every setting's echo and its effect
 *                     on the readings, the library switch, CapacitiveSensor's
 *                     pin rules, the PTC read back (on, Y 0xc03d under
 *                     FreeTouch; off, Y 0x0000 under the other two), and OSC
 *                     answered between pads (echoes in 0.16..0.58 s while
 *                     four timed-out pads made 2.65 s scans)
 *   contractprobe.py  13 passed, 0 failed, 1 skipped (/diag, free text)
 *   wiiprobe.py       13 passed, 0 failed: /i2c on and off, the scan finding
 *                     0x52 alone, /wii/id Nunchuk 00 00 a4 20 00 00, 50 of 50
 *                     /wii/raw asks answered with one value per field, each of
 *                     its type and in its range, and 50 /wii in 1 s at /rate 20
 * The same probes failed on the builds they are meant to catch: capsetupprobe's
 * PTC checks on this sketch without ptcRelease(), and its latency check on an
 * earlier build (one echo of five back within 4 s) and on one that stepped
 * the pads but kept the template's pollOSC() (1.69..1.96 s); wiiprobe's
 * identity check on the build without I2C_GAP_MS (six zero bytes).
 * By hand through the page, 2026-10-28, after ptcRelease(): FastTouch rose on
 * all eleven pins, and FreeTouch "works where it should" (the user's words);
 * observed, not recorded.
 * Not verified: D9 as pad 5 in a recorded isolated touch; CapacitiveSensor
 * with resistors fitted; noise, under any setting; on this build, any Wii
 * extension but the Nunchuk (the Classic Controller is on hand), which way
 * each stick and button reads, and the page drawing a live extension.
 */

#include <Adafruit_FreeTouch.h>
#include <FastTouch.h>
#include <CapacitiveSensor.h>
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <Wire.h>
#include "WiiExtension.h"

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#define SLIP_PORT thisBoardsSerialUSB      // the port under SLIPSerial, for pollOSC()
#else
SLIPEncodedSerial SLIPSerial(Serial);
#define SLIP_PORT Serial
#endif

static const unsigned long BAUD = 115200;  // ignored on native USB

/* ----------------------------------------------------------------- state
 * Above every function on purpose: the Arduino builder puts its generated
 * prototypes before the first function definition, so any type a signature
 * mentions has to be declared before that point.
 */
static const uint8_t MAX_PADS = 16;        // the PTC has sixteen Y lines
static const uint8_t HEADER_PINS = 11;     // D0..D10, the variant's pins 0..10

enum Lib { LIB_FREETOUCH, LIB_FASTTOUCH, LIB_CAPSENSE, LIB_COUNT };
static const char *const LIB_NAME[LIB_COUNT] = { "FreeTouch", "FastTouch", "CapacitiveSensor" };

struct Pad {                               // a FreeTouch pad
  uint8_t pin;                             // Arduino pin number
  struct adafruit_ptc_config cfg;          // everything a conversion uses
};

struct CsPad {                             // a CapacitiveSensor receive pin
  uint8_t pin;
  uint8_t samples;
  uint16_t timeoutMs;
  CapacitiveSensor *cs;                    // only while CapacitiveSensor measures
};

struct Sample {
  int32_t v[MAX_PADS];
};

static Pad ptcPads[MAX_PADS];
static uint8_t ptcCount = 0;
static uint8_t ftPins[MAX_PADS];
static uint8_t ftCount = 0;
static CsPad csPads[MAX_PADS];
static uint8_t csCount = 0;
static int8_t csSend = -1;                 // -1 until chosen; never sent as a value
static uint8_t lib = LIB_FREETOUCH;

enum Param { P_OVERSAMPLE, P_RESISTOR, P_FREQ, P_HOP, P_COMPCAP, P_INTCAP,
             P_SAMPLES, P_TIMEOUT, P_COUNT };
// Whole addresses, so the table reads as what goes on the wire; the router
// matches the part after "/cap" against the offset route("/cap") hands it.
static const char *const PARAM_ADDR[P_COUNT] = {
  "/cap/oversample", "/cap/resistor", "/cap/freq", "/cap/hop", "/cap/compcap", "/cap/intcap",
  "/cap/samples", "/cap/timeout"
};
static const uint8_t PARAM_LIB[P_COUNT] = {
  LIB_FREETOUCH, LIB_FREETOUCH, LIB_FREETOUCH, LIB_FREETOUCH, LIB_FREETOUCH, LIB_FREETOUCH,
  LIB_CAPSENSE, LIB_CAPSENSE
};
static const size_t CAP_ROOT = 4;          // strlen("/cap")
static const int32_t RESISTOR_KOHM[] = { 0, 20, 50, 100 };  // series_resistor_t order
static const uint8_t CS_SAMPLES_DEFAULT = 30;     // CapacitiveSensor's example
static const uint16_t CS_TIMEOUT_DEFAULT = 2000;  // CapacitiveSensor's constructor

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
static int8_t scanLib = -1;                // the library that scan belonged to
static bool havePrev = false;
static Sample prev;
static char bootNote[48] = "";             // a disagreement found at boot, if any

// The stream's scan in progress, read one pad per loop() pass (see loop()).
static Sample roundS;
static uint8_t roundNext = 0;              // its next pad; 0 = none in progress
static uint32_t roundStart = 0;            // millis() when it began
static uint32_t roundMicros = 0;           // time spent reading pads, OSC excluded

// The I2C bus on D4 (SDA) and D5 (SCL), the variant's Wire pins. While it is
// on, no pads are measured: FastTouch and CapacitiveSensor would take those
// pins back.
static bool i2cOn = false;
static int8_t pullSda = -1, pullScl = -1;  // 1: read HIGH against the internal pull-down; -1 unread
static bool busLow = false;                // a line stayed LOW with pull-ups on: bus not started
static int8_t wiiKind = -1;                // WII_KINDS index of the connected extension, or -1
static bool wiiPresent = false;            // NintendoExtensionCtrl connected to something
static uint8_t wiiId[6];
static uint32_t lastProbe = 0;
static const uint16_t WII_PROBE_MS = 250;  // how often an empty bus is asked again: a choice
// The bus is left idle this long after every transaction: between the scan's
// addresses, and after each of the library's connect(), requestIdentity() and
// update(). Measured on this board (2026-10-29, Wire at 100 kHz, a Nunchuk on
// Adafruit's adapter, test/hardware/WiiLibProbe): 0x52 probed 1 ms apart
// answered 10 of 10 one-byte reads, and inside a scan with no gap, none; a
// requestIdentity() 0 ms after connect() returned succeeded 0 times of 30, and
// the update() straight after it 0 of 30, while 1 ms after, both succeeded 30
// of 30, as at 2 ms. 1 ms is the shortest gap tried that worked.
static const uint8_t I2C_GAP_MS = 1;

/* ------------------------------------------------------------- FreeTouch */

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
static void findPtcPads() {
  for (uint8_t pin = 0; pin < NUM_DIGITAL_PINS && ptcCount < MAX_PADS; pin++) {
    const int8_t y = yLineOf(pin);
    Adafruit_FreeTouch bringup(pin, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
    const bool ok = bringup.begin();
    if (ok != (y >= 0)) {
      snprintf(bootNote, sizeof bootNote, "; begin() and Y rule disagree on pin %u", pin);
      continue;
    }
    if (!ok) continue;
    Pad &p = ptcPads[ptcCount++];
    const PinDescription &d = g_APinDescription[pin];
    p.pin = pin;
    p.cfg.pin = (d.ulPort == PORTB ? 32 : 0) + d.ulPin;   // ASF numbering, as the library
    p.cfg.yline = y;
    setDefaults(p.cfg);
  }
}

// Back to the PTC after another library has had the pins: begin() again for
// each pad (clock, pin mux, Y enable). The pads' settings are untouched.
static void ptcBringUp() {
  for (uint8_t i = 0; i < ptcCount; i++) {
    Adafruit_FreeTouch bringup(ptcPads[i].pin, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
    bringup.begin();
  }
}

// The PTC off, and its Y lines with it, before another library takes the pins.
// With the PTC left enabled and the Y lines on, pinMode() alone was not
// enough: under FastTouch only D2..D5, the four pins with no Y line, rose
// under a finger (2026-10-28, the touch probe and by hand). With this release
// FastTouch rose on all eleven pins and FreeTouch still on its seven
// (2026-10-28, by hand). Which of the two writes is the one that matters was
// not isolated. ptcBringUp() turns both back on, and /diag reads both back.
// Register names are FreeTouch's samd21_ptc_component.h.
static void ptcRelease() {
  while (PTC->CTRLB.bit.SYNCFLAG) ;
  PTC->CTRLA.bit.ENABLE = 0;
  while (PTC->CTRLB.bit.SYNCFLAG) ;
  PTC->YENABLEL.reg = 0;
  PTC->YENABLEH.reg = 0;
  while (PTC->CTRLB.bit.SYNCFLAG) ;
}

// One conversion on one pad: Adafruit_FreeTouch::measure(), with this pad's
// own config. The shift is measure()'s division by the oversample count.
static uint16_t readPtc(const Pad &pad) {
  adafruit_ptc_start_conversion(PTC, &pad.cfg);
  while (!adafruit_ptc_is_conversion_finished(PTC)) yield();
  return adafruit_ptc_get_conversion_result(PTC) >> pad.cfg.oversample;
}

/* ------------------------------------------------------------- FastTouch */

static void findFtPins() {
  for (uint8_t pin = 0; pin < HEADER_PINS; pin++) ftPins[ftCount++] = pin;
}

/* ------------------------------------------------------ CapacitiveSensor */

// Constructing a CapacitiveSensor sets its pins (send OUTPUT LOW, receive
// INPUT), so the objects exist only while CapacitiveSensor is measuring.
static void csDrop() {
  for (uint8_t i = 0; i < csCount; i++) {
    delete csPads[i].cs;
    csPads[i].cs = nullptr;
  }
}

static void csBuild() {
  csDrop();
  if (csSend < 0) return;
  for (uint8_t i = 0; i < csCount; i++) {
    csPads[i].cs = new CapacitiveSensor(csSend, csPads[i].pin);
    csPads[i].cs->set_CS_Timeout_Millis(csPads[i].timeoutMs);
  }
}

static bool isRecv(int32_t pin) {
  for (uint8_t i = 0; i < csCount; i++)
    if (csPads[i].pin == pin) return true;
  return false;
}

/* --------------------------------------------------- the library measuring */

static uint8_t padCountOf(uint8_t l) {     // pads measured while l is chosen
  if (i2cOn) return 0;                     // the bus has D4 and D5; nothing is measured
  if (l == LIB_FREETOUCH) return ptcCount;
  if (l == LIB_FASTTOUCH) return ftCount;
  return csSend >= 0 ? csCount : 0;
}

static uint8_t setupCountOf(uint8_t l) {   // pads that carry l's settings
  if (l == LIB_FREETOUCH) return ptcCount;
  if (l == LIB_CAPSENSE) return csCount;
  return 0;
}

static uint8_t pinOf(uint8_t l, uint8_t i) {
  if (l == LIB_FREETOUCH) return ptcPads[i].pin;
  if (l == LIB_FASTTOUCH) return ftPins[i];
  return csPads[i].pin;
}

static void releasePins() {
  for (uint8_t pin = 0; pin < HEADER_PINS; pin++) pinMode(pin, INPUT);
}

static void selectLib(uint8_t l) {
  csDrop();
  ptcRelease();
  releasePins();
  lib = l;
  if (lib == LIB_FREETOUCH) ptcBringUp();
  if (lib == LIB_CAPSENSE) csBuild();
  havePrev = false;
}

static int32_t readOne(uint8_t i) {
  if (lib == LIB_FREETOUCH) return readPtc(ptcPads[i]);
  if (lib == LIB_FASTTOUCH) {
    const uint8_t pin = ftPins[i];
    return fastTouchRead(pin);
  }
  CapacitiveSensor *cs = csPads[i].cs;
  return cs ? cs->capacitiveSensorRaw(csPads[i].samples) : -1;   // -1: the library's "bad pin"
}

static void scan(Sample &s) {
  const uint8_t n = padCountOf(lib);
  const uint32_t t0 = micros();
  for (uint8_t i = 0; i < n; i++) s.v[i] = readOne(i);
  scanMicros = micros() - t0;
  scanLib = lib;
}

/* -------------------------------------------------------------------- I2C */

static void wiiForget() {
  wiiKind = -1;
  wiiPresent = false;
  memset(wiiId, 0, sizeof wiiId);
  lastProbe = millis();
}

// Before Wire has the pins, each line is read with the SAMD21's own pull-down
// on: one that still reads HIGH has a stronger pull-up outside the chip. Then
// the internal pull-ups go on, which pinPeripheral() leaves set when Wire
// takes the pins (it ORs PMUXEN into PINCFG). A line that reads LOW even so
// is held down by something, and the bus is not started: the core's Wire
// waits on the bus with no timeout.
static bool i2cStart() {
  csDrop();
  ptcRelease();
  releasePins();
  pinMode(SDA, INPUT_PULLDOWN);
  pinMode(SCL, INPUT_PULLDOWN);
  delay(1);
  pullSda = digitalRead(SDA);
  pullScl = digitalRead(SCL);
  pinMode(SDA, INPUT_PULLUP);
  pinMode(SCL, INPUT_PULLUP);
  delay(1);
  busLow = !digitalRead(SDA) || !digitalRead(SCL);
  if (busLow) {
    selectLib(lib);
    return false;
  }
  wiiPort.begin();                         // the library's Wire.begin(): 100 kHz, the core's TWI_CLOCK
  i2cOn = true;
  wiiForget();
  lastProbe = millis() - WII_PROBE_MS;     // probe on the first tick
  roundNext = roundMicros = 0;
  havePrev = false;
  return true;
}

static void i2cStop() {
  Wire.end();
  i2cOn = false;
  wiiForget();
  selectLib(lib);                          // the touch library measures again
}

// An extension, found and identified by NintendoExtensionCtrl: its connect()
// initialises it, reads its identity and runs the matching type's own
// start-up. connect() keeps only the type, so the six identity bytes are read
// again with the library's requestIdentity(). Each call is followed by
// I2C_GAP_MS of idle bus, which the read after connect() needs.
static bool wiiProbe() {
  const bool found = wiiPort.connect();
  delay(I2C_GAP_MS);
  if (!found) return false;
  wiiKind = wiiKindOf(wiiPort.getControllerType());
  if (!wiiPort.requestIdentity(wiiId)) memset(wiiId, 0, sizeof wiiId);
  delay(I2C_GAP_MS);
  wiiPresent = true;
  return true;
}

// The library's update(): a fresh report into its data, then I2C_GAP_MS of
// idle bus.
static bool wiiRead() {
  const bool ok = wiiPort.update();
  delay(I2C_GAP_MS);
  return ok;
}

// A fresh report. When it fails, connect() again, as the library's
// MultipleTypes example does; false only if that fails too.
static bool wiiUpdate(bool &changed) {
  changed = false;
  if (wiiRead()) return true;
  const int8_t was = wiiKind;
  if (wiiProbe() && wiiRead()) {
    changed = wiiKind != was;
    return true;
  }
  wiiForget();
  changed = true;
  return false;
}

static const char *wiiName() {
  return wiiKind >= 0 ? WII_KINDS[wiiKind].name : "Unknown";
}

/* ------------------------------------------------------------ parameters */

static int32_t getParam(uint8_t p, uint8_t i) {
  if (p == P_SAMPLES) return csPads[i].samples;
  if (p == P_TIMEOUT) return csPads[i].timeoutMs;
  const struct adafruit_ptc_config &c = ptcPads[i].cfg;
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

// Writes v into pad i when it is one of the parameter's legal values. An
// illegal value writes nothing, so applying it to every pad leaves every pad
// as it was.
static bool setParam(uint8_t p, uint8_t i, int32_t v) {
  if (p == P_SAMPLES) {
    if (v < 1 || v > 255) return false;
    csPads[i].samples = (uint8_t) v;
    return true;
  }
  if (p == P_TIMEOUT) {
    if (v < 1 || v > 60000) return false;
    csPads[i].timeoutMs = (uint16_t) v;
    if (csPads[i].cs) csPads[i].cs->set_CS_Timeout_Millis(v);
    return true;
  }
  struct adafruit_ptc_config &c = ptcPads[i].cfg;
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
  const uint8_t n = padCountOf(lib);
  for (uint8_t i = 0; i < n; i++) m.add((intOSC_t) s.v[i]);
}

static void addParam(uint8_t p) {
  OSCMessage &m = bundleOUT.add(PARAM_ADDR[p]);
  const uint8_t n = setupCountOf(PARAM_LIB[p]);
  for (uint8_t i = 0; i < n; i++) m.add((intOSC_t) getParam(p, i));
}

static void addLib() {
  bundleOUT.add("/cap/lib").add(LIB_NAME[lib]);
}

static void addSend() {
  OSCMessage &m = bundleOUT.add("/cap/send");
  if (csSend >= 0) m.add((intOSC_t) csSend);
}

static void addRecv() {
  OSCMessage &m = bundleOUT.add("/cap/recv");
  for (uint8_t i = 0; i < csCount; i++) m.add((intOSC_t) csPads[i].pin);
}

static void addPins() {
  OSCMessage &m = bundleOUT.add("/cap/pins");
  const uint8_t n = padCountOf(lib);
  for (uint8_t i = 0; i < n; i++) m.add((intOSC_t) pinOf(lib, i));
}

static void addConfig() {
  addLib();
  for (uint8_t p = 0; p < P_COUNT; p++)
    if (PARAM_LIB[p] == lib) addParam(p);
  if (lib == LIB_CAPSENSE) {
    addSend();
    addRecv();
  }
  addPins();
}

static void addI2c() {
  bundleOUT.add("/i2c").add((intOSC_t) (i2cOn ? 1 : 0));
}

static void addPull() {
  OSCMessage &m = bundleOUT.add("/i2c/pull");
  if (pullSda >= 0) m.add((intOSC_t) pullSda).add((intOSC_t) pullScl);
}

static const char *busState() {            // SERCOM2's STATUS.BUSSTATE
  static const char *const NAME[4] = { "unknown", "idle", "owned", "busy" };
  return NAME[SERCOM2->I2CM.STATUS.bit.BUSSTATE & 3];
}

// Every 7-bit address from 0x08 to 0x77 that answers a one-byte read, probed
// I2C_GAP_MS apart. A read writes nothing to whatever is there, and on this
// board a Wii extension answered neither empty writes nor reads sent back to
// back, but reads spaced 1 ms apart every time. A /diag line gives the
// count, the controller's bus state and both lines' levels, to tell an empty
// bus from one that cannot run. 0x08..0x77 is the range outside the I2C
// specification's two reserved groups of eight (from memory of the
// specification, not re-read for this sketch).
static void addScan() {
  OSCMessage &m = bundleOUT.add("/i2c/scan");
  if (!i2cOn) return;
  uint8_t answered = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    if (Wire.requestFrom(a, (uint8_t) 1) == 1) {
      m.add((intOSC_t) a);
      answered++;
    }
    while (Wire.available()) Wire.read();
    delay(I2C_GAP_MS);
  }
  char d[112];
  snprintf(d, sizeof d, "scan by one-byte reads %u ms apart: %u of 112 answered; bus %s; SDA %s, SCL %s",
           I2C_GAP_MS, answered, busState(),
           digitalRead(SDA) ? "high" : "low", digitalRead(SCL) ? "high" : "low");
  bundleOUT.add("/diag").add(d);
}

// The extension's fields, from the library's getters (WiiExtension.h); for a
// type the library does not know, the six bytes of its report as the library
// read them, so it can be learned.
static void addWii() {
  OSCMessage &m = bundleOUT.add("/wii");
  if (wiiKind < 0) {
    for (uint8_t i = 0; i < 6; i++) m.add((intOSC_t) wiiPort.getControlData(i));
    return;
  }
  const WiiKind &k = WII_KINDS[wiiKind];
  for (uint8_t i = 0; i < k.count; i++) {
    const WiiField &f = k.fields[i];
    if (f.getf) m.add(f.getf());
    else m.add((intOSC_t) f.get());
  }
}

static void addWiiId() {
  OSCMessage &m = bundleOUT.add("/wii/id");
  if (!wiiPresent) return;
  m.add(wiiName());
  for (uint8_t i = 0; i < 6; i++) m.add((intOSC_t) wiiId[i]);
}

static void sayHello() {
  const uint8_t n = padCountOf(lib);
  bundleOUT.add("/enq").add("XiaoSamd21Oscuino");
  if (n) bundleOUT.add("/enq/cap").add((intOSC_t) n);
  bundleOUT.add("/enq/i2c");
  if (i2cOn && wiiPresent) bundleOUT.add("/enq/wii").add(wiiName());
  bundleOUT.add("/enq/diag");
  char diag[192];
  int k;
  if (i2cOn) {
    k = snprintf(diag, sizeof diag, "I2C at 100 kHz on D4 SDA, D5 SCL; pull-up outside SDA %s, SCL %s; ",
                 pullSda ? "yes" : "no", pullScl ? "yes" : "no");
    if (wiiPresent)
      k += snprintf(diag + k, sizeof diag - k, "Wii %s %02x %02x %02x %02x %02x %02x", wiiName(),
                    wiiId[0], wiiId[1], wiiId[2], wiiId[3], wiiId[4], wiiId[5]);
    else
      k += snprintf(diag + k, sizeof diag - k, "no Wii extension");
  } else {
    k = snprintf(diag, sizeof diag, "%s pads", LIB_NAME[lib]);
    for (uint8_t i = 0; i < n; i++)
      k += snprintf(diag + k, sizeof diag - k, " D%u", pinOf(lib, i));
    if (!n) k += snprintf(diag + k, sizeof diag - k, " none");
    if (scanLib == lib)
      k += snprintf(diag + k, sizeof diag - k, "; last scan %lu us", (unsigned long) scanMicros);
  }
  // The PTC as it is, not as this sketch believes it left it: off with no Y
  // lines while another library has the pins, which capsetupprobe.py checks.
  k += snprintf(diag + k, sizeof diag - k, "; PTC %s, Y 0x%04x",
                PTC->CTRLA.bit.ENABLE ? "on" : "off",
                (unsigned) (PTC->YENABLEH.reg << 8 | PTC->YENABLEL.reg));
  snprintf(diag + k, sizeof diag - k, "%s", bootNote);
  bundleOUT.add("/diag").add(diag);
  flush();
}

// /state goes out even when the library measuring has no pads: it is the
// stream's heartbeat, and a client waiting on /rate must still see it.
static void report(const Sample &s) {
  bundleOUT.add("/state").add(seq++).add((intOSC_t) millis());
  if (padCountOf(lib)) addCap(s);
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

// /cap/recv <i>...: every pin 0..10, none twice, none the send pin. Settings
// follow a pin that stays; a new pin starts at the defaults.
static bool setRecv(OSCMessage &m) {
  const int n = m.size();
  if (n < 1 || n > HEADER_PINS - 1) return false;
  uint8_t want[MAX_PADS];
  for (int a = 0; a < n; a++) {
    int32_t v;
    if (!argInt(m, a, v) || v < 0 || v >= HEADER_PINS || v == csSend) return false;
    for (int b = 0; b < a; b++)
      if (want[b] == v) return false;
    want[a] = (uint8_t) v;
  }
  CsPad old[MAX_PADS];
  memcpy(old, csPads, sizeof old);
  const uint8_t oldCount = csCount;
  for (int a = 0; a < n; a++) {
    csPads[a] = { want[a], CS_SAMPLES_DEFAULT, CS_TIMEOUT_DEFAULT, nullptr };
    for (uint8_t b = 0; b < oldCount; b++)
      if (old[b].pin == want[a]) {
        csPads[a].samples = old[b].samples;
        csPads[a].timeoutMs = old[b].timeoutMs;
      }
  }
  csCount = (uint8_t) n;
  return true;
}

// Every /cap... message. Bare /cap is the ask; everything below it is set-up.
static void routeCap(OSCMessage &m, int off) {
  if (m.fullMatch("/cap")) {
    if (!padCountOf(lib)) return;          // no pads: nothing to answer
    Sample s;
    scan(s);
    addCap(s);
    flush();
    return;
  }

  uint16_t echo = 0;                       // one bit per parameter to echo
  bool config = false, hello = false;
  int32_t v;

  // /cap/<p> [<v> | <v0> .. <vN-1>]
  for (uint8_t p = 0; p < P_COUNT; p++) {
    if (!m.fullMatch(PARAM_ADDR[p] + CAP_ROOT, off)) continue;
    echo |= 1u << p;
    const uint8_t n = setupCountOf(PARAM_LIB[p]);
    if (m.size() == 1 && argInt(m, 0, v)) {
      for (uint8_t i = 0; i < n; i++) setParam(p, i, v);
    } else if (n && m.size() == n) {
      for (uint8_t i = 0; i < n; i++)
        if (argInt(m, i, v)) setParam(p, i, v);
    }
  }

  // /cap/<n>/<p> <v>. No early return, so a pattern can select several pads.
  for (uint8_t i = 0; i < MAX_PADS; i++) {
    const int k = m.match(numToOSCAddress(i), off);
    if (!k) continue;
    for (uint8_t p = 0; p < P_COUNT; p++) {
      if (i >= setupCountOf(PARAM_LIB[p])) continue;
      if (!m.fullMatch(PARAM_ADDR[p] + CAP_ROOT, off + k)) continue;
      echo |= 1u << p;
      if (argInt(m, 0, v)) setParam(p, i, v);
    }
  }

  if (m.fullMatch("/lib", off)) {
    if (m.isString(0)) {
      char want[24];
      m.getString(0, want, sizeof want);
      for (uint8_t l = 0; l < LIB_COUNT; l++)
        if (!strcmp(want, LIB_NAME[l]) && l != lib) {
          if (i2cOn) lib = l;              // measured once the bus is off again
          else { selectLib(l); hello = true; }
        }
    }
    addLib();
  }

  // CapacitiveSensor's pins. While it measures, a change rebuilds its pads.
  if (m.fullMatch("/send", off)) {
    if (argInt(m, 0, v) && v >= 0 && v < HEADER_PINS && !isRecv(v) && v != csSend) {
      const bool live = lib == LIB_CAPSENSE && !i2cOn;
      if (live) { csDrop(); releasePins(); }
      csSend = (int8_t) v;
      if (live) { csBuild(); hello = true; }
    }
    addSend();
  }
  if (m.fullMatch("/recv", off)) {
    if (m.size() > 0) {
      const bool live = lib == LIB_CAPSENSE && !i2cOn;
      if (live) { csDrop(); releasePins(); }
      const bool ok = setRecv(m);
      if (live) {
        csBuild();
        hello = hello || ok;
      }
    }
    addRecv();
  }

  if (m.fullMatch("/config", off)) config = true;
  if (m.fullMatch("/defaults", off) && m.size() == 0) {
    if (lib == LIB_FREETOUCH)
      for (uint8_t i = 0; i < ptcCount; i++) setDefaults(ptcPads[i].cfg);
    if (lib == LIB_CAPSENSE) {
      csDrop();
      if (!i2cOn) releasePins();           // not the bus's pins
      csSend = -1;
      csCount = 0;
      hello = true;
    }
    config = true;
  }

  if (config) {
    addConfig();
  } else {
    for (uint8_t p = 0; p < P_COUNT; p++)
      if (echo & (1u << p)) addParam(p);
    if (m.fullMatch("/pins", off)) addPins();
  }
  if (echo || config || hello) havePrev = false;   // next report is a full one
  if (hello) roundNext = roundMicros = 0;  // a scan begun on the old pads is dropped
  flush();
  if (hello) sayHello();                   // the pad count changed with the pins
}

static void routeEnq(OSCMessage &m) {
  (void) m;
  sayHello();
  havePrev = false;                        // and a full report right behind it
}

// /i2c [<i>]: ask, or 1 to give D4 and D5 to the bus and 0 to give them back
// to the touch library. A change re-sends the hello, because the pads and the
// Wii extension come and go with it.
static void routeI2c(OSCMessage &m, int off) {
  int32_t v;
  if (m.fullMatch("/i2c")) {
    bool changed = false;
    if (argInt(m, 0, v) && (v != 0) != i2cOn) {
      if (v) i2cStart(); else i2cStop();
      changed = i2cOn == (v != 0);
    }
    addI2c();
    addPull();
    if (busLow) bundleOUT.add("/diag").add("I2C not started: SDA or SCL stays LOW with pull-ups on");
    flush();
    if (changed) sayHello();
    return;
  }
  if (m.fullMatch("/scan", off)) addScan();
  if (m.fullMatch("/pull", off)) addPull();
  flush();
}

// /wii: a fresh reading; /wii/raw: the same, with the bytes the library read
// for it beside it, from the one update(); /wii/id: what is at 0x52. Nothing
// for /wii or /wii/raw, and an empty /wii/id, while no extension is there. An
// ask looks for one itself, so it works with the stream stopped, and an
// extension that no longer answers is forgotten; either change re-sends the
// hello, as the stream's own probe does.
static void routeWii(OSCMessage &m, int off) {
  const bool raw = m.fullMatch("/raw", off), id = m.fullMatch("/id", off);
  if (!raw && !id && !m.fullMatch("/wii")) return;
  bool changed = false;
  if (i2cOn && !wiiPresent) changed = wiiProbe();
  if (id) {
    addWiiId();
  } else if (i2cOn && wiiPresent) {
    bool lost;
    if (wiiUpdate(lost)) {
      if (raw) {
        OSCMessage &r = bundleOUT.add("/wii/raw");
        for (uint8_t i = 0; i < wiiPort.getRequestSize(); i++) r.add((intOSC_t) wiiPort.getControlData(i));
      }
      addWii();
    }
    changed = changed || lost;
  }
  flush();
  if (changed) sayHello();
}

// I2C mode's stream: an empty bus is asked again every WII_PROBE_MS, a found
// extension read every /rate tick, and a change of extension re-sends the
// hello, as a pad change does. /deadband and /heartbeat apply to touch only.
static void i2cTick() {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastReport) < reportInterval) return;
  lastReport = now;
  bool have = false, changed = false;
  if (wiiPresent) {
    have = wiiUpdate(changed);
  } else if ((uint32_t)(now - lastProbe) >= WII_PROBE_MS) {
    lastProbe = now;
    if (wiiProbe()) {
      changed = true;
      have = wiiRead();
    }
  }
  if (changed) sayHello();
  bundleOUT.add("/state").add(seq++).add((intOSC_t) millis());
  if (have) addWii();
  flush();
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
  roundNext = roundMicros = 0;             // and a fresh scan, not one left half done
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
//
// The template's pollOSC() returns whenever available() says 0, but
// available() also says 0 just after it has swallowed a frame's opening END
// with the frame still buffered behind it, and the closing END only reads as
// the end on the next call. In the template's quick loop() that costs nothing.
// Here a pass can be a 0.70 s CapacitiveSensor pad, and every message waited
// two extra pads: echoes took 1.69..1.96 s against a 2.81 s scan of four
// (capsetupprobe.py, 2026-10-29). So this keeps going while the port still
// holds bytes, and asks endofPacket() once more before giving up: the
// template's calls in the template's order (endofPacket() before available()),
// without a loop() pass between them.
static bool pollOSC() {
  for (;;) {
    if (SLIPSerial.endofPacket()) return true;
    int avail = SLIPSerial.available();
    if (avail <= 0) {
      if (SLIP_PORT.available() > 0) continue;   // an END was swallowed; more behind it
      return SLIPSerial.endofPacket();           // the closing END, if that was the last byte
    }
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t) c);   // read() returns -1 on a SLIP error
    }
  }
}

// Deadband 0 reports every tick; otherwise a move of at least N counts on any
// pad is needed.
static bool changed(const Sample &a, const Sample &b) {
  if (deadband == 0) return true;
  const uint8_t n = padCountOf(lib);
  for (uint8_t i = 0; i < n; i++) {
    int32_t d = a.v[i] - b.v[i];
    if (d < 0) d = -d;
    if (d >= (int32_t) deadband) return true;
  }
  return false;
}

/* ---------------------------------------------------------------------- */

void setup() {
  SLIPSerial.begin(BAUD);
  findPtcPads();                           // FreeTouch measures first; this is its bring-up
  findFtPins();
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
      bundleIN.route("/i2c",          routeI2c);
      bundleIN.route("/wii",          routeWii);
      bundleIN.dispatch("/rate",      routeRate);
      bundleIN.dispatch("/heartbeat", routeHeartbeat);
      bundleIN.dispatch("/deadband",  routeDeadband);
    }
    bundleIN.empty();
  }

  if (reportInterval == 0) return;
  if (i2cOn) {
    i2cTick();
    return;
  }
  const uint32_t now = millis();
  if (!roundNext) {
    if ((uint32_t)(now - lastReport) < reportInterval) return;
    roundStart = now;
  }

  // One pad per pass, so OSC is read between pads. CapacitiveSensor on a pad
  // with no resistor waits out its timeout, 0.70 s at the default 2000 on this
  // board, and a scan of several such pads read in one go held off every
  // message, a /cap/lib included, for seconds.
  const uint8_t n = padCountOf(lib);
  if (roundNext < n) {
    const uint32_t t0 = micros();
    roundS.v[roundNext] = readOne(roundNext);
    roundMicros += micros() - t0;
    if (++roundNext < n) return;
  }
  scanMicros = roundMicros;
  scanLib = lib;
  roundNext = roundMicros = 0;

  const bool due = heartbeatMs && (uint32_t)(roundStart - lastReport) >= heartbeatMs;
  if (!havePrev || due || changed(roundS, prev)) {
    lastReport = roundStart;
    prev = roundS;
    havePrev = true;
    report(roundS);
  }
}

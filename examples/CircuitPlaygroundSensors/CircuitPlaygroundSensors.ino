/*
 * CircuitPlaygroundSensors — the whole Circuit Playground Express in one OSC bundle
 * -----------------------------------------------------------------------------
 * Board : Adafruit Circuit Playground Express (SAMD21G18A)
 * FQBN  : adafruit:samd:adafruit_circuitplayground_m0   (arduino:samd: also works)
 * Page  : CircuitPlaygroundSensors.html  (Chrome/Edge, Web Serial)
 *
 * PlaygroundOscuino, sitting next to this one, is generated from the shared
 * template and speaks the pin-oriented Oscuino address space: /d/<pin>, /a/<pin>.
 * That reaches the board's pads but knows nothing about what is soldered to
 * them. This sketch is the other half — the ten NeoPixels and every built-in
 * sensor, by capability, in the vocabulary of ADDRESSES.md. The board is named
 * nowhere in the address space: /btn, /light, /cap and /rgb mean the same thing
 * here as on every other Oscuino board.
 *
 * Everything the board knows about itself goes out as ONE BUNDLE per report,
 * sampled in a single pass so the values belong to the same instant. Sent as
 * separate packets, the accelerometer and the touch pads can be milliseconds
 * apart and a receiver cannot tell which readings were simultaneous. Here they
 * are simultaneous by construction, and the sequence counter in /state makes
 * drops visible. One message per capability, in this order:
 *
 *   STREAM — one bundle per report:
 *     /state  ii       seq, millis. seq is a free-running counter, wraps at
 *                      2^31; gaps mean dropped packets
 *     /btn    iii      buttonA, buttonB, slide — 0/1, 1 = pressed (left D4,
 *                      right D5); slide: 1 = the side that pulls D7 low (note below)
 *     /light  i        0..1023 ambient light phototransistor (A8)
 *     /temp   f        degrees Celsius from the thermistor (A9); NaN if out of range
 *     /mic    ii       rms, peak on the contract's 0..32767 scale. This sketch
 *                      measures one rough level (0..1023, times 32 = 0..32736)
 *                      and sends it in both slots. Only with CPX_MIC (see below)
 *     /imu    fff      accelX, accelY, accelZ in g, roughly -2..2, from the
 *                      LIS3DH on the internal I2C bus (about +1 on Z with the
 *                      board face up). Only when the LIS3DH answered its probe
 *     /cap    iiiiiii  raw capacitance counts, pads A1..A7
 *
 * A capability that is not there is neither sent nor announced: an Express
 * built without CPX_MIC carries no /mic, and a board whose accelerometer never
 * answered carries no /imu. The enq bundle says which it is, so a page reads
 * presence from /enq rather than from a sentinel value.
 *
 * Untouched pads read 196..275 counts on the board this was written against,
 * and a finger takes them to 686..1014 — a factor of three to five, not a fixed
 * offset, and the resting level depends on what is clipped to the pad. So the
 * page learns each pad's baseline rather than thresholding. A0 is the speaker's
 * DAC output, not a touch pad, which is why the pads start at A1.
 *
 * Reporting is change-driven, not timer-driven. A bundle goes out when
 * something actually moved, and otherwise once every heartbeat interval so a
 * page that connects mid-session still gets a full picture and so silence is
 * distinguishable from a dead link. Analog channels are compared with a
 * deadband because the light sensor, microphone and accelerometer never read
 * the same value twice — without one, "on change" degenerates into "always".
 * Buttons and the slide switch bypass the deadband: a press is never noise.
 *
 * Change detection runs on the raw counts, before conversion, so the deadband
 * stays in one unit across every channel. Only the outbound path converts to
 * degrees, g and the microphone's full-scale units.
 *
 * Inbound, so a page can drive the board (ADDRESSES.md is the contract):
 *
 *    /enq                       answer with the enq bundle below
 *    /rgb/<n>    <r> <g> <b>      one pixel, 0..9, colours 0..255
 *    /rgb/pixels <30 ints>        all ten at once, r,g,b per pixel
 *    /rgb        <r> <g> <b>      every pixel the same colour
 *    /rgb/bright <0..255>         NeoPixel brightness
 *    /s/l        <0/1>            the red LED beside the USB socket (D13)
 *    /buzz       <hz> [<ms>]      speaker; 0 or no argument stops it
 *    /rate       <ms>             streaming period, 0..1000; 0 STOPS the stream,
 *                                 as the contract says. This sketch reports on
 *                                 change, so a non-zero /rate is the floor on
 *                                 the gap between reports rather than a fixed
 *                                 period: 1 means "as fast as change allows",
 *                                 200 means "at most five reports a second".
 *                                 Asks, /enq and every write still work while
 *                                 the stream is stopped
 *    /heartbeat  <ms>             report at least this often, 0 disables
 *    /deadband   <counts>         analog change needed to trigger, 0..64
 *    /btn /light /temp /mic /imu /cap
 *                                 ask: answered with that one reading, fresh.
 *                                 An absent capability answers nothing
 *
 * The enq bundle is /enq "CircuitPlaygroundSensors" followed by one /enq
 * line per capability the board can prove it has:
 *
 *    /enq/rgb 10   /enq/btn 3   /enq/light   /enq/temp   /enq/cap 7   /enq/buzz
 *    /enq/diag     the board talks about itself; the line it sends is below
 *    /enq/imu 3    only when the LIS3DH answered its WHO_AM_I probe
 *    /enq/mic      only when CPX_MIC is compiled in and the PDM peripheral started
 *
 * and then the one thing /enq/diag announces:
 *
 *    /diag <s>     free text, never parsed: which I2C address the accelerometer
 *                  answered at, or that neither did
 *
 * Nothing is sent that was not announced, /diag included: a page reads the
 * /enq list and knows what to expect.
 *
 * /enq/btn counts the slide switch as a third button because the contract has
 * no switch capability; its slot is the third /btn argument.
 *
 * LIBRARIES. Adafruit NeoPixel, Adafruit FreeTouch and Adafruit LIS3DH, all
 * from the Library Manager. An earlier revision read the accelerometer
 * directly over Wire1 "to keep the install list to two" — but two sibling
 * examples in this repo (PyBadge, HalloWing) already depend on Adafruit
 * LIS3DH, so for anyone using this example set the saving was imaginary, and
 * the hand-rolled register driver duplicated a maintained one. (The honest
 * cost of the library: it pulls in Adafruit Unified Sensor and BusIO too.)
 * The thermistor and cap-touch maths remain hand-rolled deliberately: the
 * only library covering those is the full Adafruit Circuit Playground
 * package, which drags in six further dependencies for two formulas.
 *
 * THE MICROPHONE IS OPT-IN. The Express carries a PDM MEMS microphone, not the
 * Classic's analog one, so it cannot be reached with analogRead and the core's
 * I2S library is compiled out for this variant (I2S_INTERFACES_COUNT is 0).
 * Install "Adafruit Zero PDM Library" and uncomment CPX_MIC below to switch it
 * on; without it the sketch builds unchanged, announces no /enq/mic and streams
 * no /mic.
 *
 * VERIFIED. Compiles for adafruit:samd:adafruit_circuitplayground_m0 and
 * arduino:samd:adafruit_circuitplayground_m0, with CPX_MIC both off and on, and
 * RUN ON HARDWARE on 2026-08-04:
 *
 *   - the accelerometer answered at 0x19, and reads 1.01..1.02 g total with the
 *     board at rest in any attitude, which is what says the register setup, the
 *     burst read, the sign extension and the 1 mg scaling are all right
 *   - the thermistor read 23.2..24.4 C in a room at about that, so the constants
 *     below are at least not wrong by a scale factor
 *   - both buttons read 0 at rest and 1 when pressed, so the pull-down and the
 *     active-high sense in setup() are right, and the slide switch reached both
 *     states, so its pull-up is right too
 *   - all seven pads responded to a finger, 196..275 at rest to 686..1014 touched
 *   - the light sensor sees its own ring: 106 -> 109 with the pixels driven white
 *     at brightness 40, and 116 during a per-pixel sweep at 120
 *   - 2186 samples over 45 seconds with no dropped sequence numbers
 *
 * Addresses renamed onto ADDRESSES.md on 2026-09-03 (/cpx -> /state + /btn
 * /light /temp /mic /imu /cap, one message per capability instead of one
 * seventeen-argument blob; /pix -> /rgb/<n>; /pixels -> /rgb/pixels;
 * /bright -> /rgb/bright; /led -> /s/l; bare /tone -> /buzz; the booleans and
 * counts that rode in /enq -> /enq/... lines; /rate 0 meant "no floor" and
 * now stops the stream, as the contract requires); that build is
 * compile-checked and has not been re-run on the board.
 *
 * STILL UNVERIFIED, and deliberately not claimed above:
 *
 *   - the microphone, which needs CPX_MIC and the library below. What was
 *     observed is only that, compiled out, it stays out of the hello and the
 *     stream.
 *   - WHICH SIDE of the slide switch reports 1. Both states were seen, but
 *     nothing here ties either to the silkscreen, so the table above says only
 *     that 1 is the side pulling the pin low. Flip yours and see.
 *   - the ORIENTATION of the thermistor divider. A plausible room temperature
 *     does not prove it: a divider wired the other way round still yields a
 *     number, and near 25 C — the nominal point — a sign error is small. Ten
 *     NeoPixels at brightness 150 for a minute moved the reading by +0.2 C,
 *     which is the same as the ambient drift measured either side of it, so
 *     that experiment settled nothing. To settle it: hold a finger on the
 *     sensor and confirm /temp RISES.
 *
 * The constants below are Adafruit's published values for this board, not
 * anything measured here.
 *
 * Written by Adrian Freed.
 */

// Uncomment after installing "Adafruit Zero PDM Library" to enable the microphone.
// A literal #include inside a taken branch is what makes the library resolve;
// __has_include() cannot do this job, because the header is only on the include
// path once something has already asked for the library by name.
//#define CPX_MIC 1

#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_FreeTouch.h>
#include <OSCBundle.h>
#include <SLIPEncodedSerial.h>
#ifdef CPX_MIC
#include <Adafruit_ZeroPDM.h>
#endif

// OSCBoards.h defines BOARD_HAS_USB_SERIAL and thisBoardsSerialUSB for boards
// whose Serial is a USB CDC object rather than a UART. The Express always is.
#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

/* ------------------------------------------------------------------ pins */
// adafruit:samd names these; arduino:samd ships the same variant table without
// the friendly macros, so fall back to the literals and the sketch builds on
// both. The numbers come from the variant's own g_APinDescription table.
#ifndef PIN_NEOPIXEL
#define PIN_NEOPIXEL 8
#endif
#ifndef NEOPIXEL_NUM
#define NEOPIXEL_NUM 10
#endif
#ifndef PIN_BUTTON1
#define PIN_BUTTON1 4               // left button, drives the pin HIGH when pressed
#endif
#ifndef PIN_BUTTON2
#define PIN_BUTTON2 5               // right button
#endif
#define PIN_SLIDE      7            // slide switch, shorts to ground on one side
#define PIN_LIGHT      A8           // phototransistor
#define PIN_THERMISTOR A9
#define PIN_SPEAKER    A0           // the DAC pad; also the reason A0 is not a touch pad
#define PIN_SPEAKER_EN 11           // class-D amplifier shutdown, HIGH = enabled

static const uint8_t  TOUCH_COUNT = 7;
static const uint8_t  BUTTON_COUNT = 3;  // A, B, and the slide switch (see the header)
static const uint8_t  PIXEL_COUNT = NEOPIXEL_NUM;
static const long     BAUD = 115200;     // ignored by native USB, kept for clarity
static const int32_t  MIC_SCALE = 32;    // 0..1023 level -> the contract's 0..32767

/* --------------------------------------------------------------- devices */
static Adafruit_NeoPixel pixels(PIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// One PTC channel per alligator pad. A1..A7; A0 is the speaker.
static Adafruit_FreeTouch touch[TOUCH_COUNT] = {
  Adafruit_FreeTouch(A1, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A4, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A5, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A6, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
  Adafruit_FreeTouch(A7, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),
};

/* ----------------------------------------------------------------- state
 * This block sits above every function on purpose. The Arduino builder inserts
 * the prototypes it generates for the sketch immediately before the first
 * function definition in the file, so any type a function signature mentions
 * has to be declared above that point. Move `struct Sample` below the first
 * function and the build fails with "'Sample' was not declared in this scope"
 * pointing at a definition that plainly is below the struct.
 */
// bundleIN must outlive a single pass through loop(). pollOSC() returns as
// soon as the serial buffer runs dry, which for anything but a very short frame
// happens part way through one; a bundle declared inside loop() would lose the
// bytes it had already accumulated every time that happened. It accepts a bare
// message as well as a bundle, so a page may send either.
static OSCBundle bundleIN;
// Every hello, reply and report is assembled here and sent by flush(). The
// messages are heap-allocated per bundle and freed on empty(), which is the
// library's own pattern and what every sibling Oscuino sketch does.
static OSCBundle bundleOUT;

static int32_t  seq = 0;
static uint16_t reportInterval = 20;    // streaming period, ms; 0 = stream off
static uint16_t heartbeatMs = 2000;     // report even when nothing moves
static uint16_t deadband = 4;           // analog counts needed to count as change
static uint32_t lastReport = 0;
static uint32_t toneEndsAt = 0;         // 0 = not playing; else millis() deadline

// One sample of everything, in raw units.
struct Sample {
  int32_t buttonA, buttonB, slide;
  int32_t light, tempRaw, sound;
  int32_t accX, accY, accZ;             // 12-bit counts, 1 mg each
  int32_t touch[TOUCH_COUNT];
};
static Sample prev;
static bool havePrev = false;

/* ------------------------------------------------------- LIS3DH over Wire1
 * The accelerometer hangs off the SAMD21's second I2C bus, which is private to
 * it — the alligator pads' I2C is the separate Wire, so nothing a user clips on
 * can disturb this. Adafruit's own boards have shipped at both 0x19 and 0x18,
 * so probe both; the driver's begin() returns false on a WHO_AM_I mismatch,
 * which reproduces the old hand-rolled probe exactly. Settings match too:
 * begin() writes CTRL4 = 0x88 (block data update + high resolution, +-2 g)
 * and the 100 Hz data rate gives the same CTRL1 = 0x57.
 */
#include <Adafruit_LIS3DH.h>

static Adafruit_LIS3DH lis(&Wire1);
static bool accelOK = false;
static uint8_t accelAddr = 0;   // the hello's /diag line reports which address answered

static bool accelBegin() {
  Wire1.begin();
  if      (lis.begin(0x19)) { accelOK = true; accelAddr = 0x19; }
  else if (lis.begin(0x18)) { accelOK = true; accelAddr = 0x18; }
  if (accelOK) {
    lis.setDataRate(LIS3DH_DATARATE_100_HZ);
    lis.setRange(LIS3DH_RANGE_2_G);
  }
  return accelOK;
}

// Raw 12-bit counts, 1 mg each — lis.x/y/z are left-justified 16-bit, so >>4
// is bit-identical to the old manual conversion. Zero on every axis when no
// device answered; the stream then simply carries no /imu, so nothing
// downstream sees the zeros.
static void accelRead(int16_t &x, int16_t &y, int16_t &z) {
  x = y = z = 0;
  if (!accelOK) return;
  lis.read();
  x = lis.x >> 4;
  y = lis.y >> 4;
  z = lis.z >> 4;
}

/* ------------------------------------------------------------ microphone */
#ifdef CPX_MIC
// The PDM bitstream is one bit per clock: the density of 1s tracks the
// waveform, 50% being silence. A proper decimation filter would recover the
// audio; this only needs a level, so it counts set bits over a window and
// reports how far the density strays from half. Rough, cheap, and honest about
// being a level rather than a calibrated sound pressure.
//
// Two things about the driver are worth knowing before switching this on.
// Adafruit_ZeroPDM.h declares `bool read(uint32_t *buffer, int bufsiz)`, but
// the library never defines it — begin(), end(), configure() and the
// word-at-a-time read() are the only four methods that exist, so calling the
// buffered form compiles and then fails at link with an undefined reference.
// And read() spins on the I2S ready flag with no timeout, so a misconfigured
// peripheral hangs the sketch rather than returning an error. That is the other
// half of why the microphone is opt-in.
static const int  MIC_CLK = 34, MIC_DATA = 35;   // no pin macros exist for these
static const int  MIC_RATE_HZ = 22000;           // nominal audio rate
static const int  MIC_DECIMATION = 64;           // PDM bits per audio sample
static const int  MIC_WORDS = 32;                // read() returns 32 bits a call,
static const int  MIC_BITS = MIC_WORDS * 32;     // so this is 1024 bits a pass
static Adafruit_ZeroPDM pdm(MIC_CLK, MIC_DATA);
static bool micReady = false;

static int micLevel() {
  if (!micReady) return -1;
  uint32_t ones = 0;
  for (int i = 0; i < MIC_WORDS; i++) ones += __builtin_popcount(pdm.read());
  int32_t off = (int32_t) ones - MIC_BITS / 2;        // 0 at silence, +-512 full
  if (off < 0) off = -off;
  return (int) ((off * 1023) / (MIC_BITS / 2));       // 0 .. 1023
}
#else
static const bool micReady = false;                   // no PDM driver compiled in
static int micLevel() { return -1; }
#endif

/* --------------------------------------------------------------- sensors */
// A 10k NTC thermistor, nominal 10k at 25 C, beta 3380, in a divider with a 10k
// series resistor. The thermistor is the HIGH side: 3V3 -> thermistor -> A9 ->
// 10k -> GND, so warming it lowers its resistance and RAISES the ADC reading,
// and the resistance is Rs * (1023/raw - 1).
//
// That orientation is not guesswork and it is not obvious — getting it upside
// down still yields a number, and near 25 C it is a plausible-looking one. The
// first version of this divided where it should have multiplied and read 22.5 C
// in a room at about that, which is exactly why "the number looks right" proves
// nothing. Two things settled it: Adafruit_CircuitPlayground's own
// temperature() computes ((1023 * SERIESRESISTOR) / raw) - SERIESRESISTOR, and
// on hardware a fingertip on the sensor moved the reading 2.3 C the WRONG way
// until this was corrected.
static const float SERIES_OHMS   = 10000.0f;
static const float NOMINAL_OHMS  = 10000.0f;
static const float NOMINAL_C     = 25.0f;
static const float BETA          = 3380.0f;
static const int   ADC_FULLSCALE = 1023;      // the SAMD core's default 10-bit read

static float thermistorC(int raw) {
  // A rail-to-rail reading means an open or shorted divider, not a temperature.
  if (raw <= 0 || raw >= ADC_FULLSCALE) return NAN;
  float r = SERIES_OHMS * ((float) ADC_FULLSCALE / (float) raw - 1.0f);
  float inv = logf(r / NOMINAL_OHMS) / BETA + 1.0f / (NOMINAL_C + 273.15f);
  return 1.0f / inv - 273.15f;
}

/* --------------------------------------------------------------- outbound */

// Everything assembled in bundleOUT goes out as one SLIP frame. An empty
// bundle is not sent: an ask for a capability the board lacks answers nothing.
static void flush() {
  if (bundleOUT.size() > 0) {
    SLIPSerial.beginPacket();
    bundleOUT.send(SLIPSerial);
    SLIPSerial.endPacket();
  }
  bundleOUT.empty();
}

static void sample(Sample &s) {
  // The buttons pull their pin up when pressed and the slide switch pulls its
  // pin down, so one is read active-HIGH and the other inverted. Which way the
  // hardware happens to work is not something a receiver should have to know:
  // both go on the wire as 1 = pressed, 1 = switched to the + side.
  s.buttonA = digitalRead(PIN_BUTTON1) == HIGH ? 1 : 0;
  s.buttonB = digitalRead(PIN_BUTTON2) == HIGH ? 1 : 0;
  s.slide   = digitalRead(PIN_SLIDE)   == LOW  ? 1 : 0;

  s.light   = analogRead(PIN_LIGHT);
  s.tempRaw = analogRead(PIN_THERMISTOR);
  s.sound   = micLevel();

  int16_t x, y, z;
  accelRead(x, y, z);
  s.accX = x; s.accY = y; s.accZ = z;

  for (uint8_t i = 0; i < TOUCH_COUNT; i++) s.touch[i] = touch[i].measure();
}

// Which readings to put in a bundle: the whole set for a report, one for an ask.
enum { R_BTN = 1, R_LIGHT = 2, R_TEMP = 4, R_MIC = 8, R_IMU = 16, R_CAP = 32, R_ALL = 63 };

static bool haveAccel = false;

// One message per capability, converted here and nowhere else. The address
// list and the type tags are the ones documented in the header's STREAM table;
// extras/webserial/test/test-cpx-contract.mjs holds the two to each other.
static void addReadings(OSCBundle &b, const Sample &s, uint8_t which) {
  if (which & R_BTN)   b.add("/btn").add(s.buttonA).add(s.buttonB).add(s.slide);
  if (which & R_LIGHT) b.add("/light").add(s.light);
  if (which & R_TEMP)  b.add("/temp").add(thermistorC(s.tempRaw));
  if ((which & R_MIC) && micReady) {
    int32_t level = s.sound * MIC_SCALE;
    b.add("/mic").add(level).add(level);
  }
  if ((which & R_IMU) && haveAccel)
    b.add("/imu").add(s.accX * 0.001f).add(s.accY * 0.001f).add(s.accZ * 0.001f);
  if (which & R_CAP) {
    OSCMessage &m = b.add("/cap");
    for (uint8_t i = 0; i < TOUCH_COUNT; i++) m.add(s.touch[i]);
  }
}

// A report: /state first, then everything the board has.
static void send(const Sample &s) {
  bundleOUT.add("/state").add(seq++).add((int32_t) millis());
  addReadings(bundleOUT, s, R_ALL);
  flush();
}

// An ask: a fresh sample, and only the reading that was asked for.
static void answer(uint8_t which) {
  Sample s;
  sample(s);
  addReadings(bundleOUT, s, which);
  flush();
}

/* ---------------------------------------------------------------- inbound */

static int colourArg(OSCMessage &m, int i) {
  return (int) constrain(m.getInt(i), 0, 255);
}

/* Adafruit_NeoPixel::setBrightness() rescales the pixel buffer in place, and
 * the rescale is lossy in one direction fatally: at brightness 0 it multiplies
 * the whole buffer by zero, so the colours are gone and raising the brightness
 * again cannot bring them back. The page's slider reaches 0, which puts a dead
 * ring one drag away — pixels black while the page still shows their colours.
 *
 * So the commanded colours are kept here, and replayed after any brightness
 * change. This is also what makes /rgb/bright cheap to sweep: nothing else has
 * to remember what the ring was showing.
 */
static uint8_t wanted[PIXEL_COUNT * 3];

static void setPixel(int i, uint8_t r, uint8_t g, uint8_t b) {
  wanted[i * 3] = r; wanted[i * 3 + 1] = g; wanted[i * 3 + 2] = b;
  pixels.setPixelColor(i, pixels.Color(r, g, b));
}

static void replayPixels() {
  for (uint8_t i = 0; i < PIXEL_COUNT; i++)
    pixels.setPixelColor(i, pixels.Color(wanted[i * 3], wanted[i * 3 + 1], wanted[i * 3 + 2]));
}

// /rgb/<n> <r> <g> <b>: one pixel.
static void routePixel(OSCMessage &m, int i) {
  if (m.size() < 3 || i < 0 || i >= PIXEL_COUNT) return;
  setPixel(i, colourArg(m, 0), colourArg(m, 1), colourArg(m, 2));
  pixels.show();
}

// /rgb/pixels: all ten in one message, r,g,b per pixel, in pixel order. Short
// messages set as many pixels as they carry, so three arguments light only
// pixel 0.
static void routePixels(OSCMessage &m) {
  int n = m.size() / 3;
  if (n > PIXEL_COUNT) n = PIXEL_COUNT;
  for (int i = 0; i < n; i++)
    setPixel(i, colourArg(m, i * 3), colourArg(m, i * 3 + 1), colourArg(m, i * 3 + 2));
  pixels.show();
}

// /rgb <r> <g> <b>: every pixel the same colour.
static void routeRgbAll(OSCMessage &m) {
  if (m.size() < 3) return;
  uint8_t r = colourArg(m, 0), g = colourArg(m, 1), b = colourArg(m, 2);
  for (uint8_t i = 0; i < PIXEL_COUNT; i++) setPixel(i, r, g, b);
  pixels.show();
}

// /rgb/bright <0..255>
static void routeBright(OSCMessage &m) {
  if (m.size() < 1) return;
  pixels.setBrightness((uint8_t) constrain(m.getInt(0), 0, 255));
  replayPixels();          // see the note above setPixel(): without this, 0 is a cliff
  pixels.show();
}

// The four /rgb addresses share a root, so one route takes the tree and looks
// at what follows the root: nothing (/rgb), /rgb/pixels, /rgb/bright, or
// /rgb/<n> for one pixel.
static void routeRgb(OSCMessage &m, int addrOffset) {
  const char *rest = m.getAddress() + addrOffset;
  if (*rest == 0)                              routeRgbAll(m);
  else if (m.fullMatch("/pixels", addrOffset)) routePixels(m);
  else if (m.fullMatch("/bright", addrOffset)) routeBright(m);
  else if (rest[0] == '/' && rest[1] >= '0' && rest[1] <= '9')
    routePixel(m, (int) strtol(rest + 1, NULL, 10));
}

// /s/l <0/1>: the plain red LED. The only /s address this sketch answers.
static void routeSystem(OSCMessage &m, int addrOffset) {
  if (m.fullMatch("/l", addrOffset) && m.size() >= 1)
    digitalWrite(LED_BUILTIN, m.getInt(0) ? HIGH : LOW);
}

// The amplifier is left shut down while nothing is playing: enabled all the
// time it idles with an audible hiss, and the DAC pad is also a touch pad's
// neighbour. loop() switches it back off when a timed tone runs out.
static void speaker(bool on) { digitalWrite(PIN_SPEAKER_EN, on ? HIGH : LOW); }

// /buzz <hz> [<ms>]; 0 or no argument stops it.
static void routeBuzz(OSCMessage &m) {
  if (m.size() < 1 || m.getInt(0) <= 0) {
    noTone(PIN_SPEAKER);
    speaker(false);
    toneEndsAt = 0;
    return;
  }
  unsigned int freq = (unsigned int) m.getInt(0);
  speaker(true);
  if (m.size() >= 2) {
    unsigned long ms = (unsigned long) m.getInt(1);
    tone(PIN_SPEAKER, freq, ms);
    toneEndsAt = millis() + ms + 20;     // a little past the end, then mute the amp
  } else {
    tone(PIN_SPEAKER, freq);
    toneEndsAt = 0;                      // plays until /buzz 0
  }
}

// Sent once at startup and again whenever a host asks. Asking matters more than
// the startup one does: the board says hello 300 ms after it enumerates, which
// is long before a person has clicked Connect, so a page that only listened
// would never see it — and this is where it learns whether the accelerometer
// answered at all. Measured on hardware: re-flashing and reopening the port as
// fast as the host allows still misses the startup hello every time.
//
// One /enq line per capability the board can prove it has. The accelerometer
// and the microphone are announced only when their drivers came up, so a page
// reads presence from the list rather than from a sentinel in the stream.
static void sayHello() {
  bundleOUT.add("/enq").add("CircuitPlaygroundSensors");
  bundleOUT.add("/enq/rgb").add((int32_t) PIXEL_COUNT);
  bundleOUT.add("/enq/btn").add((int32_t) BUTTON_COUNT);
  bundleOUT.add("/enq/light");
  bundleOUT.add("/enq/temp");
  if (micReady)  bundleOUT.add("/enq/mic");
  if (haveAccel) bundleOUT.add("/enq/imu").add((int32_t) 3);
  bundleOUT.add("/enq/cap").add((int32_t) TOUCH_COUNT);
  bundleOUT.add("/enq/buzz");
  bundleOUT.add("/enq/diag");    // the free text below is announced like anything else
  char diag[32];
  if (haveAccel) snprintf(diag, sizeof diag, "LIS3DH at 0x%02x", accelAddr);
  else           snprintf(diag, sizeof diag, "no LIS3DH at 0x19 or 0x18");
  bundleOUT.add("/diag").add(diag);
  flush();
}

static void routeEnq(OSCMessage &m) {
  (void) m;
  sayHello();
  havePrev = false;          // and a full report right behind it
}

// /rate <ms>: 0 stops the stream, per ADDRESSES.md. A non-zero value is a floor
// on the gap between reports rather than a fixed period, because this sketch is
// change-driven; 1 is therefore "as fast as change allows", which is what the
// old /rate 0 used to mean here. Asks and writes are unaffected either way.
static void routeRate(OSCMessage &m) {
  if (m.size() < 1) return;
  reportInterval = (uint16_t) constrain(m.getInt(0), 0, 1000);
  havePrev = false;             // restarting the stream begins with a full report
}

static void routeHeartbeat(OSCMessage &m) {
  if (m.size() < 1) return;
  heartbeatMs = (uint16_t) constrain(m.getInt(0), 0, 60000);
}

static void routeDeadband(OSCMessage &m) {
  if (m.size() < 1) return;
  deadband = (uint16_t) constrain(m.getInt(0), 0, 64);
  havePrev = false;                     // force one full report at the new setting
}

// The asks: each answers on its own address with a fresh reading.
static void askBtn(OSCMessage &m)   { (void) m; answer(R_BTN); }
static void askLight(OSCMessage &m) { (void) m; answer(R_LIGHT); }
static void askTemp(OSCMessage &m)  { (void) m; answer(R_TEMP); }
static void askMic(OSCMessage &m)   { (void) m; answer(R_MIC); }
static void askImu(OSCMessage &m)   { (void) m; answer(R_IMU); }
static void askCap(OSCMessage &m)   { (void) m; answer(R_CAP); }

// Returns true once a whole packet has been accumulated in bundleIN.
static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int avail = SLIPSerial.available();
    if (avail <= 0) return false;            // nothing buffered; try next loop()
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t) c);   // read() returns -1 on a SLIP error
    }
  }
  return true;
}

/* ------------------------------------------------------- change detection */

static bool moved(int32_t a, int32_t b) {
  int32_t d = a > b ? a - b : b - a;
  return d > (int32_t) deadband;
}

// Any button or switch transition, or any analog channel that moved further
// than the deadband. The touch pads drift with humidity and with whatever is
// clipped to them, which is exactly what the deadband is for.
static bool changed(const Sample &a, const Sample &b) {
  if (a.buttonA != b.buttonA || a.buttonB != b.buttonB || a.slide != b.slide)
    return true;
  if (moved(a.light, b.light) || moved(a.tempRaw, b.tempRaw) ||
      moved(a.sound, b.sound) || moved(a.accX, b.accX) ||
      moved(a.accY, b.accY)   || moved(a.accZ, b.accZ)) return true;
  for (uint8_t i = 0; i < TOUCH_COUNT; i++)
    if (moved(a.touch[i], b.touch[i])) return true;
  return false;
}

/* ---------------------------------------------------------------------- */

void setup() {
  SLIPSerial.begin(BAUD);

  pinMode(PIN_BUTTON1, INPUT_PULLDOWN);
  pinMode(PIN_BUTTON2, INPUT_PULLDOWN);
  pinMode(PIN_SLIDE,   INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(PIN_SPEAKER_EN, OUTPUT);
  speaker(false);

  pixels.begin();
  pixels.setBrightness(40);          // ten pixels at full tilt is both blinding
  pixels.clear();                    // and more current than USB likes
  pixels.show();

  for (uint8_t i = 0; i < TOUCH_COUNT; i++) touch[i].begin();

  haveAccel = accelBegin();
#ifdef CPX_MIC
  micReady = pdm.begin();
  // configure() takes the I2S FRAME rate, not the audio sample rate, despite
  // what its doc comment says: it divides F_CPU by (arg * 16) to get the bit
  // clock. Passing 22000 directly gives a 353 kHz PDM clock, well under the
  // megahertz a MEMS PDM microphone needs. Both of the library's own examples
  // pass rate * decimation / 16, which is what this is.
  if (micReady) micReady = pdm.configure(MIC_RATE_HZ * MIC_DECIMATION / 16, true);
#endif

  delay(300);                        // let the host finish enumerating
  sayHello();
}

void loop() {
  if (pollOSC()) {
    if (!bundleIN.hasError()) {
      bundleIN.dispatch("/enq",     routeEnq);
      bundleIN.route("/rgb",          routeRgb);      // /rgb, /rgb/<n>, /rgb/pixels, /rgb/bright
      bundleIN.route("/s",            routeSystem);   // /s/l
      bundleIN.dispatch("/buzz",      routeBuzz);
      bundleIN.dispatch("/rate",      routeRate);
      bundleIN.dispatch("/heartbeat", routeHeartbeat);
      bundleIN.dispatch("/deadband",  routeDeadband);
      bundleIN.dispatch("/btn",       askBtn);
      bundleIN.dispatch("/light",     askLight);
      bundleIN.dispatch("/temp",      askTemp);
      bundleIN.dispatch("/mic",       askMic);
      bundleIN.dispatch("/imu",       askImu);
      bundleIN.dispatch("/cap",       askCap);
    }
    bundleIN.empty();
  }

  uint32_t now = millis();

  // tone(pin, freq, ms) stops the sound itself; this only mutes the amplifier
  // afterwards so it is not left hissing between beeps.
  if (toneEndsAt && (int32_t)(now - toneEndsAt) >= 0) {
    speaker(false);
    toneEndsAt = 0;
  }

  // /rate 0 stops the stream, as the contract says; asks, writes and /enq are
  // still answered above. Otherwise reportInterval is a floor rather than a
  // period, because reporting here is change-driven: it caps how fast change
  // can push packets out, so a noisy microphone cannot saturate the link.
  if (reportInterval == 0) return;
  if ((uint32_t)(now - lastReport) < reportInterval) return;

  Sample s;
  sample(s);

  bool due = heartbeatMs && (uint32_t)(now - lastReport) >= heartbeatMs;
  if (!havePrev || due || changed(s, prev)) {
    lastReport = now;
    prev = s;
    havePrev = true;
    send(s);
  }
}

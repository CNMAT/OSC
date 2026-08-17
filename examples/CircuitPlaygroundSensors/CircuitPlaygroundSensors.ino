/*
 * CircuitPlaygroundSensors — the whole Circuit Playground Express in one OSC message
 * -----------------------------------------------------------------------------
 * Board : Adafruit Circuit Playground Express (SAMD21G18A)
 * FQBN  : adafruit:samd:adafruit_circuitplayground_m0   (arduino:samd: also works)
 * Page  : CircuitPlaygroundSensors.html  (Chrome/Edge, Web Serial)
 *
 * PlaygroundOscuino, sitting next to this one, is generated from the shared
 * template and speaks the pin-oriented Oscuino address space: /d/<pin>, /a/<pin>.
 * That reaches the board's pads but knows nothing about what is soldered to
 * them. This sketch is the other half — the ten NeoPixels and every built-in
 * sensor, by name — modelled on EsploraOscuino:
 *
 *     /cpx ,iiiiififffiiiiiii  <17 args>
 *
 * Everything the board knows about itself in a single packet, sampled in one
 * pass so the values belong to the same instant. With a bundle of separate
 * messages the accelerometer and the touch pads can be milliseconds apart and a
 * receiver cannot tell which readings were simultaneous. Here they are
 * simultaneous by construction, and the sequence counter makes drops visible.
 *
 * Argument order is fixed and positional. Index, name, type, range:
 *
 *    0  seq       i  free-running counter, wraps at 2^31; gaps mean dropped packets
 *    1  buttonA   i  0/1, 1 = pressed  (left button, D4)
 *    2  buttonB   i  0/1, 1 = pressed  (right button, D5)
 *    3  slide     i  0/1, 1 = the side that pulls D7 low (see the note below)
 *    4  light     i  0..1023  ambient light phototransistor (A8)
 *    5  tempC     f  degrees Celsius from the thermistor (A9); NaN if out of range
 *    6  sound     i  0..1023 rough level, or -1 when the microphone is not built in
 *    7  accelX    f  g, roughly -2..2   LIS3DH on the internal I2C bus
 *    8  accelY    f  g
 *    9  accelZ    f  g   (about +1 with the board face up on a table)
 *   10  touch1    i  raw capacitance count, pad A1
 *   11  touch2    i  pad A2
 *   12  touch3    i  pad A3
 *   13  touch4    i  pad A4
 *   14  touch5    i  pad A5
 *   15  touch6    i  pad A6
 *   16  touch7    i  pad A7
 *
 * Untouched pads read 196..275 counts on the board this was written against,
 * and a finger takes them to 686..1014 — a factor of three to five, not a fixed
 * offset, and the resting level depends on what is clipped to the pad. So the
 * page learns each pad's baseline rather than thresholding. A0 is the speaker's
 * DAC output, not a touch pad, which is why the pads start at A1.
 *
 * Reporting is change-driven, not timer-driven. A packet goes out when
 * something actually moved, and otherwise once every heartbeat interval so a
 * page that connects mid-session still gets a full picture and so silence is
 * distinguishable from a dead link. Analog channels are compared with a
 * deadband because the light sensor, microphone and accelerometer never read
 * the same value twice — without one, "on change" degenerates into "always".
 * Buttons and the slide switch bypass the deadband: a press is never noise.
 *
 * Change detection runs on the raw counts, before conversion, so the deadband
 * stays in one unit across every channel. Only send() converts to degrees and g.
 *
 * Inbound, so the page can drive the board:
 *
 *    /hello                       ask for the identity reply below
 *    /pix       <i> <r> <g> <b>   one pixel, 0..9, colours 0..255
 *    /pixels    <30 ints>         all ten at once, r,g,b per pixel
 *    /rgb       <r> <g> <b>       every pixel the same colour
 *    /bright    <0..255>          NeoPixel brightness
 *    /led       <0/1>             the red LED beside the USB socket (D13)
 *    /tone      <freq> [<ms>]     speaker; 0 or no argument stops it
 *    /rate      <ms>              floor on the gap between reports, 0..1000
 *    /heartbeat <ms>              report at least this often, 0 disables
 *    /deadband  <counts>          analog change needed to trigger, 0..64
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
 * on; without it the sketch builds unchanged and reports sound as -1.
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
 * STILL UNVERIFIED, and deliberately not claimed above:
 *
 *   - the microphone, which needs CPX_MIC and the library below. The -1 it
 *     reports when compiled out is what was observed.
 *   - WHICH SIDE of the slide switch reports 1. Both states were seen, but
 *     nothing here ties either to the silkscreen, so the table above says only
 *     that 1 is the side pulling the pin low. Flip yours and see.
 *   - the ORIENTATION of the thermistor divider. A plausible room temperature
 *     does not prove it: a divider wired the other way round still yields a
 *     number, and near 25 C — the nominal point — a sign error is small. Ten
 *     NeoPixels at brightness 150 for a minute moved the reading by +0.2 C,
 *     which is the same as the ambient drift measured either side of it, so
 *     that experiment settled nothing. To settle it: hold a finger on the
 *     sensor and confirm tempC RISES.
 *
 * The constants below are Adafruit's published values for this board, not
 * anything measured here.
 *
 * Written by Adrian Freed, CNMAT. Part of the CNMAT OSC library.
 */

// Uncomment after installing "Adafruit Zero PDM Library" to enable the microphone.
// A literal #include inside a taken branch is what makes the library resolve;
// __has_include() cannot do this job, because the header is only on the include
// path once something has already asked for the library by name.
//#define CPX_MIC 1

#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_FreeTouch.h>
#include <OSCMessage.h>
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
static const uint8_t  PIXEL_COUNT = NEOPIXEL_NUM;
static const long     BAUD = 115200;     // ignored by native USB, kept for clarity
static const int      ARG_COUNT = 17;

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
// Both messages must outlive a single pass through loop(). pollOSC() returns as
// soon as the serial buffer runs dry, which for anything but a very short frame
// happens part way through one; a message declared inside loop() would lose the
// bytes it had already accumulated every time that happened.
static OSCMessage msgIn;
static OSCMessage msgOut("/cpx");

static int32_t  seq = 0;
static uint16_t reportInterval = 20;    // floor on the gap between reports, ms
static uint16_t heartbeatMs = 2000;     // report even when nothing moves
static uint16_t deadband = 4;           // analog counts needed to count as change
static uint32_t lastReport = 0;
static uint32_t toneEndsAt = 0;         // 0 = not playing; else millis() deadline

// One sample of everything, in raw units, in the order it goes on the wire.
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
static uint8_t accelAddr = 0;   // /hello reports which address answered

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
// is bit-identical to the old manual conversion and the wire format is
// unchanged. Zero on every axis when no device answered, which is also a
// physically impossible reading (gravity is always somewhere), so a receiver
// can tell a missing accelerometer from a still one.
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
 * change. This is also what makes /bright cheap to sweep: nothing else has to
 * remember what the ring was showing.
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

static void routePix(OSCMessage &m) {
  if (m.size() < 4) return;
  int i = m.getInt(0);
  if (i < 0 || i >= PIXEL_COUNT) return;
  setPixel(i, colourArg(m, 1), colourArg(m, 2), colourArg(m, 3));
  pixels.show();
}

// All ten in one message: r,g,b per pixel, in pixel order. Short messages set
// as many pixels as they carry, so /pixels with 3 arguments lights only pixel 0.
static void routePixels(OSCMessage &m) {
  int n = m.size() / 3;
  if (n > PIXEL_COUNT) n = PIXEL_COUNT;
  for (int i = 0; i < n; i++)
    setPixel(i, colourArg(m, i * 3), colourArg(m, i * 3 + 1), colourArg(m, i * 3 + 2));
  pixels.show();
}

static void routeRgb(OSCMessage &m) {
  if (m.size() < 3) return;
  uint8_t r = colourArg(m, 0), g = colourArg(m, 1), b = colourArg(m, 2);
  for (uint8_t i = 0; i < PIXEL_COUNT; i++) setPixel(i, r, g, b);
  pixels.show();
}

static void routeBright(OSCMessage &m) {
  if (m.size() < 1) return;
  pixels.setBrightness((uint8_t) constrain(m.getInt(0), 0, 255));
  replayPixels();          // see the note above setPixel(): without this, 0 is a cliff
  pixels.show();
}

static void routeLed(OSCMessage &m) {
  if (m.size() < 1) return;
  digitalWrite(LED_BUILTIN, m.getInt(0) ? HIGH : LOW);
}

// The amplifier is left shut down while nothing is playing: enabled all the
// time it idles with an audible hiss, and the DAC pad is also a touch pad's
// neighbour. loop() switches it back off when a timed tone runs out.
static void speaker(bool on) { digitalWrite(PIN_SPEAKER_EN, on ? HIGH : LOW); }

static void routeTone(OSCMessage &m) {
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
    toneEndsAt = 0;                      // plays until /tone 0
  }
}

// Sent once at startup and again whenever a host asks. Asking matters more than
// the startup one does: the board says hello 300 ms after it enumerates, which
// is long before a person has clicked Connect, so a page that only listened
// would never see it — and this is where it learns whether the accelerometer
// answered at all. Measured on hardware: re-flashing and reopening the port as
// fast as the host allows still misses the startup hello every time.
static bool haveAccel = false;

static void sayHello() {
  OSCMessage hello("/hello");
  hello.add("CircuitPlaygroundSensors")
       .add((int32_t) ARG_COUNT)
       .add((int32_t) PIXEL_COUNT)
       .add((int32_t) (haveAccel ? accelAddr : 0));
  SLIPSerial.beginPacket();
  hello.send(SLIPSerial);
  SLIPSerial.endPacket();
}

static void routeHello(OSCMessage &m) {
  (void) m;
  sayHello();
  havePrev = false;          // and a full state report right behind it
}

static void routeRate(OSCMessage &m) {
  if (m.size() < 1) return;
  reportInterval = (uint16_t) constrain(m.getInt(0), 0, 1000);
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

// Returns true once a whole packet has been accumulated in msgIn.
static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int avail = SLIPSerial.available();
    if (avail <= 0) return false;            // nothing buffered; try next loop()
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) msgIn.fill((uint8_t) c);   // read() returns -1 on a SLIP error
    }
  }
  return true;
}

/* --------------------------------------------------------------- outbound */

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

static void send(const Sample &s) {
  // empty() keeps the address and reuses the allocation, so the steady state
  // does not churn the heap.
  msgOut.empty();
  msgOut.add(seq++)
        .add(s.buttonA).add(s.buttonB).add(s.slide)
        .add(s.light)
        .add(thermistorC(s.tempRaw))
        .add(s.sound)
        .add(s.accX * 0.001f).add(s.accY * 0.001f).add(s.accZ * 0.001f);
  for (uint8_t i = 0; i < TOUCH_COUNT; i++) msgOut.add(s.touch[i]);

  SLIPSerial.beginPacket();
  msgOut.send(SLIPSerial);
  SLIPSerial.endPacket();
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
    if (!msgIn.hasError()) {
      msgIn.dispatch("/hello",     routeHello);
      msgIn.dispatch("/pix",       routePix);
      msgIn.dispatch("/pixels",    routePixels);
      msgIn.dispatch("/rgb",       routeRgb);
      msgIn.dispatch("/bright",    routeBright);
      msgIn.dispatch("/led",       routeLed);
      msgIn.dispatch("/tone",      routeTone);
      msgIn.dispatch("/rate",      routeRate);
      msgIn.dispatch("/heartbeat", routeHeartbeat);
      msgIn.dispatch("/deadband",  routeDeadband);
    }
    msgIn.empty();
  }

  uint32_t now = millis();

  // tone(pin, freq, ms) stops the sound itself; this only mutes the amplifier
  // afterwards so it is not left hissing between beeps.
  if (toneEndsAt && (int32_t)(now - toneEndsAt) >= 0) {
    speaker(false);
    toneEndsAt = 0;
  }

  // reportInterval is a floor, not a period: it caps how fast change can push
  // packets out, so a noisy microphone cannot saturate the link.
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

// Adafruit PyBadge M4 Express — the whole badge over OSC.
//
//   arduino-cli compile -b adafruit:samd:adafruit_pybadge_m4 \
//       --upload --port /dev/cu.usbmodemXXXX examples/PyBadgeOscuino
//
// Needs only: Adafruit GFX, Adafruit ST7735 and ST7789, Adafruit NeoPixel,
// Adafruit LIS3DH. No Arcada — the one thing that library really knows is
// how the eight buttons are wired, and those three pins are below, so there
// is no reason to drag in its dependency tree for a plain Arduino sketch.
//
// Pin numbers taken from Adafruit's own board definition
// (Adafruit_Arcada_Library/Boards/Adafruit_Arcada_PyBadge.h), not guessed:
// TFT CS 44, DC 45, RST 46, backlight 47, 160x128 landscape; speaker on A0
// with its amplifier enable on 51; five NeoPixels on 8; light sensor A7;
// battery divider A6; buttons on a 74HC165 shift register, clock 48, data 49,
// latch 50.
//
// Speaks the capability-named address space in ADDRESSES.md. Ask /enq and
// the badge answers one bundle: /enq "PyBadgeOscuino", then one /enq line
// per capability it can prove it has -- /enq/display 160 128, /enq/buzz,
// /enq/rgb 5, /enq/btn 8, /enq/light, /enq/bat, and /enq/imu 3 or /enq/mic
// only when the LIS3DH or the PDM microphone actually answered its probe in
// setup(). A page shows panels for what is announced, not for the board name.
//
// Writes. ADDRESSES.md marks the display writes, /buzz, /rgb, /s/l and /rate
// "echoed"; /mic/gain is a request that also reads, so it answers too. Each
// replies on the address it arrived on with the value actually applied, which
// is not always the one sent -- /rate clamps, /buzz fills in its defaults:
//   /display/text "a" ["b"]       two lines of text; answers /display/text <n>
//   /display/fill r g b           background colour; redraws the text
//   /display/rect x y w h r g b   a filled rectangle, screen pixels
//   /display/bl 0..255            backlight
//   /buzz hz [ms] [vol]           DAC speaker; vol 0..255, 0 hz stops
//   /rgb r g b                    all five NeoPixels one colour
//   /rgb/<n> r g b                one of them, n = 0..4
//   /rgb/pixels r g b ...         a whole frame, one triple per pixel
//   /rgb/bright 0..255            NeoPixel brightness
//   /mic/gain [<i>]               PDM gain; no arg reads it back
//   /s/l 0|1                      the red LED
//   /rate ms                      streaming period; 0 stops, else 20..5000
//
// Reads. "Every request that reads something answers on the same address",
// so each of these takes no arguments and replies on its own address, with
// exactly the shape it has in the stream below:
//   /enq                        the enq bundle again
//   /btn  /light  /bat            always answered
//   /imu  /mic                    answered only when that probe succeeded;
//                                 a badge without the part stays silent
//
// Streamed every /rate ms (default 50) as one bundle, sampled in one pass:
//   /state seq millis
//   /btn l u d r sel st a b       eight ints, 1 = pressed, in 74HC165 bit order
//   /light raw                    light sensor on A7, 10-bit
//   /bat mv                       battery millivolts, from the A6 divider
//   /imu x y z                    LIS3DH, floats in g; only when the part is present
// On an EdgeBadge, /mic goes out on the same /rate tick as the bundle, as its
// own packet rather than a bundle member -- see sendMic().
//
// Open PyBadgeOscuino.html to drive it. Chrome or Edge, served over
// http://localhost — Web Serial does not work from a file:// URL.

#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LIS3DH.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// PDM microphone. The EdgeBadge (Adafruit's TensorFlow Lite badge) is a PyBadge
// with a PDM MEMS mic added; a plain PyBadge or PyBadge LC has none.
//
// Both are safe. Leave this at 1 on either board: a badge with no mic leaves
// /enq/mic out of its enq bundle, sends no /mic, and the page hides its
// microphone panel. Set it to 0 to drop the code entirely and save the flash
// on a board you know has none. Both boards share one FQBN, so the choice
// cannot be made at compile time -- see the probe in setup() for how a
// missing mic is actually detected.
//
// Use Adafruit_ZeroPDMSPI, NOT Adafruit_ZeroPDM. Both ship in the same
// library (Adafruit_Zero_PDM_Library), and picking the wrong one silently
// yields no microphone at all: ZeroPDM drives PDM through the SAMD I2S
// peripheral and its begin() returns false unless the variant defines I2S pin
// macros such as PIN_PA10G_I2S_SCK0, while pybadge_m4's variant.h declares
// I2S_INTERFACES_COUNT 0. Measured on an EdgeBadge: with ZeroPDM, micOK is
// false and no /mic is ever sent. This board instead clocks its mic through a
// SERCOM in SPI mode -- SPI2, which is exactly what the variant's own "SPI for
// PDM mic" block (clock 5, data 6) is describing.
//
// STATUS: measured working on an EdgeBadge, 2026-08-11 -- 26.6 dB between the
// quiet-room floor and the loudest frame at MIC_GAIN 16. The absolute levels
// those runs printed were 32x low because of the rms scaling bug fixed below;
// corrected, the floor is near -34 dBFS. The no-microphone path is verified separately
// (see setup()). Still unmeasured: MIC_GAIN 8, the shipped default -- the
// figures above are at 16, and halving them is arithmetic, not a capture.
// Addresses renamed onto ADDRESSES.md on 2026-09-03 (/screen/text ->
// /display/text, /screen/fill -> /display/fill, /screen/box -> /display/rect,
// /screen/backlight -> /display/bl, bare /tone -> /buzz, /pixels ->
// /rgb/pixels, /pixel <n> -> /rgb/<n>, /led -> /s/l, the /pybadge blob ->
// /state + /btn + /light + /bat + /imu in one bundle, /enq booleans ->
// /enq/imu and /enq/mic); that build is compile-checked and has not been
// re-run on the board. The same edit gave every write its echo, gave /btn,
// /light, /bat, /imu and /mic single-shot replies, and put /mic on the /rate
// tick -- also compile-checked only.
//
// decimateFilterWord() is an INTERRUPT routine, not a polling call. It reads
// and rewrites the SERCOM data register unconditionally to keep the bit stream
// gapless, and begin() enables the data-register-empty interrupt at NVIC
// priority 0. Two consequences, the first of them observed on hardware:
//
//   * The handler MUST be defined. Leave SERCOM3_0_Handler undefined and
//     begin() arms an interrupt that vectors into the core's default trap --
//     the board locks up in setup() and never sends a single packet. Measured:
//     zero frames in three seconds, versus sixty once the handler existed.
//   * Calling it from loop() instead is not a milder version of the same
//     thing; it corrupts the stream and still leaves the ISR armed.
//
// So the handler below does the DSP and accumulates into volatile state, and
// sendMic() only snapshots that state. The filter runs at 2 interrupts per
// sample -- 32 kHz for 16 kHz audio -- and the library measures itself at
// about 12.5%% CPU at 120 MHz, which leaves USB and the display alone.
#ifndef BADGE_HAS_PDM_MIC
#define BADGE_HAS_PDM_MIC 1
#endif

#if BADGE_HAS_PDM_MIC
#include <Adafruit_ZeroPDMSPI.h>
#define PDM_SPI      SPI2            // the variant's "SPI for PDM mic" SERCOM
#define PDM_HANDLER  SERCOM3_0_Handler   // ...and SPI2 is SERCOM3 on this board
#define PDM_IRQ      SERCOM3_0_IRQn
#define PDM_RATE     16000
// The driver defaults to unity gain, and unity is too quiet to be useful: a
// quiet room measured about -90 dBFS on this mic, an rms of 1 count out of
// 32768 -- so every ordinary sound rounded to nothing. Gain is applied inside
// the driver, after DC removal and BEFORE the clip to 16 bits, so rms and
// peak below are post-gain figures and too much gain destroys loud sounds
// rather than merely brightening quiet ones.
//
// Measured at gain 16, 30 s of speech and taps at an EdgeBadge. Those runs
// predate the rms scaling fix above, so the raw figures they reported were
// 32x low; corrected to true full scale they are a noise floor near rms 640
// (-34 dBFS) rising to about 13700 (-8 dBFS) on the loudest frame, with the
// transient pinning peak at 32767 -- clipped. The 26.6 dB between floor and
// loudest frame is a RATIO and so was never affected by the scale error.
// Hence gain 8. The gain is a plain multiply ahead of the clip, so the floor
// should halve and the headroom double; that arithmetic has not itself been
// re-measured under load. Raise it for a quiet installation, lower it if you
// expect to shout at the badge.
#define MIC_GAIN     8.0f
#define SCOPE_POINTS 96
// An OSC blob is padded to a multiple of four. /mic is written field by field
// rather than through OSCMessage::add() -- the scope trace is streamed
// straight out of the capture buffer instead of being copied into a message --
// and a hand-written emitter has no builder to get the padding right for it.
// 96 divides by 4, so no pad bytes are emitted. Assert it rather than rely on
// it: change this to 100 and every /mic frame silently becomes malformed OSC.
static_assert(SCOPE_POINTS % 4 == 0, "OSC blob payload must be a multiple of 4");
#define SCOPE_DECIM  8               // 16 kHz / 8 = one scope point per 0.5 ms
static Adafruit_ZeroPDMSPI pdmspi(&PDM_SPI);
static bool  micOK   = false;
// MIC_GAIN is the boot value; /mic/gain moves it at runtime and reads it back.
static float micGain = MIC_GAIN;

// written by PDM_HANDLER, read and cleared by sendMic() with the IRQ masked.
//
// micCount is uint32_t, not uint16_t: the window is now one /rate period
// rather than a fixed 50 ms, and /rate accepts up to 5000 ms. At 16 kHz that
// is 80000 samples, which a uint16 wraps -- and a wrapped count divides the
// sum of squares by the wrong number, so rms would come back wildly wrong at
// exactly the slow rates a user picks to calm the stream down. uint32 holds
// 74 hours of it.
static volatile uint64_t micSumSq  = 0;   // sum of v*v at full scale
static volatile uint32_t micCount  = 0;
static volatile int32_t  micPeak   = 0;   // full-scale, +-32768
static volatile int16_t  micScope[SCOPE_POINTS];   // full-scale samples
static volatile uint8_t  micScopeN = 0;
static volatile uint8_t  micDecim  = 0;
#else
static const bool micOK = false;
#endif

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

#define TFT_CS        44
#define TFT_DC        45
#define TFT_RST       46
#define TFT_LITE      47
#define SPKR_ENABLE   51
#define AUDIO_OUT     A0
#define NEOPIX_PIN     8
#define NEOPIX_NUM     5
#define LIGHT_SENSOR  A7
#define BATTERY_SENSE A6
#define BTN_CLOCK     48
#define BTN_DATA      49
#define BTN_LATCH     50

// Bit positions in the shift register byte: left 0x01, up 0x02, down 0x04,
// right 0x08, select 0x10, start 0x20, A 0x40, B 0x80. /btn sends one int
// per button in that order, bit 0 first, so the page never sees the byte.
#define BTN_COUNT      8

// The PyBadge's display is on SPI1 -- a second SERCOM on pins 41/42/43, per
// the variant header's "Internal SPI for TFT" block. Constructing the driver
// without naming the bus uses the default SPI, and the screen stays blank
// with no error: the pins are simply not connected to anything.
Adafruit_ST7735  tft = Adafruit_ST7735(&SPI1, TFT_CS, TFT_DC, TFT_RST);
Adafruit_NeoPixel pixels(NEOPIX_NUM, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_LIS3DH   lis;

static bool     accelOK = false;
static uint32_t reportEvery = 50;           // /rate, ms; 0 = not streaming
static uint32_t seq = 0;                    // /state sequence number
static char     line1[26] = "PyBadgeOscuino";
static char     line2[26] = "waiting for OSC";
static uint16_t bgColour = ST77XX_BLACK;

// Everything the badge says, hello and stream alike, goes out as a bundle
// built here and flushed as one SLIP packet.
static OSCBundle bundleOUT;

static void flushBundle() {
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

// Single messages: the echo of a write and the answer to a read. ADDRESSES.md
// wants both -- "/s/l ... echoed", "/rate ... Echoed", "/buzz ... echoed",
// "/rgb ... echoed", the display writes "echoed", and "Every request that
// reads something answers on the same address" -- and neither belongs in the
// stream bundle, because both are answers to something that just arrived
// rather than readings taken on the stream tick. So each goes out on its own
// as its own SLIP packet.
static void sendMsg(OSCMessage &m) {
  SLIPSerial.beginPacket();
  m.send(SLIPSerial);
  SLIPSerial.endPacket();
}

// The commonest echo: the address the message arrived on, carrying its first
// `count` int arguments clamped exactly as the route clamped them.
static void echoInts(OSCMessage &m, int count) {
  OSCMessage e(m.getAddress());
  for (int i = 0; i < count; i++) e.add((intOSC_t) constrain(m.getInt(i), 0, 255));
  sendMsg(e);
}

/* ------------------------------------------------------- raw OSC helpers */

// The scope is a blob; writing it straight to the wire avoids OSCMessage
// mallocing and copying it every frame.
static void writePadded(Print &p, const char *str) {
  size_t n = strlen(str) + 1;
  p.write((const uint8_t *) str, n);
  while (n & 3) { p.write((uint8_t) 0); n++; }
}
static void writeBE32(Print &p, uint32_t v) {
  const uint8_t b[4] = { (uint8_t)(v>>24), (uint8_t)(v>>16), (uint8_t)(v>>8), (uint8_t)v };
  p.write(b, 4);
}

/* ----------------------------------------------------------------- buttons */

// One 74HC165: pulse the latch to sample all eight inputs, then clock them
// out MSB first.
//
// Do NOT use Arduino's shiftIn() here. It drives the clock HIGH and *then*
// samples, whereas this register presents its first bit as soon as the latch
// rises and advances on the clock edge -- so sampling after the edge reads
// every bit one position late and rotates the whole byte. Every button then
// appears as its neighbour. Read first, clock afterwards, which is what
// Adafruit's own Arcada driver does.
static uint8_t readButtons() {
  digitalWrite(BTN_LATCH, LOW);
  delayMicroseconds(1);
  digitalWrite(BTN_LATCH, HIGH);
  delayMicroseconds(1);

  uint8_t bits = 0;
  for (int i = 0; i < 8; i++) {
    bits <<= 1;
    bits |= digitalRead(BTN_DATA);      // sample BEFORE the clock edge
    digitalWrite(BTN_CLOCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(BTN_CLOCK, LOW);
    delayMicroseconds(1);
  }
  return bits;
}

/* ------------------------------------------------------------------ screen */

static void redraw() {
  tft.fillScreen(bgColour);
  tft.setTextWrap(true);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 8);   tft.print(line1);
  tft.setTextSize(2);
  tft.setCursor(5, 30);  tft.print(line2);
}

/* ----------------------------------------------------------------- speaker */

// Audio is the DAC on A0 behind a class-D amp whose enable is pin 51. tone()
// does not drive a DAC pin on SAMD and analogWrite() gives one DC level, so
// the square wave is generated here from two DAC levels. Blocking on purpose:
// the notes are short and it keeps the example readable.
static void beep(uint16_t freq, uint16_t ms, uint8_t vol) {
  if (!freq || !ms) return;
  digitalWrite(SPKR_ENABLE, HIGH);
  const uint32_t halfUs = 500000UL / freq;
  const uint32_t cycles = ((uint32_t) ms * 1000UL) / (halfUs * 2);
  const int hi = 512 + (vol * 2), lo = 512 - (vol * 2);
  for (uint32_t i = 0; i < cycles; i++) {
    analogWrite(AUDIO_OUT, hi); delayMicroseconds(halfUs);
    analogWrite(AUDIO_OUT, lo); delayMicroseconds(halfUs);
  }
  analogWrite(AUDIO_OUT, 512);            // rest at mid-scale, not at 0
  digitalWrite(SPKR_ENABLE, LOW);         // quiet the amp between notes
}

#if BADGE_HAS_PDM_MIC
/* --------------------------------------------------------------------- mic */

// The SERCOM's data-register-empty interrupt, two calls per audio sample.
// decimateFilterWord() returns true on the second of each pair, handing back a
// 16-bit sample centred on 32768 with the DC offset tracked out.
//
// Keep this short, but do NOT pre-shift v before squaring. An earlier version
// accumulated (v>>5)^2 and justified it as overflow protection -- which was
// simply wrong: micSumSq is uint64_t, and a single v*v of 1.07e9 fits a
// uint32 addend anyway, so nothing could overflow. What the shift actually
// did was send rms out 32x low, i.e. 30 dB, while `peak` in the same message
// went out at full scale, so two numbers derived from the same samples
// disagreed by 30 dB and the page's one dBFS conversion could not be right
// for both. Every absolute level this sketch's header used to quote was
// wrong by that amount. PicoMateOscuino warns against this exact shift by
// name; this file had the bug it warns about.
void PDM_HANDLER(void) {
  uint16_t raw;
  if (!pdmspi.decimateFilterWord(&raw)) return;

  const int32_t v = (int32_t) raw - 32768;
  micSumSq += (uint32_t) (v * v);            // full scale, into a uint64
  micCount++;

  // No (int16_t) here: |v| reaches 32768, which does not fit int16_t and
  // wrapped to -32768 -- a negative peak exactly when the mic clipped
  // hardest. micPeak is int32_t precisely so it does not need narrowing.
  const int32_t a = v < 0 ? -v : v;
  if (a > micPeak) micPeak = a;

  if (++micDecim >= SCOPE_DECIM) {
    micDecim = 0;
    if (micScopeN < SCOPE_POINTS)
      // Full-scale, matching the declaration: sendMic() normalises the
      // window to ITS OWN peak, so a pre-shrunk (v>>8) store made a quiet
      // room's +-2 counts normalise up to a full-amplitude square wave.
      micScope[micScopeN++] = (int16_t) v;
  }
}

// /mic ,iiib  rms, peak, sampleRate, scope
//
// That is exactly ADDRESSES.md's four-argument form, `/mic <i> <i> [<i> <b>]`
// -- rms and peak at full scale, and the optional pair the contract describes
// as what "boards with a scope add", the sample rate and a waveform blob.
// Nothing here is an extension.
//
// It goes out on the same /rate tick as the stream bundle, but as its OWN
// packet rather than a bundle member: the scope trace is written straight
// from the capture buffer to the wire below, and a bundle member would have
// to be copied into an OSCMessage first.
//
// The scope covers only the first SCOPE_POINTS * SCOPE_DECIM samples of the
// window -- 768 of them, 48 ms -- because the ISR stops storing once the
// buffer is full. rms and peak still cover the whole window, so at a slow
// /rate the numbers describe the period and the waveform describes its
// opening. At the default 50 ms they are the same thing.
//
// rms and peak go out at FULL SCALE, 0..32767, because that is the number a
// receiver can actually reason about -- it is comparable between frames, and
// the page turns it into dBFS for the meter. Sending a pre-scaled 0..127 was
// worse than useless here: this mic idles around 150 counts, so everything
// quieter than a shout rounded to zero.
//
// The scope blob has one signed byte per point, normalised to the frame's own
// peak. A fixed shift cannot serve both ends of a range this wide -- the same
// divisor that renders the noise floor clips ordinary speech flat -- so the
// waveform is sent shape-only and `peak` is the scale factor for it.
static void sendMic() {
  int16_t  raw[SCOPE_POINTS];
  int8_t   scope[SCOPE_POINTS];
  uint64_t sumsq;
  uint32_t count;
  int32_t  peak;
  uint8_t  n;

  NVIC_DisableIRQ(PDM_IRQ);
  sumsq = micSumSq; count = micCount; peak = micPeak; n = micScopeN;
  for (uint8_t i = 0; i < n; i++) raw[i] = micScope[i];
  micSumSq = 0; micCount = 0; micPeak = 0; micScopeN = 0; micDecim = 0;
  NVIC_EnableIRQ(PDM_IRQ);

  if (!count) return;                       // interrupt stopped: say nothing

  // Normalise against the peak of the SCOPE WINDOW, not the frame peak. The
  // scope keeps every SCOPE_DECIM'th sample, so it routinely misses the lone
  // spike that set `peak` -- measured: frame peak 154 while every retained
  // sample was 0 or 1, which normalising by 154 rendered as a flat line.
  int32_t swing = 0;
  for (uint8_t i = 0; i < n; i++) {
    const int32_t a = raw[i] < 0 ? -raw[i] : raw[i];
    if (a > swing) swing = a;
  }
  for (uint8_t i = 0; i < n; i++)
    scope[i] = swing ? (int8_t) ((raw[i] * 127L) / swing) : 0;
  for (uint8_t i = n; i < SCOPE_POINTS; i++) scope[i] = 0;

  // Float division, deliberately. `sumsq / count` in uint64 is integer
  // division: a quiet room puts the mean square between 1 and 3, which
  // truncates to 1, and every rms in the capture came back as exactly 1.
  const int32_t rms = (int32_t) sqrtf((float) sumsq / (float) count);

  SLIPSerial.beginPacket();
  writePadded(SLIPSerial, "/mic");
  writePadded(SLIPSerial, ",iiib");
  writeBE32(SLIPSerial, (uint32_t) rms);
  writeBE32(SLIPSerial, (uint32_t) peak);
  writeBE32(SLIPSerial, PDM_RATE);
  writeBE32(SLIPSerial, SCOPE_POINTS);
  SLIPSerial.write((const uint8_t *) scope, SCOPE_POINTS);
  SLIPSerial.endPacket();
}
#endif

/* ----------------------------------------------------------------- inbound */

// /display/text "a" ["b"]. The display capability's reply in ADDRESSES.md is
// `/display/text <i>` lines drawn, so that is what goes back: the number of
// text lines this message supplied. redraw() repaints both slots either way,
// but only the ones counted here were changed by the request.
static void routeText(OSCMessage &m) {
  intOSC_t lines = 0;
  if (m.size() > 0 && m.isString(0)) { m.getString(0, line1, sizeof line1); lines++; }
  if (m.size() > 1 && m.isString(1)) { m.getString(1, line2, sizeof line2); lines++; }
  redraw();
  OSCMessage e("/display/text"); e.add(lines); sendMsg(e);
}
static void routeFill(OSCMessage &m) {              // /display/fill r g b, echoed
  if (m.size() < 3) return;
  bgColour = tft.color565(constrain(m.getInt(0), 0, 255),
                          constrain(m.getInt(1), 0, 255),
                          constrain(m.getInt(2), 0, 255));
  redraw();
  echoInts(m, 3);
}
static void routeBacklight(OSCMessage &m) {         // /display/bl 0..255, echoed
  if (m.size() < 1 || !m.isInt(0)) return;
  analogWrite(TFT_LITE, constrain(m.getInt(0), 0, 255));
  echoInts(m, 1);
}
static void routeRect(OSCMessage &m) {              // /display/rect x y w h r g b
  if (m.size() < 7) return;
  tft.fillRect(m.getInt(0), m.getInt(1), m.getInt(2), m.getInt(3),
               tft.color565(constrain(m.getInt(4), 0, 255),
                            constrain(m.getInt(5), 0, 255),
                            constrain(m.getInt(6), 0, 255)));
  // Not echoInts(): x, y, w and h are screen coordinates, not 0..255 colours,
  // and clamping them to 255 would misreport a rectangle drawn off the right
  // of a 160-wide panel. Echo the geometry as sent and the colour as clamped.
  OSCMessage e("/display/rect");
  for (int i = 0; i < 4; i++) e.add((intOSC_t) m.getInt(i));
  for (int i = 4; i < 7; i++) e.add((intOSC_t) constrain(m.getInt(i), 0, 255));
  sendMsg(e);
}
static void routeBuzz(OSCMessage &m) {              // /buzz hz [ms] [vol], echoed
  if (m.size() < 1 || !m.isInt(0)) return;
  const intOSC_t hz  = constrain(m.getInt(0), 0, 8000);
  const intOSC_t ms  = (m.size() > 1 && m.isInt(1)) ? constrain(m.getInt(1), 0, 3000) : 150;
  const intOSC_t vol = (m.size() > 2 && m.isInt(2)) ? constrain(m.getInt(2), 0, 255) : 120;
  // Echo BEFORE playing. beep() blocks for the whole note, so echoing after it
  // would hold the acknowledgement back by up to three seconds and make the
  // page's next write look like it had been dropped.
  OSCMessage e("/buzz"); e.add(hz).add(ms).add(vol); sendMsg(e);
  beep(hz, ms, vol);
}

// r g b at argument index i of a message, clamped, as a NeoPixel colour.
static uint32_t colourAt(OSCMessage &m, int i) {
  return pixels.Color(constrain(m.getInt(i),     0, 255),
                      constrain(m.getInt(i + 1), 0, 255),
                      constrain(m.getInt(i + 2), 0, 255));
}

// One route for the whole rgb capability. `offset` is where the address
// stops matching "/rgb", so the remainder tells the four shapes apart:
//   /rgb r g b            remainder empty     all five, one colour
//   /rgb/<n> r g b        remainder /<n>      one pixel
//   /rgb/pixels r g b ... remainder /pixels   a frame, one triple per pixel
//   /rgb/bright 0..255    remainder /bright   the strip's brightness
// All four are echoed, on the address they arrived on.
static void routeRgb(OSCMessage &m, int offset) {
  if (m.fullMatch("/bright", offset)) {
    if (m.size() < 1 || !m.isInt(0)) return;
    // setBrightness() only takes effect on the next show(), and it rescales
    // from the colours last set rather than from the ones on the wire.
    pixels.setBrightness(constrain(m.getInt(0), 0, 255));
    pixels.show();
    echoInts(m, 1);
    return;
  }
  if (m.fullMatch("/pixels", offset)) {
    // A short frame leaves the pixels it does not name as they were.
    const int n = min(m.size() / 3, (int) NEOPIX_NUM);
    for (int i = 0; i < n; i++) pixels.setPixelColor(i, colourAt(m, 3 * i));
    pixels.show();
    echoInts(m, 3 * n);                             // echo what was applied
    return;
  }
  if (m.size() < 3) return;
  if (m.getAddressLength(offset) == 0) {            // bare /rgb: all five
    const uint32_t c = colourAt(m, 0);
    for (int i = 0; i < NEOPIX_NUM; i++) pixels.setPixelColor(i, c);
    pixels.show();
    echoInts(m, 3);
    return;
  }
  char rest[8];                                     // "/<n>", or something else
  m.getAddress(rest, offset, sizeof rest);
  if (rest[0] != '/' || !isdigit((unsigned char) rest[1])) return;
  pixels.setPixelColor(constrain(atoi(rest + 1), 0, NEOPIX_NUM - 1), colourAt(m, 0));
  pixels.show();
  echoInts(m, 3);
}
static void routeLed(OSCMessage &m) {               // /s/l 0|1, echoed
  if (m.size() < 1 || !m.isInt(0)) return;
  const intOSC_t on = m.getInt(0) ? 1 : 0;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
  OSCMessage e("/s/l"); e.add(on); sendMsg(e);
}
static void routeRate(OSCMessage &m) {              // /rate <ms>, 0 stops, echoed
  if (m.size() < 1 || !m.isInt(0)) return;
  const int v = m.getInt(0);
  reportEvery = v <= 0 ? 0 : constrain(v, 20, 5000);
  // Echo the value in force, not the one asked for: 5 becomes 20 and 9000
  // becomes 5000, and a page that drew its slider from the request would sit
  // there disagreeing with the board about the rate it is actually running.
  OSCMessage e("/rate"); e.add((intOSC_t) reportEvery); sendMsg(e);
}

/* ------------------------------------------------------------- the readings

   Each capability's reading is built in one place and used twice: added to
   the stream bundle on the /rate tick, and sent on its own when the address
   is asked for with no arguments. ADDRESSES.md: "Every request that reads
   something answers on the same address" -- and the enq bundle announces
   /enq/btn, /enq/light, /enq/bat, so those requests have to answer, or a page
   reading the contract would take the silence for "no buttons". */

static void fillBtn(OSCMessage &m) {                // eight ints, 1 = pressed
  const uint8_t bits = readButtons();
  for (int i = 0; i < BTN_COUNT; i++) m.add((intOSC_t) ((bits >> i) & 1));
}
static void fillLight(OSCMessage &m) {              // A7, 10-bit raw
  m.add((intOSC_t) analogRead(LIGHT_SENSOR));
}
// A6 sits on a divider giving half the battery voltage, read 10-bit against
// the 3.3 V reference -- the arithmetic the page used to do, moved here so
// /bat is millivolts as the contract says.
static void fillBat(OSCMessage &m) {
  m.add((intOSC_t) ((analogRead(BATTERY_SENSE) * 6600L) / 1023));
}
// x_g/y_g/z_g are in g: Adafruit_LIS3DH::read() (library 1.3.0) scales the raw
// counts by the selected range -- its own comment reads "raw_lsb => 10-bit lsb
// -> milli-gs -> gs" -- and getEvent() multiplies them by
// SENSORS_GRAVITY_STANDARD to make m/s^2.
static void fillImu(OSCMessage &m) {
  lis.read();
  m.add(lis.x_g).add(lis.y_g).add(lis.z_g);
}

static void routeBtnAsk(OSCMessage &)   { OSCMessage m("/btn");   fillBtn(m);   sendMsg(m); }
static void routeLightAsk(OSCMessage &) { OSCMessage m("/light"); fillLight(m); sendMsg(m); }
static void routeBatAsk(OSCMessage &)   { OSCMessage m("/bat");   fillBat(m);   sendMsg(m); }

// "Absence is silence": a badge whose LIS3DH did not answer left /enq/imu out
// of its hello, so /imu must answer nothing at all -- not a zero, not a -1.
static void routeImuAsk(OSCMessage &) {
  if (!accelOK) return;
  OSCMessage m("/imu"); fillImu(m); sendMsg(m);
}

#if BADGE_HAS_PDM_MIC
static void routeMicAsk(OSCMessage &) { if (micOK) sendMic(); }
// /mic/gain [<i>]: with an argument it sets the gain, and either way it
// answers with the gain now in force. Silent on a badge with no microphone.
static void routeMicGain(OSCMessage &m) {
  if (!micOK) return;
  if (m.size() >= 1 && m.isInt(0)) {
    micGain = (float) constrain(m.getInt(0), 1, 64);
    pdmspi.setMicGain(micGain);
  }
  OSCMessage e("/mic/gain"); e.add((intOSC_t) micGain); sendMsg(e);
}
#endif

/* -------------------------------------------------------------------- main */

// The boot /enq is very nearly always lost: the board resets, its USB
// device re-enumerates, and the host opens the port some hundreds of
// milliseconds later, by which time setup() has long finished. Measured on
// this repo's ESP32 and SAMD boards -- a probe opening the port straight
// after flashing never once caught it. So /enq is also an INBOUND address
// and the page asks for it on connect.
//
// The reply is a bundle: the name, then one /enq line per capability that is
// actually there. The display, speaker, NeoPixels, buttons, light sensor and
// battery divider are soldered to every PyBadge; the accelerometer and the
// microphone are announced only if their probes in setup() succeeded, so a
// page decides whether to show those panels from the hello, not from a flag.
static void sendEnq() {
  bundleOUT.add("/enq").add("PyBadgeOscuino");
  bundleOUT.add("/enq/led");        // this board has a plain LED on /s/l
  bundleOUT.add("/enq/display").add((intOSC_t) tft.width()).add((intOSC_t) tft.height());
  bundleOUT.add("/enq/buzz");
  bundleOUT.add("/enq/rgb").add((intOSC_t) NEOPIX_NUM);
  bundleOUT.add("/enq/btn").add((intOSC_t) BTN_COUNT);
  bundleOUT.add("/enq/light");
  bundleOUT.add("/enq/bat");
  if (accelOK) bundleOUT.add("/enq/imu").add((intOSC_t) 3);
  if (micOK)   bundleOUT.add("/enq/mic");
  flushBundle();
}

static void routeEnq(OSCMessage &) { sendEnq(); }

void setup() {
  SLIPSerial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TFT_LITE, OUTPUT);      analogWrite(TFT_LITE, 255);
  pinMode(SPKR_ENABLE, OUTPUT);   digitalWrite(SPKR_ENABLE, LOW);
  pinMode(BTN_LATCH, OUTPUT);     digitalWrite(BTN_LATCH, HIGH);
  pinMode(BTN_CLOCK, OUTPUT);    digitalWrite(BTN_CLOCK, LOW);
  pinMode(BTN_DATA, INPUT);

  analogWriteResolution(10);      // the DAC is 10-bit
  analogWrite(AUDIO_OUT, 512);    // idle at mid-scale so it does not click

  tft.initR(INITR_BLACKTAB);      // 160x128 panel
  tft.setRotation(1);             // landscape, as Adafruit's own definition has it
  redraw();

  pixels.begin(); pixels.setBrightness(40); pixels.clear(); pixels.show();

#if BADGE_HAS_PDM_MIC
  // begin() returns true on any PyBadge -- it only configures a SERCOM, and
  // the SERCOM is there whether or not a microphone is soldered to it. So
  // probe the signal instead: listen for 100 ms and require both that the
  // interrupt is running and that the samples actually move. A missing mic
  // leaves the data line at a constant level, which the driver's DC tracker
  // flattens to exactly zero variance; a real mic always has a noise floor.
  //
  // Verified on a SAMD51 with no microphone on those pins (a Feather M4
  // Express built with this FQBN, running the pre-rename build that sent one
  // board-named state message): the raw filter output was a flat 0, the
  // probe returned micOK false, and that build went on sending its state
  // message with no /mic at all. The path a mic-less PyBadge takes is
  // therefore known good; the renamed bundle has not been re-run on it.
  // The mic-present path IS verified -- see the STATUS note in the header.
  if (pdmspi.begin(PDM_RATE)) {
    pdmspi.setMicGain(micGain);
    delay(100);
    NVIC_DisableIRQ(PDM_IRQ);
    const uint32_t count = micCount;
    const int32_t  peak  = micPeak;
    micSumSq = 0; micCount = 0; micPeak = 0; micScopeN = 0; micDecim = 0;
    NVIC_EnableIRQ(PDM_IRQ);
    micOK = count > 0 && peak > 0;
    if (!micOK) NVIC_DisableIRQ(PDM_IRQ);   // no mic: stop paying for the ISR
  }
#endif

  accelOK = lis.begin(0x19) || lis.begin(0x18);
  if (accelOK) lis.setRange(LIS3DH_RANGE_4_G);

  sendEnq();          // nearly always lost; the page asks again
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
  static uint32_t last = 0;

  if (pollOSC()) {
    if (!inMsg.hasError()) {
      // writes
      inMsg.dispatch("/display/text", routeText);
      inMsg.dispatch("/display/fill", routeFill);
      inMsg.dispatch("/display/bl",   routeBacklight);
      inMsg.dispatch("/display/rect", routeRect);
      inMsg.dispatch("/buzz",         routeBuzz);
      inMsg.route("/rgb",             routeRgb);   // /rgb, /rgb/<n>, /pixels, /bright
      inMsg.dispatch("/s/l",          routeLed);
      inMsg.dispatch("/rate",         routeRate);
      // reads -- one per capability the enq bundle announces, so that
      // "absence is silence" still means something on this board
      inMsg.dispatch("/enq",        routeEnq);
      inMsg.dispatch("/btn",          routeBtnAsk);
      inMsg.dispatch("/light",        routeLightAsk);
      inMsg.dispatch("/bat",          routeBatAsk);
      inMsg.dispatch("/imu",          routeImuAsk);
#if BADGE_HAS_PDM_MIC
      inMsg.dispatch("/mic",          routeMicAsk);
      inMsg.dispatch("/mic/gain",     routeMicGain);
#endif
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (!reportEvery || now - last < reportEvery) return;
  last = now;

  // One bundle, sampled in one pass, so every reading in it belongs to the
  // same instant. /state carries the sequence number and the millis it was
  // taken at; the rest is one message per capability, never a board blob.
  bundleOUT.add("/state").add((intOSC_t) seq++).add((intOSC_t) now);

  fillBtn(bundleOUT.add("/btn"));
  fillLight(bundleOUT.add("/light"));
  fillBat(bundleOUT.add("/bat"));
  if (accelOK) fillImu(bundleOUT.add("/imu"));
  flushBundle();

#if BADGE_HAS_PDM_MIC
  // On the /rate tick, like everything else. ADDRESSES.md retired /mic/rate
  // with the note "one rate per board", so a fixed cadence here would have
  // meant /rate 0 stopping the bundle while /mic carried on and /rate 200
  // leaving it at 50 ms. It is still its own packet rather than a bundle
  // member -- see sendMic() for why the blob is written straight to the wire.
  if (micOK) sendMic();
#endif
}

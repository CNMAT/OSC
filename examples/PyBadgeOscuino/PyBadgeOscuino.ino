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
// Open PyBadgeOscuino.html to drive it. Chrome or Edge, served over
// http://localhost — Web Serial does not work from a file:// URL.

#include <OSCMessage.h>
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
// Both are safe. Leave this at 1 on either board: a badge with no mic reports
// micOK false, sends no /mic, and the page hides its microphone panel. Set it
// to 0 to drop the code entirely and save the flash on a board you know has
// none. Both boards share one FQBN, so the choice cannot be made at compile
// time -- see the probe in setup() for how a missing mic is actually detected.
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
// STATUS: measured working on an EdgeBadge, 2026-08-11 -- quiet-room floor
// rms 20 (-64 dBFS) at MIC_GAIN 16, rising with speech, 26.6 dB between the
// floor and the loudest frame. The no-microphone path is verified separately
// (see setup()). Still unmeasured: MIC_GAIN 8, the shipped default -- the
// figures above are at 16, and halving them is arithmetic, not a capture.
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
// Measured at gain 16, 30 s of speech and taps at an EdgeBadge: the noise
// floor sat at rms 20 (-64 dBFS), quiet peaks ran about 3000 against 25000
// plus when spoken to, and the loudest transient pinned peak at exactly
// 32767 -- clipped. Hence 8. The gain is a plain multiply ahead of the clip,
// so that floor should halve to an rms near 10 and the headroom should
// double; that arithmetic has not itself been re-measured under load. Raise
// it for a quiet installation, lower it if you expect to shout at the badge.
#define MIC_GAIN     8.0f
#define SCOPE_POINTS 96
#define SCOPE_DECIM  8               // 16 kHz / 8 = one scope point per 0.5 ms
static Adafruit_ZeroPDMSPI pdmspi(&PDM_SPI);
static bool micOK = false;

// written by PDM_HANDLER, read and cleared by sendMic() with the IRQ masked
static volatile uint64_t micSumSq  = 0;   // sum of v*v at full scale
static volatile uint16_t micCount  = 0;
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

// Bit positions in the shift register byte, in the order the page expects.
// left 0x01, up 0x02, down 0x04, right 0x08, select 0x10, start 0x20,
// A 0x40, B 0x80.

// The PyBadge's display is on SPI1 -- a second SERCOM on pins 41/42/43, per
// the variant header's "Internal SPI for TFT" block. Constructing the driver
// without naming the bus uses the default SPI, and the screen stays blank
// with no error: the pins are simply not connected to anything.
Adafruit_ST7735  tft = Adafruit_ST7735(&SPI1, TFT_CS, TFT_DC, TFT_RST);
Adafruit_NeoPixel pixels(NEOPIX_NUM, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_LIS3DH   lis;

static bool     accelOK = false;
static uint32_t reportEvery = 50;
static char     line1[26] = "PyBadgeOscuino";
static char     line2[26] = "waiting for OSC";
static uint16_t bgColour = ST77XX_BLACK;

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
// Keep this short. The (v>>5) before squaring is what makes the accumulator
// safe rather than decorative: v is +-32768, so v*v alone reaches 1.07e9 and
// overflows uint32 after four samples. Shifted, each term caps at 1.05e6 and
// a full 50 ms frame of 800 samples sums to at most 8.4e8.
void PDM_HANDLER(void) {
  uint16_t raw;
  if (!pdmspi.decimateFilterWord(&raw)) return;

  const int32_t v = (int32_t) raw - 32768;
  const int32_t q = v >> 5;
  micSumSq += (uint32_t) (q * q);
  micCount++;

  const int32_t a = v < 0 ? -v : v;
  if (a > micPeak) micPeak = (int16_t) a;

  if (++micDecim >= SCOPE_DECIM) {
    micDecim = 0;
    if (micScopeN < SCOPE_POINTS)
      micScope[micScopeN++] = (int8_t) constrain(v >> 8, -127, 127);
  }
}

// /mic ,iiib  rms, peak, sampleRate, scope
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
  uint16_t count;
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

static void routeText(OSCMessage &m) {              // /screen/text "a" "b"
  if (m.isString(0)) m.getString(0, line1, sizeof line1);
  if (m.size() > 1 && m.isString(1)) m.getString(1, line2, sizeof line2);
  redraw();
}
static void routeFill(OSCMessage &m) {              // /screen/fill r g b
  if (m.size() < 3) return;
  bgColour = tft.color565(constrain(m.getInt(0), 0, 255),
                          constrain(m.getInt(1), 0, 255),
                          constrain(m.getInt(2), 0, 255));
  redraw();
}
static void routeBacklight(OSCMessage &m) {         // /screen/backlight 0..255
  if (m.size() >= 1 && m.isInt(0)) analogWrite(TFT_LITE, constrain(m.getInt(0), 0, 255));
}
static void routeBox(OSCMessage &m) {               // /screen/box x y w h r g b
  if (m.size() < 7) return;
  tft.fillRect(m.getInt(0), m.getInt(1), m.getInt(2), m.getInt(3),
               tft.color565(constrain(m.getInt(4), 0, 255),
                            constrain(m.getInt(5), 0, 255),
                            constrain(m.getInt(6), 0, 255)));
}
static void routeTone(OSCMessage &m) {              // /tone freq [ms] [vol]
  if (m.size() < 1 || !m.isInt(0)) return;
  beep(constrain(m.getInt(0), 0, 8000),
       (m.size() > 1 && m.isInt(1)) ? constrain(m.getInt(1), 0, 3000) : 150,
       (m.size() > 2 && m.isInt(2)) ? constrain(m.getInt(2), 0, 255) : 120);
}
static void routePixels(OSCMessage &m) {            // /pixels r g b  (all five)
  if (m.size() < 3) return;
  const uint32_t c = pixels.Color(constrain(m.getInt(0), 0, 255),
                                  constrain(m.getInt(1), 0, 255),
                                  constrain(m.getInt(2), 0, 255));
  for (int i = 0; i < NEOPIX_NUM; i++) pixels.setPixelColor(i, c);
  pixels.show();
}
static void routePixel(OSCMessage &m) {             // /pixel i r g b (just one)
  if (m.size() < 4) return;
  pixels.setPixelColor(constrain(m.getInt(0), 0, NEOPIX_NUM - 1),
                       pixels.Color(constrain(m.getInt(1), 0, 255),
                                    constrain(m.getInt(2), 0, 255),
                                    constrain(m.getInt(3), 0, 255)));
  pixels.show();
}
static void routeLed(OSCMessage &m) {               // /led 0|1
  if (m.size() >= 1 && m.isInt(0)) digitalWrite(LED_BUILTIN, m.getInt(0) ? HIGH : LOW);
}
static void routeRate(OSCMessage &m) {              // /rate <ms>
  if (m.size() >= 1 && m.isInt(0)) reportEvery = constrain(m.getInt(0), 20, 5000);
}

/* -------------------------------------------------------------------- main */

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
  // Express built with this FQBN): the raw filter output was a flat 0, the
  // probe returned micOK false, and the sketch went on sending /pybadge with
  // no /mic at all. The path a mic-less PyBadge takes is therefore known good.
  // The mic-present path is NOT verified -- see the note in the header.
  if (pdmspi.begin(PDM_RATE)) {
    pdmspi.setMicGain(MIC_GAIN);
    delay(100);
    NVIC_DisableIRQ(PDM_IRQ);
    const uint16_t count = micCount;
    const int32_t  peak  = micPeak;
    micSumSq = 0; micCount = 0; micPeak = 0; micScopeN = 0; micDecim = 0;
    NVIC_EnableIRQ(PDM_IRQ);
    micOK = count > 0 && peak > 0;
    if (!micOK) NVIC_DisableIRQ(PDM_IRQ);   // no mic: stop paying for the ISR
  }
#endif

  accelOK = lis.begin(0x19) || lis.begin(0x18);
  if (accelOK) lis.setRange(LIS3DH_RANGE_4_G);

  OSCMessage hello("/hello");
  // the page uses micOK to decide whether to show the microphone panel
  hello.add("PyBadgeOscuino").add((intOSC_t) NEOPIX_NUM).add(accelOK).add(micOK);
  SLIPSerial.beginPacket(); hello.send(SLIPSerial); SLIPSerial.endPacket();
}

void loop() {
  static uint32_t last = 0;

  if (SLIPSerial.available()) {
    OSCMessage in;
    unsigned long lastByte = millis();
    while (!SLIPSerial.endofPacket()) {
      if (SLIPSerial.available()) {
        int c = SLIPSerial.read();      // int, -1 for "no byte"
        if (c >= 0) in.fill((uint8_t) c);
        lastByte = millis();
      } else if (millis() - lastByte > 200) break;
    }
    if (!in.hasError()) {
      in.dispatch("/screen/text",      routeText);
      in.dispatch("/screen/fill",      routeFill);
      in.dispatch("/screen/backlight", routeBacklight);
      in.dispatch("/screen/box",       routeBox);
      in.dispatch("/tone",             routeTone);
      in.dispatch("/pixels",           routePixels);
      in.dispatch("/pixel",            routePixel);
      in.dispatch("/led",              routeLed);
      in.dispatch("/rate",             routeRate);
    }
  }

#if BADGE_HAS_PDM_MIC
  static uint32_t lastMic = 0;
  if (micOK && millis() - lastMic >= 50) { lastMic = millis(); sendMic(); }
#endif

  const uint32_t now = millis();
  if (now - last < reportEvery) return;
  last = now;

  int16_t ax = 0, ay = 0, az = 0;
  if (accelOK) { lis.read(); ax = lis.x; ay = lis.y; az = lis.z; }

  // One message, sampled in one pass, so every reading in it belongs to the
  // same instant -- the same shape as EsploraOscuino and XiaoS3SenseOscuino.
  OSCMessage m("/pybadge");
  m.add((intOSC_t) readButtons());                     // one bitmask, eight buttons
  m.add((intOSC_t) analogRead(LIGHT_SENSOR));
  m.add((intOSC_t) analogRead(BATTERY_SENSE));         // half the battery volts
  m.add((intOSC_t) ax).add((intOSC_t) ay).add((intOSC_t) az);
  m.add(accelOK);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

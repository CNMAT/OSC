/*
 * FruitJamOscuino — Oscuino over SLIP-encoded USB serial, for the
 * Adafruit Fruit Jam (RP2350B), hand-written like the other board demos.
 * -----------------------------------------------------------------------------
 * Board : Adafruit Fruit Jam (RP2350B, 16 MB flash, 8 MB PSRAM)
 * FQBN  : rp2040:rp2040:adafruit_fruitjam        (arduino-pico >= 6.0.0)
 * Libs  : Adafruit NeoPXL8 (+NeoPixel), Adafruit TLV320 I2S (+BusIO),
 *         Adafruit DVI HSTX (+GFX) — see DVI note below
 *
 * The same board also has a CircuitPython firmware speaking the identical
 * address space: extras/python/FruitJamOscuino/. This sketch pairs with the
 * same FruitJamOscuino.html page (generated next to both by extras/webserial;
 * extras/webserial/oscuino.html is the same page for any board).
 *
 * ADDRESSES — the standard Oscuino set (/d /a /tone /s, see ADDRESSES.md)
 * plus the capabilities this board announces in its /enq bundle:
 *
 *   /btn                     -> /btn <b1> <b2> <b3>   (1 = pressed) [/enq/btn 3]
 *   /rgb <r> <g> <b>         set all 5 NeoPixels, 0..255 each      [/enq/rgb 5]
 *   /rgb/<n> <r> <g> <b>     set NeoPixel n (0..4)
 *   /buzz <freq> [<ms>]      sine through the TLV320 codec (headphone jack
 *                            and speaker); no argument or 0 stops it [/enq/buzz]
 *   /display/text <string>      print a line of text on the DVI display [/enq/display]
 *
 * The board is named nowhere in the address space — /btn, /rgb, /buzz and
 * /display mean the same on every Oscuino sketch, so one page drives them all.
 * /enq is answered on request (the boot one is usually lost to USB) and
 * lists only the capabilities that actually came up, so a bare board with
 * no display or codec degrades to a shorter list, not a lie.

 * DVI NOTE: the display runs on the "Adafruit DVI HSTX" library (dvhstx),
 * which drives the RP2350's HSTX peripheral properly and knows this board's
 * pinout (ADAFRUIT_FRUIT_JAM_CFG). Do NOT substitute PicoDVI: it is the
 * RP2040 PIO implementation, and pointed at these pins it wedges the boot so
 * hard the device never enumerates on USB (measured 2026-08-29; recovery was
 * the BOOT button — front button #1 — held through a reset).
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <Adafruit_NeoPXL8.h>
#include <Adafruit_TLV320DAC3100.h>
#include <I2S.h>
#include <Adafruit_dvhstx.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static const unsigned long BAUD = 115200;  // ignored on native USB

// ---- Fruit Jam hardware (pins from the arduino-pico variant header) --------
// NeoPXL8, not Adafruit_NeoPixel: plain show() masks ALL interrupts for the
// whole transmission (~190 us for five pixels), and the dvhstx display needs
// its scanline interrupt every 31.7 us — measured on this board, one show()
// wedged video, audio and the main loop simultaneously. NeoPXL8's show() is
// DMA-fed PIO with no masking; lanes marked -1 are skipped, so only the
// NeoPixel pin is driven.
static int8_t neopixel_lanes[8] = {PIN_NEOPIXEL, -1, -1, -1, -1, -1, -1, -1};
Adafruit_NeoPXL8 pixels(NUM_NEOPIXEL, neopixel_lanes, NEO_GRB);
Adafruit_TLV320DAC3100 codec;
I2S i2s(OUTPUT);

// Text-mode DVI terminal on the library's own Fruit Jam pin table (which
// matches the pins measured on this board via CircuitPython: CKP=13, D0P=15,
// D1P=17, D2P=19). DVHSTXText scrolls by itself.
DVHSTXText display(ADAFRUIT_FRUIT_JAM_CFG);

static bool dviOK = false;
static bool codecOK = false;

// ---- tone state: fed incrementally from loop() so serving never blocks -----
static const int SAMPLE_RATE = 44100;
static volatile uint32_t tonePhase = 0, toneStep = 0;
static unsigned long toneOffAt = 0;        // 0 = no scheduled stop

static OSCBundle bundleOUT;

// Wire-level breadcrumb, sent immediately (not queued): the instrument for
// locating a hang. Cheap enough to leave in a demo.
static void dbg(const char *s) {
  OSCMessage m("/diag");
  m.add(s);
  SLIPSerial.beginPacket();
  m.send(SLIPSerial);
  SLIPSerial.endPacket();
}

static char *numToOSCAddress(int pin) {
  static char s[10];
  int i = 9;
  s[i--] = '\0';
  do {
    s[i--] = "0123456789"[pin % 10];
    pin /= 10;
  } while (pin && i);
  s[i] = '/';
  return &s[i];
}

static void pinAddress(char *out, const char *prefix, int pin, const char *suffix) {
  strcpy(out, prefix);
  strcat(out, numToOSCAddress(pin));
  if (suffix) strcat(out, suffix);
}

// ---- standard routes, same shapes as the generated template ----------------
void routeDigital(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < (int)NUM_DIGITAL_PINS; pin++) {
    int matched = msg.match(numToOSCAddress(pin), addrOffset);
    if (!matched) continue;
    char addr[12];
    if (msg.isInt(0)) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, msg.getInt(0) > 0 ? HIGH : LOW);
    } else if (msg.isFloat(0)) {
      float v = msg.getFloat(0);
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      pinMode(pin, OUTPUT);
      analogWrite(pin, (int)(v * 255.0f + 0.5f));
    } else if (msg.fullMatch("/u", matched + addrOffset)) {
      pinMode(pin, INPUT_PULLUP);
      pinAddress(addr, "/d", pin, "/u");
      bundleOUT.add(addr).add((intOSC_t)digitalRead(pin));
    } else {
      pinMode(pin, INPUT);
      pinAddress(addr, "/d", pin, NULL);
      bundleOUT.add(addr).add((intOSC_t)digitalRead(pin));
    }
    return;
  }
}

void routeAnalog(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < (int)NUM_ANALOG_INPUTS; pin++) {
    int matched = msg.match(numToOSCAddress(pin), addrOffset);
    if (!matched) continue;
    char addr[12];
    if (msg.isInt(0)) {
      pinMode(analogInputToDigitalPin(pin), OUTPUT);
      digitalWrite(analogInputToDigitalPin(pin), msg.getInt(0) > 0 ? HIGH : LOW);
    } else if (msg.isFloat(0)) {
      float v = msg.getFloat(0);
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
      pinMode(analogInputToDigitalPin(pin), OUTPUT);
      analogWrite(analogInputToDigitalPin(pin), (int)(v * 255.0f + 0.5f));
    } else {
      pinAddress(addr, "/a", pin, NULL);
      bundleOUT.add(addr).add((intOSC_t)analogRead(pin));
    }
    return;
  }
}

void routeTone(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < (int)NUM_DIGITAL_PINS; pin++) {
    if (!msg.match(numToOSCAddress(pin), addrOffset)) continue;
    unsigned int freq = 0;
    if (msg.isInt(0))        freq = (unsigned int)msg.getInt(0);
    else if (msg.isFloat(0)) freq = (unsigned int)msg.getFloat(0);
    if (freq == 0) noTone(pin);
    else if (msg.isInt(1))   tone(pin, freq, msg.getInt(1));
    else                     tone(pin, freq);
    return;
  }
}

void routeSystem(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/m", addrOffset)) bundleOUT.add("/s/m").add((intOSC_t)micros());
  if (msg.fullMatch("/d", addrOffset)) bundleOUT.add("/s/d").add((intOSC_t)NUM_DIGITAL_PINS);
  if (msg.fullMatch("/a", addrOffset)) bundleOUT.add("/s/a").add((intOSC_t)NUM_ANALOG_INPUTS);
  if (msg.fullMatch("/l", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, v > 0 ? HIGH : LOW);
    bundleOUT.add("/s/l").add((intOSC_t)v);
  }
}

// ---- capability routes -------------------------------------------------------
static void addEnq() {
  bundleOUT.add("/enq").add("FruitJamOscuino");
  bundleOUT.add("/enq/btn").add((intOSC_t)3);
  bundleOUT.add("/enq/rgb").add((intOSC_t)NUM_NEOPIXEL);
  if (codecOK) bundleOUT.add("/enq/buzz");
  if (dviOK)   bundleOUT.add("/enq/display").add((intOSC_t)display.width())
                                        .add((intOSC_t)display.height());
}

void routeEnq(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addEnq();
}

void routeBtn(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  // Buttons pull to ground; report 1 = pressed.
  bundleOUT.add("/btn")
      .add((intOSC_t)(digitalRead(PIN_BUTTON1) == LOW))
      .add((intOSC_t)(digitalRead(PIN_BUTTON2) == LOW))
      .add((intOSC_t)(digitalRead(PIN_BUTTON3) == LOW));
}

void routeRgb(OSCMessage &msg, int addrOffset) {
  if (!(msg.isInt(0) && msg.isInt(1) && msg.isInt(2))) return;
  // /rgb/<n> addresses one pixel, matched the same way /d/<pin> is.
  for (int n = 0; n < (int)NUM_NEOPIXEL; n++) {
    if (!msg.match(numToOSCAddress(n), addrOffset)) continue;
    pixels.setPixelColor(n, msg.getInt(0), msg.getInt(1), msg.getInt(2));
    pixels.show();
    char addr[12];
    pinAddress(addr, "/rgb", n, NULL);
    bundleOUT.add(addr).add((intOSC_t)msg.getInt(0))
        .add((intOSC_t)msg.getInt(1)).add((intOSC_t)msg.getInt(2));
    return;
  }
  for (unsigned n = 0; n < NUM_NEOPIXEL; n++)
    pixels.setPixelColor(n, msg.getInt(0), msg.getInt(1), msg.getInt(2));
  pixels.show();
  // Echo the values back: probes can't see photons.
  bundleOUT.add("/rgb").add((intOSC_t)msg.getInt(0))
      .add((intOSC_t)msg.getInt(1)).add((intOSC_t)msg.getInt(2));
}

void routeBuzz(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  unsigned int freq = 0;
  if (msg.isInt(0))        freq = (unsigned int)msg.getInt(0);
  else if (msg.isFloat(0)) freq = (unsigned int)msg.getFloat(0);
  if (freq == 0 || !codecOK) {
    toneStep = 0;
    toneOffAt = 0;
  } else {
    toneStep = (uint32_t)(((uint64_t)freq << 32) / SAMPLE_RATE);
    toneOffAt = msg.isInt(1) ? millis() + msg.getInt(1) : 0;
  }
  bundleOUT.add("/buzz").add((intOSC_t)(codecOK ? freq : -1));
}

void routeDisplay(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/text", addrOffset) && msg.isString(0)) {
    char text[96];
    msg.getString(0, text, sizeof(text));
    if (dviOK) display.println(text);
    bundleOUT.add("/display/text").add((intOSC_t)(dviOK ? 1 : 0));
  }
}

// ---- codec bring-up: the Adafruit basicI2Sconfig recipe, PLL from BCLK -----
static bool codecInit() {
  pinMode(PIN_PERIPHERAL_RESET, OUTPUT);
  digitalWrite(PIN_PERIPHERAL_RESET, LOW);
  delay(20);
  digitalWrite(PIN_PERIPHERAL_RESET, HIGH);
  delay(20);

  if (!codec.begin()) return false;
  bool ok = codec.setCodecInterface(TLV320DAC3100_FORMAT_I2S, TLV320DAC3100_DATA_LEN_16)
    && codec.setCodecClockInput(TLV320DAC3100_CODEC_CLKIN_PLL)
    && codec.setPLLClockInput(TLV320DAC3100_PLL_CLKIN_BCLK)
    && codec.setPLLValues(1, 2, 32, 0)
    && codec.setNDAC(true, 8)
    && codec.setMDAC(true, 2)
    && codec.powerPLL(true)
    && codec.setDACDataPath(true, true, TLV320_DAC_PATH_NORMAL,
                            TLV320_DAC_PATH_NORMAL, TLV320_VOLUME_STEP_1SAMPLE)
    && codec.configureAnalogInputs(TLV320_DAC_ROUTE_MIXER, TLV320_DAC_ROUTE_MIXER,
                                   false, false, false, false)
    && codec.setDACVolumeControl(false, false, TLV320_VOL_INDEPENDENT)
    && codec.setChannelVolume(false, 18)
    && codec.setChannelVolume(true, 18)
    && codec.configureHeadphoneDriver(true, true, TLV320_HP_COMMON_1_35V, false)
    && codec.configureHPL_PGA(0, true)
    && codec.configureHPR_PGA(0, true)
    && codec.setHPLVolume(true, 6)
    && codec.setHPRVolume(true, 6)
    && codec.enableSpeaker(true)
    && codec.configureSPK_PGA(TLV320_SPK_GAIN_6DB, true)
    && codec.setSPKVolume(true, 0);
  return ok;
}

// -----------------------------------------------------------------------------

void setup() {
  SLIPSerial.begin(BAUD);

  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);

  // Announce before any risky peripheral bring-up, so a wedge downstream is
  // distinguishable from a dead port: /enq arrives, then silence.
  delay(300);
  bundleOUT.add("/enq").add("FruitJamOscuino");   // full list: ask /enq
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();

  // DVI before I2S: dvhstx reconfigures the system PLL for the pixel clock,
  // and the I2S dividers must be computed against the final clock.
  dbg("pre-dvi-begin");
  dviOK = display.begin();
  dbg(dviOK ? "dvi-ok" : "dvi-failed");
  if (dviOK) {
    display.println("FruitJamOscuino");
    display.println("OSC over SLIP over USB serial");
    dbg("banner-printed");
  }

  // NeoPXL8 after the display: its PIO clock divider is computed from the
  // system clock dvhstx just changed.
  bool npxOK = pixels.begin();
  dbg(npxOK ? "neopxl8-ok" : "neopxl8-failed");
  pixels.show();

  i2s.setBCLK(PIN_I2S_BITCLK);            // WORDSEL is BCLK+1 on this board
  i2s.setDATA(PIN_I2S_DATAOUT);
  i2s.setBitsPerSample(16);
  bool i2sOK = i2s.begin(SAMPLE_RATE);
  dbg(i2sOK ? "i2s-ok" : "i2s-failed");
  codecOK = i2sOK && codecInit();
  dbg(codecOK ? "codec-ok" : "codec-failed");
}

static bool pollOSC(OSCBundle &bundleIN) {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t)c);
    }
  }
  return true;
}

static OSCBundle bundleIN;

void loop() {
  if (pollOSC(bundleIN)) {
    if (!bundleIN.hasError()) {
      bundleIN.route("/d", routeDigital);
      bundleIN.route("/a", routeAnalog);
      bundleIN.route("/tone", routeTone);
      bundleIN.route("/s", routeSystem);
      bundleIN.route("/enq", routeEnq);
      bundleIN.route("/btn",  routeBtn);
      bundleIN.route("/rgb",  routeRgb);
      bundleIN.route("/buzz", routeBuzz);
      bundleIN.route("/display", routeDisplay);
    }
    bundleIN.empty();
  }

  if (bundleOUT.size() > 0) {
    SLIPSerial.beginPacket();
    bundleOUT.send(SLIPSerial);
    SLIPSerial.endPacket();
    bundleOUT.empty();
  }

  // Feed the codec a sine without ever blocking the OSC loop.
  if (toneStep) {
    if (toneOffAt && millis() >= toneOffAt) {
      toneStep = 0;
      toneOffAt = 0;
    } else {
      for (int n = 0; n < 64 && i2s.availableForWrite() >= 4; n++) {
        int16_t s = (int16_t)(12000.0f * sinf(tonePhase * (6.2831853f / 4294967296.0f)));
        i2s.write16(s, s);
        tonePhase += toneStep;
      }
    }
  }
}

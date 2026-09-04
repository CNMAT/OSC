/*
 * XiaoNrf52Oscuino — Oscuino over TWO transports at once: SLIP-encoded USB
 * serial, and SLIP-encoded OSC over Bluetooth LE. For the Seeed XIAO
 * nRF52840 Sense.
 * -----------------------------------------------------------------------------
 * Board : Seeed XIAO nRF52840 Sense (nRF52840, VID 0x2886 PID 0x8045)
 * FQBN  : Seeeduino:nrf52:xiaonRF52840Sense
 * Libs  : Seeed_Arduino_LSM6DS3 (GitHub ZIP, the library Seeed's wiki names
 *         for this board's IMU); BLE comes from the core's own Bluefruit52Lib.
 * Page  : XiaoNrf52Oscuino.html, generated beside this sketch by extras/webserial;
 *         pick Web Bluetooth or Web Serial there (extras/webserial/oscuino.html
 *         is the same page for any board)
 *
 * THE POINT OF THIS EXAMPLE: the library's SLIP transport is a template,
 * `_SLIPSerial<T>`, and it was written for HardwareSerial and for TCP
 * Client. Bluefruit's BLEUart is an Arduino Stream with the same
 * write/available/peek/read/flush surface, so
 *
 *     _SLIPSerial<BLEUart> SLIPBle(bleuart);
 *
 * gives OSC over BLE with NO changes to the library at all. The bytes on
 * the air are the same SLIP-framed OSC 1.0 packets this repo puts on a
 * wire; only the pipe is different. A BLE central that speaks the Nordic
 * UART Service (NUS) — including Chrome's Web Bluetooth — is a client.
 *
 * Both transports run simultaneously and share one handler, and a reply
 * goes back to whichever transport asked, so a USB client and a phone can
 * talk to the board at the same time without configuring anything.
 *
 * ADDRESSES — the standard Oscuino set (/d /a /tone /s, see ADDRESSES.md)
 * plus the capabilities this board announces in its /enq bundle:
 *
 *   /rgb <r> <g> <b>      the RGB LED, 0..255 each (PWM)      [/enq/rgb 1]
 *   /imu                  -> /imu <ax> <ay> <az> <gx> <gy> <gz>
 *                            floats: g, then deg/s             [/enq/imu 6]
 *   /mic                  -> /mic <rms> <peak>, full scale     [/enq/mic]
 *   /mic/gain <n>         PDM gain 0..80 (40 = unity, 0.5 dB a step)
 *   /bat                  -> /bat <millivolts>                 [/enq/bat]
 *   /chg [<50|100>]       -> /chg <charging> <mA>              [/enq/chg]
 *   /rate <ms>            stream /state + /imu + /mic every <ms>; 0 stops
 *
 * Nothing here is named after the board: the same addresses mean the same
 * things on every Oscuino sketch, which is what lets one page drive all of
 * them.
 *
 * /enq answers with the capability bundle; imuOK and micOK decide whether
 * /enq/imu and /enq/mic appear in it, imuOK from the driver's own begin()
 * status and micOK from a signal probe.
 *
 * TWO THINGS THE DOCUMENTATION SETTLED that guessing got wrong. First, the
 * Sense's LSM6DS3TR-C is not on the `Wire` exposed at D4/D5 — Seeed's driver
 * remaps `Wire` to `Wire1` for this board, and it does so behind
 * TARGET_SEEED_XIAO_NRF52840_SENSE, which (measured with a #pragma probe)
 * the *Bluetooth* core defines too. That matters because the wiki steers IMU
 * users to the mbed core, implying a choice between BLE and the IMU; there
 * is none, and this sketch uses both at once. Second, the IMU's power pin is
 * not an ordinary output: the driver reconfigures it as a high-drive pin
 * (NRF_P1->PIN_CNF[8], H0H1) before releasing the rail. A hand-rolled
 * pinMode/digitalWrite plus direct register reads found no IMU at all here.
 *
 * STATUS — VERIFIED END TO END, 2026-08-30. Over USB: /enq, /imu
 * (physically sane — 0.99 g on Z lying flat, gyro ~0 at rest), /rgb,
 * /bat and the standard set. Over the air, from Chrome via the companion
 * page: advertising, the NUS connection, writes into the board, notifications
 * back out, SLIP frames spanning several notifications, and /imu streaming
 * at 50 ms with the readouts tracking the board as it is tilted. (The BLE
 * half had to be checked from a browser: macOS denies CoreBluetooth to a
 * command-line process — a bleak scan aborts with SIGABRT and no prompt is
 * ever offered — so Chrome, which holds the permission, is the instrument.)
 *
 * MEASURED on this board (2026-08-30): transport ladder all clean over USB
 * — echo 22/22, widths 11/11, probe 7/7, full bench including compound ×3
 * and the 1100-byte lazy-reader ring. The nRF52840's TinyUSB stack behaves
 * like the RP2040 TinyUSB family: NAK, no drops.
 *
 * BUILD NOTE (macOS): the Seeed nRF52 platform's packaging step shells out
 * to `python`, which no longer exists on a modern macOS that ships only
 * `python3`. The compile succeeds and the upload then fails looking for a
 * .zip that was never built. Put a `python` -> `python3` shim on PATH.
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <Wire.h>
#include <bluefruit.h>
#include <LSM6DS3.h>
#include <PDM.h>
#include "transports.h"

// ---- transports ------------------------------------------------------------
#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

BLEUart bleuart;
_SLIPSerial<BLEUart> SLIPBle(bleuart);      // the whole BLE transport

static const unsigned long BAUD = 115200;   // ignored on native USB

// ---- board hardware --------------------------------------------------------
// Address 0x6A per Seeed's wiki example; the driver picks the bus.
LSM6DS3 myIMU(I2C_MODE, 0x6A);

// BQ25101 charge status, ~CHG. variant.h names only HICHG
// (PIN_CHARGING_CURRENT, D22); the status line is D23 in the variant's pin
// map ("D23 is P0.17 (~CHG)").
static const int PIN_CHARGE_STATUS = 23;

static int chargeMilliamps = -1;            // -1 = untouched, board default
static bool imuOK = false;
static uint32_t imuRateMs = 0;              // 0 = not streaming
static int32_t  seq = 0;

static OSCBundle bundleOUT;

static void addIMU() {
  bundleOUT.add("/imu")
      .add(myIMU.readFloatAccelX()).add(myIMU.readFloatAccelY())
      .add(myIMU.readFloatAccelZ())
      .add(myIMU.readFloatGyroX()).add(myIMU.readFloatGyroY())
      .add(myIMU.readFloatGyroZ());
}

// ---- PDM microphone --------------------------------------------------------
// The core ships its own PDM library and its global instance is built from
// this variant's PIN_PDM_DIN/CLK/PWR, so it powers and wires the mic itself —
// no core switch, despite the wiki steering PDM users to the mbed core.
#define MIC_RATE 16000

// The library's DEFAULT_PDM_GAIN is 20, and on the nRF52 PDM 40 (0x28) is
// unity with 0.5 dB per step — so the stock setting runs the microphone 10 dB
// BELOW unity, which is why it barely reacts to a room. Measured here by
// sweeping the gain and watching the quiet-room noise floor, which is an
// instrument check that needs no sound source:
//
//     gain 20 -> rms  6      gain 60 -> rms  62  (+20.3 dB)
//     gain 40 -> rms 29.5    gain 80 -> rms 167  (+28.9 dB)
//
// against +10 / +20 / +30 dB predicted by the 0.5 dB step, so the law holds.
// 50 is chosen as an audible working point, NOT a calibrated one: finding the
// clip point needs a known loud source, which this bench did not have. Change
// it live with /mic/gain.
#define MIC_GAIN 50
static short micBuf[256];
static volatile int micSamples = 0;
static bool micOK = false;

static void onPDMdata() {
  int bytes = PDM.available();
  if (bytes > (int)sizeof micBuf) bytes = sizeof micBuf;
  PDM.read(micBuf, bytes);
  micSamples = bytes / 2;
}

// RMS and peak over the latest block, at full scale. Squares accumulate in
// uint64 and the divide is float: pre-shifting or integer division silently
// zeroes a quiet room, which is a lesson this repo already paid for once.
static void micLevel(uint16_t *rms, uint16_t *peak) {
  const int n = micSamples;
  micSamples = 0;
  if (n <= 0) { *rms = 0; *peak = 0; return; }
  uint64_t sumsq = 0;
  int32_t pk = 0;
  for (int i = 0; i < n; i++) {
    int32_t v = micBuf[i];
    sumsq += (uint64_t)(v * v);
    if (v < 0) v = -v;
    if (v > pk) pk = v;
  }
  *rms = (uint16_t)sqrt((double)sumsq / (double)n);
  *peak = (uint16_t)pk;
}

// Probe by SIGNAL, not by begin(): the PDM peripheral exists whether or not a
// microphone is attached to it, so require samples to arrive AND to vary.
static bool micProbe() {
  uint32_t t0 = millis();
  int32_t lo = 32767, hi = -32768;
  int got = 0;
  while (millis() - t0 < 200) {
    int n = micSamples;
    if (n > 0) {
      for (int i = 0; i < n; i++) {
        int32_t v = micBuf[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      got += n;
      micSamples = 0;
    }
  }
  return got > 0 && (hi - lo) > 1;
}

// ---- addressing helpers (same shapes as the generated template) ------------
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

// ---- standard routes -------------------------------------------------------
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
#ifdef BOARD_HAS_LED
  if (msg.fullMatch("/l", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(LED_BUILTIN, OUTPUT);
    // LED_STATE_ON comes from the variant rather than folklore about which
    // way round these LEDs are wired.
    digitalWrite(LED_BUILTIN, v > 0 ? LED_STATE_ON : !LED_STATE_ON);
    bundleOUT.add("/s/l").add((intOSC_t)v);
  }
#endif
}

// ---- capability routes -------------------------------------------------------
// Each is routed by its own root (/rgb, /imu, ...) so the addresses are the
// same on every board that has the capability. See ADDRESSES.md.

static void addEnq() {
  bundleOUT.add("/enq").add("XiaoNrf52Oscuino");
  bundleOUT.add("/enq/rgb").add((intOSC_t)1);
  if (imuOK) bundleOUT.add("/enq/imu").add((intOSC_t)6);
  if (micOK) bundleOUT.add("/enq/mic");
  bundleOUT.add("/enq/bat");
  bundleOUT.add("/enq/chg");
}

void routeEnq(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addEnq();
}

void routeRgb(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (!(msg.isInt(0) && msg.isInt(1) && msg.isInt(2))) return;
  const int pins[3] = {LED_RED, LED_GREEN, LED_BLUE};
  for (int i = 0; i < 3; i++) {
    int v = msg.getInt(i);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    pinMode(pins[i], OUTPUT);
    // ACTIVE LOW, measured: sending 255/255/255 goes dark and 0/0/0 lights
    // white. The variant's LED_STATE_ON is 1, which reads like active high
    // and is why the first build produced confidently wrong colours — every
    // channel was inverted, so "red" arrived as cyan.
    analogWrite(pins[i], 255 - v);
  }
  // Echo: probes cannot see photons.
  bundleOUT.add("/rgb")
      .add((intOSC_t)msg.getInt(0)).add((intOSC_t)msg.getInt(1))
      .add((intOSC_t)msg.getInt(2));
}

void routeImu(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  // Absence is silence (ADDRESSES.md): with no IMU there is no /enq/imu in
  // the enq bundle and this answers nothing, rather than inventing a -1
  // that claims to be a float.
  if (imuOK) addIMU();
}

void routeRate(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (!msg.isInt(0)) return;
  int r = msg.getInt(0);
  imuRateMs = (r <= 0) ? 0 : constrain(r, 20, 2000);
  bundleOUT.add("/rate").add((intOSC_t)imuRateMs);
}

void routeMic(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/gain", addrOffset) && msg.isInt(0)) {
    int g = constrain(msg.getInt(0), 0, 80);
    PDM.setGain(g);
    bundleOUT.add("/mic/gain").add((intOSC_t)g);
    return;
  }
  if (!micOK) return;                       // absence is silence, as above
  uint16_t rms, peak;
  micLevel(&rms, &peak);
  bundleOUT.add("/mic").add((intOSC_t)rms).add((intOSC_t)peak);
}

void routeChg(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (msg.isInt(0)) {
    chargeMilliamps = (msg.getInt(0) >= 100) ? 100 : 50;
    pinMode(PIN_CHARGING_CURRENT, OUTPUT);
    // HIGH is the 50 mA setting, LOW the 100 mA one — inverted-feeling, and
    // straight from the wiki rather than from guessing at the name.
    digitalWrite(PIN_CHARGING_CURRENT, chargeMilliamps == 100 ? LOW : HIGH);
  }
  pinMode(PIN_CHARGE_STATUS, INPUT_PULLUP);   // ~CHG is open drain
  bundleOUT.add("/chg")
      .add((intOSC_t)(digitalRead(PIN_CHARGE_STATUS) == LOW ? 1 : 0))
      .add((intOSC_t)chargeMilliamps);
}

void routeBat(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  // VBAT_ENABLE must be driven LOW to connect the divider (variant.h).
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  delay(2);
  int raw = analogRead(PIN_VBAT);
  // 10-bit default reference 3.6 V, and the board halves VBAT: 2 * 3600/1024.
  bundleOUT.add("/bat").add((intOSC_t)((raw * 3600L * 2) / 1024));
}

// -----------------------------------------------------------------------------

static void dispatchAll(OSCBundle &b) {
  if (b.hasError()) return;
  b.route("/d", routeDigital);
  b.route("/a", routeAnalog);
  b.route("/tone", routeTone);
  b.route("/s", routeSystem);
  b.route("/enq", routeEnq);
  b.route("/rgb",   routeRgb);
  b.route("/imu",   routeImu);
  b.route("/mic",   routeMic);
  b.route("/chg",   routeChg);
  b.route("/bat",   routeBat);
  b.route("/rate",  routeRate);
}

void setup() {
  SLIPSerial.begin(BAUD);

  // IMU: the driver powers the rail (high-drive) and picks Wire1 itself.
  // Its own defaults are 104 Hz, +/-2 g and 2000 dps (LSM6DS3.h settings).
  imuOK = (myIMU.begin() == 0);

  // Microphone. onReceive() before begin(), as the library's own example does.
  PDM.onReceive(onPDMdata);
  micOK = PDM.begin(1, MIC_RATE);
  if (micOK) PDM.setGain(MIC_GAIN);       // after begin(): begin() sets its own
  micOK = micOK && micProbe();

  // BLE: the Nordic UART Service, which is what SLIPBle rides on.
  //
  // configPrphConn BEFORE begin(), and this is not optional. BLEUart sizes
  // its TXD characteristic with setMaxLen(Bluefruit.getMaxMtu()), which
  // defaults to BLE_GATT_ATT_MTU_DEFAULT = 23 — and BLECharacteristic::notify
  // then does `min16(len, _max_len)`, so it TRUNCATES rather than fails. An
  // OSC bundle is ~76 bytes, so every reply left the board cut to 23 bytes:
  // writes into the board worked, nothing legible ever came back, and no
  // error was reported anywhere. Measured against the companion page.
  // 247 is BLE_GATT_ATT_MTU_MAX; the deeper HVN queue covers the several
  // notifications a large packet still costs when a central negotiates a
  // small MTU anyway.
  Bluefruit.configPrphConn(BLE_GATT_ATT_MTU_MAX, BLE_GAP_EVENT_LENGTH_DEFAULT, 16, 16);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("XiaoNrf52Oscuino");
  bleuart.begin();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);           // 0 = advertise forever

  delay(300);
  addEnq();
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

static OSCBundle bundleINusb, bundleINble;

void loop() {
  // USB in, USB out.
  if (pollOSC(SLIPSerial, bundleINusb)) {
    dispatchAll(bundleINusb);
    bundleINusb.empty();
    flushTo(SLIPSerial, bundleOUT);
  }

  // BLE in, BLE out — the reply follows whoever asked.
  if (Bluefruit.connected() && pollOSC(SLIPBle, bundleINble)) {
    dispatchAll(bundleINble);
    bundleINble.empty();
    flushTo(SLIPBle, bundleOUT);
  }

  // Motion stream: BLE when a central is listening, USB otherwise.
  static uint32_t lastImu = 0;
  if (imuRateMs && imuOK && millis() - lastImu >= imuRateMs) {
    lastImu = millis();
    bundleOUT.add("/state").add((intOSC_t)seq++).add((intOSC_t)millis());
    addIMU();
    if (micOK) {
      uint16_t rms, peak;
      micLevel(&rms, &peak);
      bundleOUT.add("/mic").add((intOSC_t)rms).add((intOSC_t)peak);
    }
    if (Bluefruit.connected()) flushTo(SLIPBle, bundleOUT);
    else                       flushTo(SLIPSerial, bundleOUT);
  }
}

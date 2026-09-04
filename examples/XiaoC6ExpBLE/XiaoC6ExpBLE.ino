/*
 * XiaoC6ExpBLE — the third twin: the same OSC vocabulary as
 * XiaoC6ExpOscuino (USB serial) and XiaoC6ExpWiFi (UDP + HTTP), now over
 * Bluetooth LE. Seeed XIAO ESP32-C6 on the XIAO Expansion Board.
 * -----------------------------------------------------------------------------
 * Board : Seeed XIAO ESP32-C6 on the XIAO Expansion Board
 * FQBN  : esp32:esp32:XIAO_ESP32C6      (stock defaults; this variant sets
 *         cdc_on_boot itself — do NOT carry another ESP32's CDCOnBoot here)
 * Libs  : Adafruit SSD1306 + GFX. BLE comes from the esp32 core.
 *
 *                    XiaoC6ExpOscuino   XiaoC6ExpWiFi      XiaoC6ExpBLE
 *   transport        USB CDC            802.11             Bluetooth LE
 *   framing          SLIP               none (UDP frames)  SLIP again
 *   who may talk     one host           anything routable  one central
 *   browser reaches  Web Serial         fetch/HTTP         Web Bluetooth
 *
 * SLIP returns here because BLE, like a serial port, is a stream of bytes
 * with no packet boundaries a receiver can trust: a notification is not a
 * packet, and a long reply spans several of them.
 *
 * THE POINT: _SLIPSerial<T> is a template over anything Stream-shaped. On
 * the nRF52840 (examples/XiaoNrf52Oscuino) Bluefruit's BLEUart already was a
 * Stream, so BLE cost that sketch nothing. The ESP32 BLE library has no such
 * class, so ble_stream.h supplies one in ~60 lines of buffering, and the
 * transport line here is identical to the nRF52840's:
 *
 *     _SLIPSerial<BLEStream> SLIPBle(bleStream);
 *
 * Two chip families, two unrelated BLE stacks, one transport abstraction and
 * one set of OSC bytes. The service is the Nordic UART Service in both cases,
 * so one Web Bluetooth page drives both boards: XiaoC6ExpBLE.html, generated
 * beside this sketch by extras/webserial (extras/webserial/oscuino.html is
 * the same page for any board).
 *
 * ADDRESSES — mirroring the serial and WiFi twins:
 *   /display/text <s> [...]  up to 4 lines on the OLED
 *   /display/clear           blank it
 *   /buzz <freq> [<ms>]   the expansion board's buzzer; 0 stops
 *   /s/l <int>            the XIAO's user LED (active LOW on this module)
 *   /rate <ms>            state period, 20..2000
 * State, streamed at that rate and on request, as a bundle (ADDRESSES.md):
 *   /state <seq> <millis>  +  /btn <pressed>
 *
 * STATUS — BLE VERIFIED OVER THE AIR, 2026-09-04, from Chrome via
 * XiaoC6ExpBLE.html, on a bare XIAO ESP32-C6 (MAC 10:bd:a3:9f:ef:ac):
 * advertising and the device picker, the connection, the /enq greeting
 * (name plus /enq/btn 1 and /enq/buzz, and correctly NO /enq/display on a
 * board with no OLED), central-to-board writes with echoes (/s/l 1 and 0
 * visibly toggling the LED, /buzz 523 200 audible), and a clean notification
 * stream: 12 consecutive /state bundles, sequence 2208..2219 with no gaps,
 * board-side millis exactly 100 ms apart at the default rate, arriving
 * 89-106 ms apart. /rate 0 was echoed and the stream stopped dead.
 * Over USB in the same build: the greeting, /s/l, /buzz, /btn and /rate.
 *
 * THE NOTIFICATION BUG THIS FOUND. The run before that one showed corrupt
 * bundles on the air while USB stayed byte-perfect: some frames truncated,
 * others with foreign bytes spliced between the bundle timetag and its first
 * element, and /btn arriving ahead of the /state added before it. The cause
 * was in ble_stream.h, not in OSC or SLIP -- it chunked every bundle into
 * 20-byte notifications 3 ms apart, far inside the connection interval, so
 * setValue() kept overwriting payloads the radio had not sent yet. One chunk
 * was lost, another went out twice, which is exactly what reordering and
 * spliced bytes look like. The adapter now uses the MTU the central actually
 * granted and sends the whole bundle in one notification. See the note there.
 *
 * NOT verified: the peripheral lines. This C6 was bare, so displayOK came
 * back false. Display addresses are now silent in that case rather than
 * answering "/display/clear 1" for a screen that is not there (fixed the same
 * day, after the reply was seen on the air). Put the board on the XIAO
 * Expansion Board to exercise the OLED, buzzer and button for real.
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "ble_stream.h"
#include "transports.h"

// Nordic UART Service — the same UUIDs the nRF52840 twin advertises, so one
// Web Bluetooth page serves both boards.
#define NUS_SERVICE "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   // central -> board
#define NUS_TX      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   // board -> central

#define OLED_W    128
#define OLED_H     64
#define OLED_ADDR 0x3C          // scanned on the real bus by the serial twin
#define PIN_BUZZ  D3            // GPIO21
#define PIN_BTN   D1            // GPIO1, active LOW

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

BLEStream bleStream;
_SLIPSerial<BLEStream> SLIPBle(bleStream);      // the whole BLE transport

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static bool     displayOK = false;
static int32_t  seq       = 0;
static uint32_t reportMs  = 100;
static uint32_t buzzUntil = 0;
static char     lines[4][22] = { "XIAO C6", "OSC over BLE", "", "" };

static OSCBundle bundleOUT;

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void redraw() {
  if (!displayOK) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  for (uint8_t i = 0; i < 4; i++) {
    oled.setCursor(0, (int16_t)(i * 10));
    oled.print(lines[i]);
  }
  oled.setCursor(0, 52);
  oled.print(bleStream.connected() ? "BLE connected" : "advertising...");
  oled.display();
}

// ---- routes ----------------------------------------------------------------
void routeDisplay(OSCMessage &msg, int addrOffset) {
  // Absence is silence (ADDRESSES.md). With no OLED on the bus this board
  // announces no /enq/display, so it must not answer display commands either
  // -- it used to reply "/display/clear 1", claiming to have cleared a screen
  // that is not there. Caught over BLE on a bare C6, 2026-09-04.
  if (!displayOK) return;
  if (msg.fullMatch("/text", addrOffset)) {
    for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
    const int n = msg.size() < 4 ? msg.size() : 4;
    for (int i = 0; i < n; i++)
      if (msg.isString(i)) msg.getString(i, lines[i], sizeof lines[i]);
    redraw();
    bundleOUT.add("/display/text").add((intOSC_t)n);
    return;
  }
  if (msg.fullMatch("/clear", addrOffset)) {
    for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
    redraw();
    bundleOUT.add("/display/clear").add((intOSC_t)1);
    return;
  }
}

void routeBuzz(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (!msg.isInt(0)) return;
  const int32_t freq = msg.getInt(0);
  const int32_t ms   = msg.isInt(1) ? msg.getInt(1) : 150;
  if (freq <= 0) { noTone(PIN_BUZZ); buzzUntil = 0; }
  else {
    tone(PIN_BUZZ, (unsigned int)freq);
    buzzUntil = millis() + (uint32_t)constrain(ms, 10, 5000);
  }
  bundleOUT.add("/buzz").add((intOSC_t)freq);
}

void routeLed(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (!msg.isInt(0)) return;
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, msg.getInt(0) ? LOW : HIGH);   // active LOW here
  bundleOUT.add("/s/l").add((intOSC_t)msg.getInt(0));
}

void routeRate(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  // 0 STOPS the stream (ADDRESSES.md); constrain(v, 20, 2000) turned it into
  // 20 and streamed faster instead. Measured on the serial twin, 2026-09-04.
  if (msg.isInt(0)) {
    const int32_t v = msg.getInt(0);
    reportMs = (v <= 0) ? 0 : (uint32_t) constrain(v, 20, 2000);
  }
  bundleOUT.add("/rate").add((intOSC_t)reportMs);
}

static void addBtn() {
  bundleOUT.add("/btn").add((intOSC_t)(digitalRead(PIN_BTN) == LOW ? 1 : 0));
}

static void addState() {
  bundleOUT.add("/state").add((intOSC_t)seq).add((intOSC_t)millis());
  addBtn();
}

static void addEnq() {
  bundleOUT.add("/enq").add("XiaoC6ExpBLE");
  bundleOUT.add("/enq/btn").add((intOSC_t)1);
  bundleOUT.add("/enq/buzz");
  if (displayOK) bundleOUT.add("/enq/display").add((intOSC_t)OLED_W).add((intOSC_t)OLED_H);
}

void routeEnq(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addEnq();
}

void routeState(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addState();
}

// /enq/btn in the enq bundle is a promise that this works, not just that
// the button rides along in the stream (ADDRESSES.md).
void routeBtn(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addBtn();
}

static void dispatchAll(OSCBundle &b) {
  if (b.hasError()) return;
  b.route("/display", routeDisplay);
  b.route("/buzz", routeBuzz);
  b.route("/s/l",  routeLed);
  b.route("/rate", routeRate);
  b.route("/state", routeState);
  b.route("/btn",   routeBtn);
  b.route("/enq", routeEnq);
}

// ---- BLE plumbing ----------------------------------------------------------
class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    // Hand the server over so BLEStream can ask what MTU the central actually
    // granted, and send a whole bundle in one notification instead of racing
    // 20-byte chunks against the connection interval. getPeerMTU/getConnId are
    // declared outside this core's Bluedroid/NimBLE #ifdefs, so this works
    // whichever stack the variant builds -- the C6 here builds NimBLE, and the
    // Bluedroid-only onConnect(BLEServer*, esp_ble_gatts_cb_param_t*) overload
    // does not even compile for it.
    bleStream.setPeer(s);
    bleStream.setConnected(true);
    redraw();
  }
  void onDisconnect(BLEServer *s) override {
    bleStream.setConnected(false);
    redraw();
    s->getAdvertising()->start();          // or nothing can ever reconnect
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    bleStream.feed((const uint8_t *)v.c_str(), v.length());
  }
};

void setup() {
  SLIPSerial.begin(115200);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);         // off (active LOW)

  Wire.begin();
  displayOK = i2cPresent(OLED_ADDR) && oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (displayOK) { oled.setTextColor(SSD1306_WHITE); oled.cp437(true); redraw(); }

  BLEDevice::init("XiaoC6ExpBLE");
  // Ask for a large MTU. The nRF52840 twin's silent-truncation bug came from
  // a 23-byte default; the adapter chunks conservatively anyway, so this is
  // throughput rather than correctness.
  BLEDevice::setMTU(247);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  BLEService *svc = server->createService(NUS_SERVICE);
  BLECharacteristic *tx = svc->createCharacteristic(
      NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  tx->addDescriptor(new BLE2902());
  BLECharacteristic *rx = svc->createCharacteristic(
      NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCB());
  svc->start();
  bleStream.attach(tx);

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  delay(300);
  addEnq();
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
  redraw();
}

static OSCBundle bundleINusb, bundleINble;

void loop() {
  if (pollOSC(SLIPSerial, bundleINusb)) {
    dispatchAll(bundleINusb);
    bundleINusb.empty();
    flushTo(SLIPSerial, bundleOUT);
  }

  if (bleStream.connected() && pollOSC(SLIPBle, bundleINble)) {
    dispatchAll(bundleINble);
    bundleINble.empty();
    flushTo(SLIPBle, bundleOUT);
  }

  if (buzzUntil && millis() >= buzzUntil) { noTone(PIN_BUZZ); buzzUntil = 0; }

  static uint32_t lastReport = 0;
  if (reportMs != 0 && millis() - lastReport >= reportMs) {
    lastReport = millis();
    seq++;
    if (bleStream.connected()) {
      addState();
      flushTo(SLIPBle, bundleOUT);
    }
  }
}

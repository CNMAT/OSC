/*
 * XiaoC3BLE — the third twin for the Seeed XIAO ESP32-C3: the same OSC
 * vocabulary as XiaoC3Oscuino (USB serial) and XiaoC3WiFi (UDP + HTTP), now
 * over Bluetooth LE.
 * -----------------------------------------------------------------------------
 * Board : Seeed XIAO ESP32-C3, bare (no expansion board)
 * FQBN  : esp32:esp32:XIAO_ESP32C3      (stock defaults; this variant sets
 *         cdc_on_boot itself — do NOT carry another ESP32's CDCOnBoot here)
 * Libs  : none beyond the esp32 core's own BLE
 * Page  : XiaoC3BLE.html, generated beside this sketch by extras/webserial
 *         (extras/webserial/oscuino.html is the same page for any board)
 *
 *                    XiaoC3Oscuino     XiaoC3WiFi         XiaoC3BLE
 *   transport        USB CDC           802.11             Bluetooth LE
 *   framing          SLIP              none (UDP frames)  SLIP again
 *   who may talk     one host          anything routable  one central
 *   browser reaches  Web Serial        fetch/HTTP         Web Bluetooth
 *
 * SLIP returns here because BLE, like a serial port, is a stream of bytes with
 * no packet boundaries a receiver can trust: a notification is not a packet,
 * and a reply longer than one can span several.
 *
 * THE POINT: _SLIPSerial<T> is a template over anything Stream-shaped, so the
 * transport line is one declaration — `_SLIPSerial<BLEStream> SLIPBle(...)` —
 * and the OSC code above it is the same code that runs over USB and WiFi.
 * ble_stream.h supplies the Stream; it is shared verbatim with XiaoC6ExpBLE,
 * including the notification fix measured there on 2026-09-04 (chunking a
 * bundle into 20-byte notifications 3 ms apart is inside the connection
 * interval and corrupts the stream; it now sends a whole bundle in one
 * notification using the MTU the central granted).
 *
 * THIS BOARD HAS NO USER LED. The LED marked CH belongs to the battery
 * charger, not to a GPIO, and the variant defines no LED_BUILTIN — so there
 * is no /s/l here, and nothing to blink to show a connection. The OLED and
 * buzzer of the C6 expansion-board twin are likewise absent, so the greeting
 * announces one capability and no more.
 *
 * ADDRESSES (ADDRESSES.md):
 *   /rate <ms>   state period, 20..2000; 0 STOPS. Echoed.
 *   /state       -> /state <seq> <millis>, with /btn beside it
 *   /btn         -> /btn <int>, 1 = BOOT (GPIO9) pressed
 *   /enq         -> the greeting: name, then /enq/btn 1
 *
 * STATUS: see the end of this header.
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "ble_stream.h"
#include "transports.h"

// Nordic UART Service — the same UUIDs the nRF52840, C6 and MG24 twins
// advertise, so one Web Bluetooth page serves every one of them.
#define NUS_SERVICE "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   // central -> board
#define NUS_TX      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   // board -> central

static const int PIN_BTN = 9;            // D9 on this variant, active LOW

BLEStream bleStream;
_SLIPSerial<BLEStream> SLIPBle(bleStream);      // the whole BLE transport

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static int32_t  seq       = 0;
static uint32_t reportMs  = 100;

static OSCBundle bundleOUT, bundleINusb, bundleINble;

// ---- routes ----------------------------------------------------------------
static void addBtn() {
  bundleOUT.add("/btn").add((intOSC_t)(digitalRead(PIN_BTN) == LOW ? 1 : 0));
}

static void addState() {
  bundleOUT.add("/state").add((intOSC_t)seq).add((intOSC_t)millis());
  addBtn();
}

static void addEnq() {
  bundleOUT.add("/enq").add("XiaoC3BLE");
  bundleOUT.add("/enq/btn").add((intOSC_t)1);
}

void routeEnq(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addEnq();
}

void routeState(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addState();
}

// /enq/btn is a promise that a client can ASK for the button, not merely that
// it rides along in the stream (ADDRESSES.md).
void routeBtn(OSCMessage &msg, int addrOffset) {
  (void)msg; (void)addrOffset;
  addBtn();
}

void routeRate(OSCMessage &msg, int addrOffset) {
  (void)addrOffset;
  if (msg.isInt(0)) {
    const int32_t v = msg.getInt(0);
    reportMs = (v <= 0) ? 0 : (uint32_t)constrain(v, 20, 2000);   // 0 stops
  }
  bundleOUT.add("/rate").add((intOSC_t)reportMs);
}

// The core /s/* set. /s/l is absent on purpose and not by oversight: this
// board has no GPIO-driven LED, so under "absence is silence" it must answer
// nothing rather than pretend (ADDRESSES.md).
void routeSystem(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/m", addrOffset)) bundleOUT.add("/s/m").add((intOSC_t)micros());
  if (msg.fullMatch("/d", addrOffset)) bundleOUT.add("/s/d").add((intOSC_t)NUM_DIGITAL_PINS);
  if (msg.fullMatch("/a", addrOffset)) bundleOUT.add("/s/a").add((intOSC_t)NUM_ANALOG_INPUTS);
}

static void dispatchAll(OSCBundle &b) {
  b.route("/s",     routeSystem);
  b.route("/enq",   routeEnq);
  b.route("/state", routeState);
  b.route("/btn",   routeBtn);
  b.route("/rate",  routeRate);
}

// ---- BLE plumbing ----------------------------------------------------------
class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    // Hand the server over so BLEStream can ask what MTU the central granted
    // and send a whole bundle in one notification. See ble_stream.h.
    bleStream.setPeer(s);
    bleStream.setConnected(true);
  }
  void onDisconnect(BLEServer *s) override {
    bleStream.setConnected(false);
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

  BLEDevice::init("XiaoC3BLE");
  BLEDevice::setMTU(247);                  // the adapter uses what is granted
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

  // The boot greeting goes out over USB, where a host may be watching. Over
  // BLE nobody is connected yet, which is why /enq is also an inbound address.
  delay(300);
  addEnq();
  flushTo(SLIPSerial, bundleOUT);
}

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

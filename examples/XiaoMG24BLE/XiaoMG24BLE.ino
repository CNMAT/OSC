/*
 * XiaoMG24BLE — OSC over SLIP on two transports at once, USB serial and
 * Bluetooth LE, for the Seeed XIAO MG24 (Sense). Silicon Labs EFR32MG24.
 * -----------------------------------------------------------------------------
 * Board : Seeed XIAO MG24 (Sense), EFR32MG24, CMSIS-DAP interface chip
 * FQBN  : SiliconLabs:silabs:xiao_mg24:protocol_stack=ble_silabs
 *         The protocol_stack option is REQUIRED — with `none` there is no
 *         radio at all, and `ble_arduino` is a different API from the
 *         sl_bt_* one used here.
 * Libs  : none. The BLE stack and the GATT database come from the core.
 *
 * THE THIRD BLE STACK, AND THE SAME ONE-LINE TRANSPORT. `_SLIPSerial<T>`
 * needs T only to look like a Stream:
 *
 *     nRF52840  Bluefruit BLEUart IS a Stream      -> no adapter at all
 *     ESP32-C6  Bluedroid is a callback API        -> ~60-line adapter
 *     EFR32MG24 sl_bt_* is a callback API          -> ~60-line adapter (here)
 *
 * and in every case the line that carries OSC over the radio is the same:
 *
 *     _SLIPSerial<BLEStream> SLIPBle(bleStream);
 *
 * The service is the Nordic UART Service on all three, so ONE Web Bluetooth
 * page — examples/XiaoBLEOscuino/XiaoBLEOscuino.html — drives every one of
 * them. The GATT database here is built at runtime with sl_bt_gattdb_*,
 * adapted from the core's own ble_spp example.
 *
 * WHY NOT ezBLE? The core ships ezBLE, which is already a Stream and would
 * have needed no adapter — but its data characteristic is READ|WRITE with
 * no NOTIFY, so the board cannot push to a central and a browser would have
 * to poll it. Notifications are what make this board work with the same
 * page as the others, so the GATT database is built by hand.
 *
 * ADDRESSES — the standard Oscuino set (/d /a /tone /s) plus:
 *   /mg/led <int>     the user LED
 *   /mg/rate <ms>     state period, 20..2000; 0 stops the stream
 * State, streamed at that rate and on request:
 *   /mg <seq> <millis>
 *
 * TRANSPORT WARNING, measured (see BOARDS.md): this board's USB serial is a
 * CMSIS-DAP VCOM bridge that tolerates only ~242 bytes per write. Past that
 * it loses data, and past ~396 bytes it WEDGES until the board is physically
 * unplugged — surviving even a reflash, because the CDC endpoint lives in
 * the interface chip. Pacing fixes it: the same 1100 bytes sent with 5 ms
 * between frames are clean. Nothing here sends bursts that large, but a
 * client hammering this board over USB should keep single writes small.
 * The BLE path has no such limit.
 *
 * STATUS — VERIFIED over USB, 2026-09-02: /hello, the standard set
 * (/s/m, /s/d = 19, /s/a = 19), /mg state and /mg/led all answer, and the
 * ladder on this board passed echo 22/22 and widths 11/11 in a separate
 * build. The BLE half is **NOT yet verified on hardware**: advertising,
 * the dynamic GATT database, notifications and SLIP reassembly across
 * them are written to the documented sl_bt_* API and compile, but nothing
 * has connected to this board over the air. The nRF52840 and ESP32-C6
 * twins ARE verified and this follows their shape, which is an argument,
 * not a measurement. Treat the radio as untested until someone runs it —
 * point a Web Bluetooth central at XiaoBLEOscuino.html and look for
 * "XiaoMG24BLE" in the picker.
 */

#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include "ble_stream.h"
#include "transports.h"

SLIPEncodedSerial SLIPSerial(Serial);      // BOARD_HAS_USB_SERIAL is not
                                           // defined for this core, and that
                                           // is correct: Serial here is the
                                           // UART bridged to the CMSIS-DAP
                                           // interface chip, not native USB.

BLEStream bleStream;
_SLIPSerial<BLEStream> SLIPBle(bleStream);  // the whole BLE transport

// Nordic UART Service, byte-reversed as the sl_bt_ API expects — the same
// UUIDs the nRF52840 and ESP32-C6 twins advertise.
// 6e400001-b5a3-f393-e0a9-e50e24dcca9e
static const uuid_128 nus_service_uuid = {
  .data = { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
            0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e }
};
// 6e400002-... central -> board
static const uuid_128 nus_rx_uuid = {
  .data = { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
            0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e }
};
// 6e400003-... board -> central
static const uuid_128 nus_tx_uuid = {
  .data = { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
            0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e }
};

static const uint8_t advertised_name[] = "XiaoMG24BLE";
static uint16_t gattdb_session_id;
static uint16_t nus_service_handle;
static uint16_t nus_tx_handle;
static uint16_t nus_rx_handle;
static uint16_t generic_access_handle;
static uint16_t device_name_handle;
static uint8_t  advertising_set_handle = 0xff;

static int32_t  seq = 0;
static uint32_t reportMs = 0;               // 0 = not streaming
static OSCBundle bundleOUT;

// ---- addressing helpers ----------------------------------------------------
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

// ---- routes ----------------------------------------------------------------
void routeDigital(OSCMessage &msg, int addrOffset) {
  for (int pin = 0; pin < (int)NUM_DIGITAL_PINS; pin++) {
    int matched = msg.match(numToOSCAddress(pin), addrOffset);
    if (!matched) continue;
    char addr[12];
    if (msg.isInt(0)) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, msg.getInt(0) > 0 ? HIGH : LOW);
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
    if (!msg.match(numToOSCAddress(pin), addrOffset)) continue;
    char addr[12];
    pinAddress(addr, "/a", pin, NULL);
    bundleOUT.add(addr).add((intOSC_t)analogRead(pin));
    return;
  }
}

void routeSystem(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/m", addrOffset)) bundleOUT.add("/s/m").add((intOSC_t)micros());
  if (msg.fullMatch("/d", addrOffset)) bundleOUT.add("/s/d").add((intOSC_t)NUM_DIGITAL_PINS);
  if (msg.fullMatch("/a", addrOffset)) bundleOUT.add("/s/a").add((intOSC_t)NUM_ANALOG_INPUTS);
  if (msg.fullMatch("/l", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, v > 0 ? LED_BUILTIN_ACTIVE : LED_BUILTIN_INACTIVE);
    bundleOUT.add("/s/l").add((intOSC_t)v);
  }
}

static void addState() {
  bundleOUT.add("/mg").add((intOSC_t)seq).add((intOSC_t)millis());
}

void routeMG(OSCMessage &msg, int addrOffset) {
  if (msg.fullMatch("/led", addrOffset) && msg.isInt(0)) {
    int v = msg.getInt(0);
    pinMode(LED_BUILTIN, OUTPUT);
    // LED_BUILTIN_ACTIVE comes from the variant rather than folklore about
    // which way round the LED is wired.
    digitalWrite(LED_BUILTIN, v > 0 ? LED_BUILTIN_ACTIVE : LED_BUILTIN_INACTIVE);
    bundleOUT.add("/mg/led").add((intOSC_t)v);
    return;
  }
  if (msg.fullMatch("/rate", addrOffset) && msg.isInt(0)) {
    int r = msg.getInt(0);
    reportMs = (r <= 0) ? 0 : constrain(r, 20, 2000);
    bundleOUT.add("/mg/rate").add((intOSC_t)reportMs);
    return;
  }
  // Bare "/mg" with nothing after it is a request for the state packet.
  addState();
}

static void dispatchAll(OSCBundle &b) {
  if (b.hasError()) return;
  b.route("/d", routeDigital);
  b.route("/a", routeAnalog);
  b.route("/s", routeSystem);
  b.route("/mg", routeMG);
}

// ---- GATT database, built at runtime (pattern from the core's ble_spp) -----
static void ble_initialize_gatt_db() {
  sl_status_t sc = sl_bt_gattdb_new_session(&gattdb_session_id);
  if (sc != SL_STATUS_OK) return;

  // Generic Access (0x1800) with a Device Name characteristic (0x2A00).
  // Building the GATT database at runtime means there is no gatt_db.h and so
  // no gattdb_device_name symbol; the name has to be a characteristic we
  // create ourselves, exactly as the core's ble_spp example does.
  const uint8_t generic_access_uuid[] = { 0x00, 0x18 };
  sc = sl_bt_gattdb_add_service(gattdb_session_id,
                                sl_bt_gattdb_primary_service,
                                SL_BT_GATTDB_ADVERTISED_SERVICE,
                                sizeof(generic_access_uuid),
                                generic_access_uuid,
                                &generic_access_handle);
  if (sc != SL_STATUS_OK) return;

  const sl_bt_uuid_16_t device_name_uuid = { .data = { 0x00, 0x2A } };
  sc = sl_bt_gattdb_add_uuid16_characteristic(gattdb_session_id,
                                              generic_access_handle,
                                              SL_BT_GATTDB_CHARACTERISTIC_READ,
                                              0x00, 0x00,
                                              device_name_uuid,
                                              sl_bt_gattdb_fixed_length_value,
                                              sizeof(advertised_name) - 1,
                                              sizeof(advertised_name) - 1,
                                              advertised_name,
                                              &device_name_handle);
  if (sc != SL_STATUS_OK) return;
  sl_bt_gattdb_start_service(gattdb_session_id, generic_access_handle);

  sc = sl_bt_gattdb_add_service(gattdb_session_id,
                                sl_bt_gattdb_primary_service,
                                SL_BT_GATTDB_ADVERTISED_SERVICE,
                                sizeof(nus_service_uuid.data),
                                nus_service_uuid.data,
                                &nus_service_handle);
  if (sc != SL_STATUS_OK) return;

  // TX: what the board pushes to the central.
  uint8_t init_value = 0;
  sc = sl_bt_gattdb_add_uuid128_characteristic(gattdb_session_id,
                                               nus_service_handle,
                                               SL_BT_GATTDB_CHARACTERISTIC_NOTIFY,
                                               0x00, 0x00,
                                               nus_tx_uuid,
                                               sl_bt_gattdb_variable_length_value,
                                               244, 1, &init_value,
                                               &nus_tx_handle);
  if (sc != SL_STATUS_OK) return;

  // RX: what the central writes to the board.
  sc = sl_bt_gattdb_add_uuid128_characteristic(gattdb_session_id,
                                               nus_service_handle,
                                               SL_BT_GATTDB_CHARACTERISTIC_WRITE
                                               | SL_BT_GATTDB_CHARACTERISTIC_WRITE_NO_RESPONSE,
                                               0x00, 0x00,
                                               nus_rx_uuid,
                                               sl_bt_gattdb_variable_length_value,
                                               244, 1, &init_value,
                                               &nus_rx_handle);
  if (sc != SL_STATUS_OK) return;

  sl_bt_gattdb_start_service(gattdb_session_id, nus_service_handle);
  sl_bt_gattdb_commit(gattdb_session_id);
}

static void ble_start_advertising() {
  static bool init = true;
  if (init) {
    sl_bt_advertiser_create_set(&advertising_set_handle);
    sl_bt_advertiser_set_timing(advertising_set_handle, 160, 160, 0, 0);
    init = false;
  }
  sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                        sl_bt_advertiser_general_discoverable);
  sl_bt_legacy_advertiser_start(advertising_set_handle,
                                sl_bt_advertiser_connectable_scannable);
}

void sl_bt_on_event(sl_bt_msg_t *evt) {
  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      ble_initialize_gatt_db();
      sl_bt_gatt_server_set_max_mtu(247, NULL);
      ble_start_advertising();
      break;

    case sl_bt_evt_connection_opened_id:
      bleStream.attach(evt->data.evt_connection_opened.connection, nus_tx_handle);
      bleStream.setConnected(true);
      break;

    case sl_bt_evt_connection_closed_id:
      bleStream.setConnected(false);
      ble_start_advertising();        // or nothing can ever reconnect
      break;

    case sl_bt_evt_gatt_server_attribute_value_id:
      if (evt->data.evt_gatt_server_attribute_value.attribute == nus_rx_handle) {
        bleStream.feed(evt->data.evt_gatt_server_attribute_value.value.data,
                       evt->data.evt_gatt_server_attribute_value.value.len);
      }
      break;

    default:
      break;
  }
}

// -----------------------------------------------------------------------------

void setup() {
  SLIPSerial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_BUILTIN_INACTIVE);

  delay(300);
  bundleOUT.add("/hello").add("XiaoMG24BLE");
  SLIPSerial.beginPacket();
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
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

  static uint32_t last = 0;
  if (reportMs && millis() - last >= reportMs) {
    last = millis();
    seq++;
    addState();
    if (bleStream.connected()) flushTo(SLIPBle, bundleOUT);
    else                       flushTo(SLIPSerial, bundleOUT);
  }
}

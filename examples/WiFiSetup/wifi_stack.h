// wifi_stack.h -- everything about WiFi that differs from one Arduino core to
// the next, in one place, so WiFiSetup.ino can be written once.
//
// Six WiFi stacks present the same WiFiUDP / WiFiServer / WiFiClient objects,
// which is why the stock WiFi* examples are one sketch each. Bringing a board
// up as an access point with a settings page needs four more things that they
// do NOT spell the same way, and this header is the ladder for those:
//
//                       access point       AP address     clients?         settings store
//   ESP32 (all cores)   softAP()           softAPIP()     softAPgetStationNum()  Preferences (NVS)
//   ESP8266             softAP()           softAPIP()     softAPgetStationNum()  EEPROM + commit()
//   Pico W / Pico 2 W   softAP()           softAPIP()     softAPgetStationNum()  EEPROM + commit()
//   UNO R4 WiFi         beginAP()          softAPIP()     status()==WL_AP_CONNECTED  EEPROM (data flash)
//   Portenta C33        beginAP()          localIP()      status()==WL_AP_CONNECTED  EEPROM
//   NINA (Nano 33 IoT,  beginAP()          localIP()      status()==WL_AP_CONNECTED  WiFiStorage: a file on
//     MKR 1010, …)                                                               the NINA module itself
//   WINC1500 (MKR1000,  beginAP()          localIP()      status()==WL_AP_CONNECTED  FlashStorage (a page of
//     Feather M0 WiFi)                                                           the sketch's own flash)
//
// plus a restart, a "radio, do not sleep", and a MAC string. Nothing here
// touches a GPIO.
//
// Only the taken branch is compiled, so a board never asks for a library it
// does not have. The last branch is the fallback (WiFiNINA), so a board with
// no radio fails with "WiFiNINA.h: No such file", not with something subtler.
#pragma once

#if defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266mDNS.h>
  #include <EEPROM.h>
  #define WS_AP_SOFTAP
  #define WS_STORE_EEPROM_COMMIT
  #define WS_HAS_MDNS
#elif defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <ESPmDNS.h>
  #include <Preferences.h>
  #include <esp_mac.h>
  #define WS_AP_SOFTAP
  #define WS_STORE_PREFERENCES
  #define WS_HAS_MDNS
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
  // Raspberry Pi Pico W / Pico 2 W on arduino-pico (CYW43). The Nano RP2040
  // Connect on the mbed core is a WiFiNINA board and takes the last branch.
  #include <WiFi.h>
  #include <SimpleMDNS.h>
  #include <EEPROM.h>
  #define WS_AP_SOFTAP
  #define WS_STORE_EEPROM_COMMIT
  #define WS_HAS_MDNS
#elif defined(ARDUINO_UNOR4_WIFI)
  #include <WiFiS3.h>
  #include <EEPROM.h>
  #define WS_AP_BEGINAP
  #define WS_AP_IP_SOFTAPIP
  #define WS_STORE_EEPROM
#elif defined(ARDUINO_PORTENTA_C33)
  #include <WiFiC3.h>
  #include <EEPROM.h>
  #define WS_AP_BEGINAP
  #define WS_STORE_EEPROM
#elif defined(ADAFRUIT_FEATHER_M0) || defined(ARDUINO_SAMD_MKR1000)
  // ATWINC1500 parts want WiFi101, not WiFiNINA. adafruit_feather_m0 is one
  // FQBN for the whole Feather M0 family, so this branch assumes the WiFi one.
  #include <SPI.h>
  #include <WiFi101.h>
  #include <FlashStorage.h>
  #define WS_AP_BEGINAP
  #define WS_STORE_FLASHSTORAGE
  #define WS_NO_ACCEPT            // WiFi101's WiFiServer has available() only
  #if defined(ADAFRUIT_FEATHER_M0)
    #define WS_RADIO_PINS() WiFi.setPins(8, 7, 4, 2)
  #endif
#else
  #include <SPI.h>
  #include <WiFiNINA.h>
  #define WS_AP_BEGINAP
  #define WS_STORE_NINA
#endif

#include <WiFiUdp.h>

#ifndef WS_RADIO_PINS
  #define WS_RADIO_PINS() do {} while (0)
#endif

// ---------------------------------------------------------------------------
// Access point
// ---------------------------------------------------------------------------

// Bring up an OPEN network with this SSID. Open on purpose: it exists for the
// minute it takes to type in credentials, and a password on it would have to
// be printed somewhere first, which is the problem being solved.
static bool apStart(const char *ssid) {
#if defined(WS_AP_SOFTAP)
  #if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    WiFi.mode(WIFI_AP);
  #endif
  return WiFi.softAP(ssid);
#else
  return WiFi.beginAP(ssid) == WL_AP_LISTENING;
#endif
}

static IPAddress apIP() {
#if defined(WS_AP_SOFTAP) || defined(WS_AP_IP_SOFTAPIP)
  return WiFi.softAPIP();
#else
  return WiFi.localIP();
#endif
}

static bool apHasClient() {
#if defined(WS_AP_SOFTAP)
  return WiFi.softAPgetStationNum() > 0;
#else
  return WiFi.status() == WL_AP_CONNECTED;
#endif
}

// ---------------------------------------------------------------------------
// Station
// ---------------------------------------------------------------------------

static void staStart(const char *ssid, const char *pass) {
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  // The ESP cores keep their own copy of the last credentials in flash and
  // reconnect to it behind the sketch's back. One store, this sketch's.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
#endif
  if (pass && pass[0]) WiFi.begin(ssid, pass);
  else                 WiFi.begin(ssid);
}

// Modem power save batches traffic to the access point's DTIM beacons.
// Measured on the XIAO ESP32-C6 (A/B, three runs each way): UDP round trip
// 112 ms median with it, 10 ms without; on a weak link it became 66 % loss.
// OSC is interactive, so every stack that can turn it off does.
static void radioNoSleep() {
#if defined(ARDUINO_ARCH_ESP32)
  WiFi.setSleep(false);
#elif defined(ARDUINO_ARCH_ESP8266)
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#elif defined(WS_STORE_NINA) || (defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED))
  WiFi.noLowPowerMode();
#endif
}

// "aa:bb:cc:dd:ee:ff" into a caller's 18-byte buffer.
//
// Measured on the M5Capsule (esp32 core 3.3.11): WiFi.macAddress() before the
// station driver is up returns 00:00:2c:00:00:00, and the board named itself
// oscuino-0000. The efuse is readable at any time, so read that instead; it
// is the station MAC on every ESP32, in every mode.
static void macString(char *out) {
  uint8_t m[6];
#if defined(ARDUINO_ARCH_ESP32)
  esp_read_mac(m, ESP_MAC_WIFI_STA);
#else
  WiFi.macAddress(m);
#endif
  const char *hex = "0123456789abcdef";
  for (int i = 0; i < 6; i++) {
    out[i * 3]     = hex[m[i] >> 4];
    out[i * 3 + 1] = hex[m[i] & 15];
    out[i * 3 + 2] = (i < 5) ? ':' : '\0';
  }
}

// ---------------------------------------------------------------------------
// Restart. A reboot is the one mode switch every stack survives: leaving an
// access point for a station (or back) without one has a different set of
// half-torn-down states on each radio firmware.
// ---------------------------------------------------------------------------
static void restartBoard() {
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  ESP.restart();
#elif defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
  rp2040.reboot();
#else
  NVIC_SystemReset();
#endif
  while (true) delay(10);
}

// ---------------------------------------------------------------------------
// The settings store: one opaque blob, load and save. The sketch owns the
// layout and checks its own magic, so a store that returns garbage (a fresh
// EEPROM, a page of erased flash) is simply "no settings yet".
// ---------------------------------------------------------------------------
#if defined(WS_STORE_PREFERENCES)
static bool storeLoad(void *buf, size_t n) {
  Preferences p;
  if (!p.begin("oscwifi", true)) return false;      // read-only; false = never written
  const size_t got = p.getBytes("s", buf, n);
  p.end();
  return got == n;
}
static bool storeSave(const void *buf, size_t n) {
  Preferences p;
  if (!p.begin("oscwifi", false)) return false;
  const size_t put = p.putBytes("s", buf, n);
  p.end();
  return put == n;
}

#elif defined(WS_STORE_EEPROM_COMMIT) || defined(WS_STORE_EEPROM)
static bool storeLoad(void *buf, size_t n) {
  #if defined(WS_STORE_EEPROM_COMMIT)
  EEPROM.begin(512);
  #endif
  uint8_t *b = (uint8_t *) buf;
  for (size_t i = 0; i < n; i++) b[i] = EEPROM.read((int) i);
  return true;                                     // the caller checks the magic
}
static bool storeSave(const void *buf, size_t n) {
  const uint8_t *b = (const uint8_t *) buf;
  #if defined(WS_STORE_EEPROM_COMMIT)
  EEPROM.begin(512);
  for (size_t i = 0; i < n; i++) EEPROM.write((int) i, b[i]);
  return EEPROM.commit();
  #else
  for (size_t i = 0; i < n; i++) EEPROM.update((int) i, b[i]);   // data flash: write only what changed
  return true;
  #endif
}

#elif defined(WS_STORE_NINA)
// A file on the NINA module's own flash: it survives reflashing the host MCU,
// which is exactly when a bring-up tool should NOT forget the network.
static const char *WS_NINA_FILE = "/fs/oscwifi";
static bool storeLoad(void *buf, size_t n) {
  WiFiStorageFile f = WiFiStorage.open(WS_NINA_FILE);
  if (!f || f.size() != n) return false;
  f.seek(0);
  return f.read(buf, n) == n;
}
static bool storeSave(const void *buf, size_t n) {
  WiFiStorageFile f = WiFiStorage.open(WS_NINA_FILE);
  f.erase();
  return f.write(buf, n) == n;
}

#elif defined(WS_STORE_FLASHSTORAGE)
// A page of the sketch's own flash. Erased by every upload, so a WINC board
// must be set up again after each reflash; the NINA path above does better.
struct WsFlashBlob { uint8_t b[256]; };
FlashStorage(wsFlash, WsFlashBlob);
static bool storeLoad(void *buf, size_t n) {
  if (n > sizeof(WsFlashBlob)) return false;
  WsFlashBlob blob = wsFlash.read();
  memcpy(buf, blob.b, n);
  return true;
}
static bool storeSave(const void *buf, size_t n) {
  if (n > sizeof(WsFlashBlob)) return false;
  WsFlashBlob blob = {};
  memcpy(blob.b, buf, n);
  wsFlash.write(blob);
  return true;
}
#else
  #error "wifi_stack.h: no settings store for this board"
#endif

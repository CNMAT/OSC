/*
  Make an OSC bundle and send it over WiFi

  OSCBundles allow OSCMessages to be grouped together to preserve the order and
  completeness of related messages. They also allow for timetags to be carried
  to represent the presentation time of the messages.

  The current Arduino WiFi libraries all present the same WiFiUDP object, so one
  sketch covers them and only the header to include differs.

  Set ssid/pass below, and outIp to the machine running your OSC receiver.

  Compiled with arduino-cli 1.5.1 for the UNO R4 WiFi, Nano 33 IoT, Portenta C33
  and ESP32. The ESP8266 branch has not been compiled — that core is not
  installed on the development machine — and this has not been run on hardware.

  Addresses renamed onto ADDRESSES.md on 2026-09-03 (/analog/0 -> /a/0, /digital/5 -> /d/5, /micros -> /s/m);
  that build is compile-checked and has not been re-run on the board.

  Adrian Freed
 */

// ---------------------------------------------------------------------------
// Pick this board's WiFi stack. Only the taken branch is compiled, so the
// sketch never asks for a library the board does not have.
//
//   ESP8266                                    ESP8266WiFi (esp8266 core)
//   ESP32 / S2 / S3 / C3 / C6                  WiFi        (esp32 core)
//   UNO R4 WiFi                                WiFiS3      (bundled in the core)
//   Portenta C33                               WiFiC3      (bundled in the core)
//   Nano 33 IoT, MKR WiFi 1010, Nano RP2040    WiFiNINA    (Library Manager)
//
// The last branch is the fallback, so a board with no radio at all — an UNO R4
// Minima, a plain Nano — fails with "WiFiNINA.h: No such file", not with
// something subtler. MKR1000 and the old WiFi shield want WiFi101, whose API is
// close enough to WiFiNINA's that a branch of its own is all it takes.
// ---------------------------------------------------------------------------
#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#elif defined(ARDUINO_UNOR4_WIFI)
#include <WiFiS3.h>
#define OSC_WIFI_HAS_MODULE_STATUS
#elif defined(ARDUINO_PORTENTA_C33)
#include <WiFiC3.h>
#elif defined(ADAFRUIT_FEATHER_M0) || defined(ARDUINO_SAMD_MKR1000)
// ATWINC1500 parts: the Feather M0 WiFi and the MKR1000 want WiFi101, not
// WiFiNINA. They are SAMD21s like several WiFiNINA boards, so there is no
// architecture macro to separate them -- it has to be by board.
//
// NOTE: adafruit:samd:adafruit_feather_m0 is one FQBN for the whole Feather
// M0 family -- Basic, Adalogger, WiFi, RFM69, LoRa -- and they all define
// ADAFRUIT_FEATHER_M0. This branch therefore assumes the WiFi variant. On a
// Basic or Adalogger there is simply no radio for WiFi101 to find.
#include <SPI.h>
#include <WiFi101.h>
// WiFi101 has no WL_NO_MODULE; the equivalent "no radio found" status is
// WL_NO_SHIELD, so this branch reports it under its own name rather than
// claiming the WiFiNINA constant.
#define OSC_WIFI_NO_RADIO WL_NO_SHIELD
#if defined(ADAFRUIT_FEATHER_M0)
// The Feather M0 WiFi has the ATWINC1500 on its own pins rather than the
// shield defaults, so they must be handed to the library before begin().
#define OSC_WIFI_PINS() WiFi.setPins(8, 7, 4, 2)
#endif
#else
#include <SPI.h>
#include <WiFiNINA.h>
#define OSC_WIFI_HAS_MODULE_STATUS
#endif

#ifndef OSC_WIFI_PINS
#define OSC_WIFI_PINS() do {} while (0)
#endif
#include <WiFiUdp.h>
#include <OSCBundle.h>
#include <OSCTiming.h>
#include <OSCBufferedPrint.h>

char ssid[] = "your-network-name";
char pass[] = "your-network-password";

WiFiUDP Udp;

//scratch space for one OSC packet. A bundle costs more writes than a single
//message — the header, the timetag and a size prefix per element, on top of
//each message's own — so it gains the most from being buffered here.
uint8_t packetbuf[256];

// destination IP: the machine listening for OSC
const IPAddress outIp(192, 168, 1, 100);
const unsigned int outPort = 9999;

void connectWiFi() {
#ifdef OSC_WIFI_HAS_MODULE_STATUS
  // WiFiNINA and WiFiS3 drive a separate radio chip. If it is missing or wedged
  // the sketch would otherwise sit in the connect loop below forever with no
  // clue as to why.
#ifndef OSC_WIFI_NO_RADIO
#define OSC_WIFI_NO_RADIO WL_NO_MODULE
#endif
  if (WiFi.status() == OSC_WIFI_NO_RADIO) {
    Serial.println("communication with the WiFi module failed");
    while (true) delay(1000);
  }
#endif
#ifdef WIFI_FIRMWARE_LATEST_VERSION
  if (String(WiFi.firmwareVersion()) < WIFI_FIRMWARE_LATEST_VERSION)
    Serial.println("the radio firmware is older than this library expects");
#endif

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass);
    // WiFiNINA, WiFiS3 and WiFiC3 block inside begin() until the attempt
    // resolves; the ESP cores return immediately, so poll before retrying
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++)
      delay(500);
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  connectWiFi();
  Udp.begin(8888);
}

void loop() {
  //declare the bundle
  OSCBundle bndl;

  //OSCBundle's 'add' returns the OSCMessage so the message's 'add' can be composed together
  //
  //A0 is the only analog pin all of these boards name: the ESP8266 has just the
  //one, and the classic ESP32 skips A1 and A2 because GPIO37 and GPIO38 are not
  //brought out. Add your own board's other pins here.
  bndl.add("/a/0").add((intOSC_t)analogRead(A0));
  bndl.add("/d/5").add((intOSC_t)digitalRead(5));
  bndl.add("/s/m").add((intOSC_t)micros());

  Udp.beginPacket(outIp, outPort);
    //send() writes the packet in small pieces, and on the UNO R4 WiFi and its
    //clones each of those is a separate command to the ESP32-S3 radio over a
    //UART. Buffering it here makes the whole bundle one write. Sending straight
    //to Udp works too; see OSCBufferedPrint.h.
    OSCBufferedPrint out(Udp, packetbuf, sizeof(packetbuf));
    bndl.send(out); // accumulate the bytes
    out.flush();    // one Udp.write() for the whole packet
  Udp.endPacket(); // mark the end of the OSC Packet
  bndl.empty(); // empty the bundle to free room for a new one

  // a bundle can also carry a timetag, asking the receiver to act on its
  // messages at a stated time rather than on arrival. Without one a bundle goes
  // out as "immediately", as the one above did.
  //
  // This is a second bundle rather than a setTimetag() on the one above because
  // empty() clears the messages but keeps the timetag: reusing that bundle
  // would send the next loop's readings with this loop's presentation time.
  osctime_t oneSecondFromNow = oscTime();
  oneSecondFromNow.seconds += 1;

  OSCBundle timed(oneSecondFromNow);
  timed.add("/a/0").add((intOSC_t)analogRead(A0));

  Udp.beginPacket(outIp, outPort);
    OSCBufferedPrint timedOut(Udp, packetbuf, sizeof(packetbuf));
    timed.send(timedOut);
    timedOut.flush();
  Udp.endPacket();
  timed.empty();

  delay(1000);
}

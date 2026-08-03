/*
  Make an OSC message and send it over WiFi

  The current Arduino WiFi libraries all present the same WiFiUDP object, so one
  sketch covers them and only the header to include differs. WiFiUDP derives
  from Print, which is all OSCMessage::send() asks for.

  Set ssid/pass below, and outIp to the machine running your OSC receiver.

  Compiled with arduino-cli 1.5.1 for the UNO R4 WiFi, Nano 33 IoT, Portenta C33
  and ESP32. The ESP8266 branch has not been compiled — that core is not
  installed on the development machine — and this has not been run on hardware.

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
#else
#include <SPI.h>
#include <WiFiNINA.h>
#define OSC_WIFI_HAS_MODULE_STATUS
#endif
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <OSCBufferedPrint.h>

char ssid[] = "your-network-name";
char pass[] = "your-network-password";

WiFiUDP Udp;

//scratch space for one OSC packet, sized for the message below with room to
//spare. A bigger packet is not truncated, it just takes more than one write.
uint8_t packetbuf[128];

// destination IP: the machine listening for OSC
const IPAddress outIp(192, 168, 1, 100);
const unsigned int outPort = 9999;

void connectWiFi() {
#ifdef OSC_WIFI_HAS_MODULE_STATUS
  // WiFiNINA and WiFiS3 drive a separate radio chip. If it is missing or wedged
  // the sketch would otherwise sit in the connect loop below forever with no
  // clue as to why.
  if (WiFi.status() == WL_NO_MODULE) {
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
  //the message wants an OSC address as first argument
  OSCMessage msg("/analog/0");
  msg.add((intOSC_t)analogRead(A0));

  Udp.beginPacket(outIp, outPort);
    //send() writes the packet in small pieces — the address, each pad byte on
    //its own, the comma, each type character. On the UNO R4 WiFi and its clones
    //every one of those is a separate command to the ESP32-S3 radio over a
    //UART, so buffering the packet here turns eight round trips into one.
    //Sending straight to Udp works too; see OSCBufferedPrint.h.
    OSCBufferedPrint out(Udp, packetbuf, sizeof(packetbuf));
    msg.send(out); // accumulate the bytes
    out.flush();   // one Udp.write() for the whole packet
  Udp.endPacket(); // mark the end of the OSC Packet
  msg.empty(); // free space occupied by message

  delay(20);
}

// Seeed XIAO ESP32-C6 on the XIAO Expansion Board: OLED, buzzer and button
// over OSC on WiFi.
//
//   Page: XiaoC6ExpWiFi.html, generated beside this sketch by extras/webserial;
//   pick HTTP there (extras/webserial/oscuino.html is the same page for any board).
//
// The twin of XiaoC6ExpOscuino. Same board, same peripherals, same OSC
// vocabulary -- only the transport differs, and comparing the two side by
// side is the point:
//
//                    XiaoC6ExpOscuino          XiaoC6ExpWiFi (this one)
//   transport        USB CDC                   802.11 (2.4 GHz only on C6)
//   framing          SLIP, because a stream    none: a UDP datagram already
//                    has no packet boundaries  has a length
//   addressing       whichever port appeared   an IP the board must tell you
//   who may talk     one host, exclusively     anything on the network
//   page reaches it  Web Serial                HTTP fetch (browsers cannot
//                                              send UDP at all)
//
// SLIPEncodedSerial is absent here on purpose. SLIP exists to mark packet
// boundaries in a byte stream; UDP preserves them already, so an OSC packet
// is simply the datagram payload. Sending SLIP over UDP would be a bug that
// happens to work.
//
// TWO WAYS IN, one OSC vocabulary:
//
//   UDP port 8000     for real OSC software -- Max, Pd, TouchOSC, oscsend.
//                     Replies go to whoever asked (Udp.remoteIP()), so
//                     nothing needs configuring here.
//   HTTP port 80      for the browser page, which cannot send UDP. POST the
//                     same OSC bytes to /osc; GET /state for the latest state
//                     bundle; GET /enq for the greeting, as OSC bytes rather
//                     than a JSON shape of its own, so the page needs one
//                     decoder. CORS is open so the page can be served from
//                     localhost while the board lives elsewhere.
//
// Both paths hand bytes to one handler, so the two cannot drift apart.
//
// FQBN: esp32:esp32:XIAO_ESP32C6 with STOCK DEFAULTS -- see the note in the
// serial twin about not carrying another ESP32 board's CDCOnBoot over.
//
// STATUS -- VERIFIED ON HARDWARE, 2026-09-04, on a XIAO ESP32-C6
// (MAC 10:bd:a3:9f:ef:ac), 7 of 7 checks:
//   association, and the board reported its own address on the USB log
//     (OSC/UDP on 192.168.0.240:8000);
//   UDP 20/20 round trips, median 8 ms, worst 29 ms, each replying to
//     Udp.remoteIP() with the state bundle (/state + /btn);
//   /rate 0 still answers over UDP -- the reply is the answer to the request,
//     not a tick of the stream, so stopping the stream does not go silent;
//   GET /state returns the same OSC bytes with Access-Control-Allow-Origin: *;
//   GET /enq returns the greeting as OSC: name XiaoC6ExpWiFi, capabilities
//     btn, buzz, net;
//   POST /osc accepts a raw OSC body containing NUL bytes.
// NOT verified: the IP shown on the OLED. This C6 was bare, so displayOK came
// back false and the display addresses answered without driving anything --
// which is itself the capability design working: it announced no /enq/display.
//
// SET YOUR CREDENTIALS BELOW. The OLED shows the IP once connected, which is
// the whole reason this sketch bothers with the display first: on WiFi a
// board that cannot tell you its address is a board you cannot talk to.
// The HTTP bridge exists only so a BROWSER can reach the board -- browsers
// cannot open UDP sockets. Set it to 0 if you drive this from Max, Pd,
// TouchOSC or oscsend. Measured on this sketch, three builds:
//
//   USB serial twin           319,734 B   24%
//   WiFi, UDP only          1,027,404 B   78%
//   WiFi + HTTP bridge      1,071,486 B   81%
//
// So the bridge is 44 KB, and the radio is the expensive part: WiFi and its
// TCP/IP stack account for ~708 KB, more than twice the entire serial twin.
// That is the real answer to "why is the WiFi build so large" -- it is not
// this sketch, and turning off the bridge barely dents it.
#ifndef XC6_HTTP_BRIDGE
#define XC6_HTTP_BRIDGE 1
#endif

#include <WiFi.h>
#include <WiFiUdp.h>
#if XC6_HTTP_BRIDGE
#include <WebServer.h>
#endif
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <OSCMessage.h>
#include <OSCBundle.h>

// ---------------------------------------------------------------------------
// CREDENTIALS LIVE OUTSIDE THIS FILE, and outside the repository.
//
//     cp arduino_secrets.h.example arduino_secrets.h   # then edit it
//
// arduino_secrets.h is in .gitignore and a pre-commit hook rejects any attempt
// to add one, so a password cannot reach git history even by accident. Editing
// the placeholders below instead would put your password in a tracked file --
// don't. __has_include means the sketch still compiles with no secrets file at
// all, which is what CI and anyone cloning this gets.
#if defined(__has_include)
#  if __has_include("arduino_secrets.h")
#    include "arduino_secrets.h"
#  endif
#endif

#ifndef SECRET_SSID
#define SECRET_SSID "your-ssid"          // placeholder: association will fail
#define SECRET_PASS "your-password"
#endif

const char *WIFI_SSID = SECRET_SSID;
const char *WIFI_PASS = SECRET_PASS;
// ---------------------------------------------------------------------------

#define OSC_PORT  8000
#define OLED_W    128
#define OLED_H    64
#define OLED_ADDR 0x3C          // scanned on the real bus, not assumed
#define PIN_BUZZ  D3            // GPIO21
#define PIN_BTN   D1            // GPIO1, active LOW
#define RTC_ADDR  0x51          // PCF8563

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
WiFiUDP   Udp;
#if XC6_HTTP_BRIDGE
WebServer http(80);
#endif

static bool     displayOK = false;
static bool     rtcOK     = false;
static int32_t  seq       = 0;
static uint32_t reportMs  = 50;
static uint32_t buzzUntil = 0;
static char     lines[4][22] = { "XIAO ESP32-C6", "WiFi OSC", "", "" };
static char     bigLine[12]  = "";
static bool     bigMode   = false;
static bool     showNet   = true;      // until /display/* overrides it

// The last state packet, kept encoded so GET /state and the UDP reply are
// byte-identical rather than two encoders that might disagree.
static uint8_t  stateBuf[96];
static size_t   stateLen = 0;

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void redraw() {
  if (!displayOK) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  if (showNet) {
    oled.setCursor(0, 0);  oled.print("XIAO C6  OSC/WiFi");
    oled.setCursor(0, 16);
    if (WiFi.status() == WL_CONNECTED) {
      oled.print(WiFi.localIP());
      oled.setCursor(0, 28); oled.print("udp  "); oled.print(OSC_PORT);
      oled.setCursor(0, 40); oled.print("http "); oled.print(80);
      oled.setCursor(0, 52); oled.print(WiFi.RSSI()); oled.print(" dBm");
    } else {
      oled.print("connecting...");
      oled.setCursor(0, 28); oled.print(WIFI_SSID);
    }
  } else if (bigMode) {
    oled.setTextSize(2);
    const int16_t w = (int16_t) strlen(bigLine) * 12;
    oled.setCursor(w < OLED_W ? (OLED_W - w) / 2 : 0, (OLED_H - 16) / 2);
    oled.print(bigLine);
  } else {
    for (uint8_t i = 0; i < 4; i++) {
      oled.setCursor(0, (int16_t)(i * 10));
      oled.print(lines[i]);
    }
  }
  oled.display();
}

/* ------------------------------------------------------------- OSC handling */

static void routeText(OSCMessage &m) {
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  const int n = m.size() < 4 ? m.size() : 4;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) m.getString(i, lines[i], sizeof lines[i]);
  bigMode = false; showNet = false;
  redraw();
}

static void routeBig(OSCMessage &m) {
  if (m.size() < 1 || !m.isString(0)) return;
  m.getString(0, bigLine, sizeof bigLine);
  bigMode = true; showNet = false;
  redraw();
}

static void routeClear(OSCMessage &) {
  for (uint8_t i = 0; i < 4; i++) lines[i][0] = '\0';
  bigLine[0] = '\0'; bigMode = false; showNet = false;
  redraw();
}

static void routeNet(OSCMessage &)   { showNet = true; redraw(); }

static void routeInvert(OSCMessage &m) {
  if (displayOK && m.size() >= 1 && m.isInt(0)) oled.invertDisplay(m.getInt(0) != 0);
}

static void routeBuzz(OSCMessage &m) {
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t freq = m.getInt(0);
  const int32_t ms   = (m.size() > 1 && m.isInt(1)) ? m.getInt(1) : 150;
  if (freq <= 0) { noTone(PIN_BUZZ); buzzUntil = 0; return; }
  tone(PIN_BUZZ, (unsigned int) freq);
  buzzUntil = millis() + (uint32_t) constrain(ms, 10, 5000);
}

static void routeLed(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) digitalWrite(LED_BUILTIN, m.getInt(0) ? LOW : HIGH);
}

static void routeRate(OSCMessage &m) {
  // 0 STOPS the stream (ADDRESSES.md). constrain(v, 20, 2000) turns 0 into 20,
  // so asking the board to be quiet made it stream faster -- measured on the
  // serial twin, 2026-09-04, and the same line was here.
  if (!(m.size() >= 1 && m.isInt(0))) return;
  const int32_t v = m.getInt(0);
  reportMs = (v <= 0) ? 0 : (uint32_t) constrain(v, 20, 2000);
}

// One entry point for both transports. Whatever arrives -- datagram payload or
// HTTP body -- is the same OSC packet and takes the same path.
static void handlePacket(const uint8_t *data, size_t len) {
  OSCMessage m;
  m.fill((uint8_t *) data, (int) len);
  if (m.hasError()) return;
  m.dispatch("/display/text",   routeText);
  m.dispatch("/display/big",    routeBig);
  m.dispatch("/display/clear",  routeClear);
  m.dispatch("/display/net",    routeNet);
  m.dispatch("/display/invert", routeInvert);
  m.dispatch("/buzz",        routeBuzz);
  m.dispatch("/s/l",         routeLed);
  m.dispatch("/rate",        routeRate);
}

// seq is NOT incremented here. buildState() has three callers -- the
// periodic refresh, the UDP reply, and GET /state -- and when each bump was
// buried in here the counter stopped meaning "reporting periods elapsed":
// every poll consumed numbers, and the page's gap arithmetic dutifully
// reported the phantom drops. Only the periodic tick in loop() advances it.
static void buildState() {
  OSCBundle m;
  m.add("/state").add((intOSC_t) seq).add((intOSC_t) millis());
  m.add("/btn").add((intOSC_t) (digitalRead(PIN_BTN) == LOW ? 1 : 0));
  stateLen = 0;
  const int n = 96;                      // a bundle of these two is under that
  if (n > 0 && n <= (int) sizeof stateBuf) {
    // OSCMessage has no encode-to-buffer, so send() it into a tiny Print
    // that captures bytes -- the same encoder both transports then reuse.
    struct Cap : public Print {
      uint8_t *b; size_t n, cap;
      size_t write(uint8_t c) override { if (n < cap) b[n++] = c; return 1; }
    } cap;
    cap.b = stateBuf; cap.n = 0; cap.cap = sizeof stateBuf;
    m.send(cap);
    stateLen = cap.n;
  }
}

/* ------------------------------------------------------------------- HTTP */
#if XC6_HTTP_BRIDGE

static void cors() { http.sendHeader("Access-Control-Allow-Origin", "*"); }

// The OSC body must NOT come through http.arg("plain"): the WebServer stores
// that as an Arduino String built with strlen, and every OSC packet contains
// NUL bytes -- the address terminator at minimum -- so "/led 1" (16 bytes on
// the wire) arrived as 4 and nothing ever dispatched. The whole browser path
// was dead, exactly as the header's untested warning allowed. The raw-upload
// path below hands over the bytes uncounted and untouched; it is enabled by
// registering the FOURTH argument in http.on(), which is what makes the
// server's canRaw() true.
static uint8_t oscRaw[512];
static size_t  oscRawLen = 0;

static void httpOscRaw() {
  HTTPRaw &r = http.raw();
  if (r.status == RAW_START) {
    oscRawLen = 0;
  } else if (r.status == RAW_WRITE) {
    const size_t room = sizeof oscRaw - oscRawLen;
    const size_t n = r.currentSize < room ? r.currentSize : room;
    memcpy(oscRaw + oscRawLen, r.buf, n);
    oscRawLen += n;
  } else if (r.status == RAW_END) {
    handlePacket(oscRaw, oscRawLen);
  }
}

static void httpOsc() {                       // POST /osc -- body already
  cors();                                     // dispatched by httpOscRaw
  if (http.method() == HTTP_OPTIONS) {        // preflight
    http.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    http.sendHeader("Access-Control-Allow-Headers", "content-type");
    http.send(204);
    return;
  }
  http.send(200, "text/plain", "ok");
}

static void httpState() {                     // GET /state -> the /xc6 packet
  cors();
  buildState();
  http.send_P(200, "application/octet-stream", (const char *) stateBuf, stateLen);
}

static void httpEnq() {                     // GET /enq -> the /enq bundle
  cors();
  OSCBundle b;
  b.add("/enq").add("XiaoC6ExpWiFi");
  b.add("/enq/btn").add((intOSC_t) 1);
  b.add("/enq/buzz");
  if (displayOK) b.add("/enq/display").add((intOSC_t) OLED_W).add((intOSC_t) OLED_H);
  b.add("/enq/net").add(WiFi.localIP().toString().c_str())
                   .add((intOSC_t) WiFi.RSSI()).add((intOSC_t) OSC_PORT);
  static uint8_t buf[160];
  struct Cap : public Print {
    uint8_t *b; size_t n, cap;
    size_t write(uint8_t c) override { if (n < cap) b[n++] = c; return 1; }
  } cap;
  cap.b = buf; cap.n = 0; cap.cap = sizeof buf;
  b.send(cap);
  http.send_P(200, "application/octet-stream", (const char *) buf, cap.n);
}

#endif  // XC6_HTTP_BRIDGE

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);            // active LOW on this module

  Wire.begin();
  displayOK = i2cPresent(OLED_ADDR) && oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  rtcOK     = i2cPresent(RTC_ADDR);
  if (displayOK) { oled.setTextColor(SSD1306_WHITE); oled.cp437(true); redraw(); }

  WiFi.mode(WIFI_STA);                        // C6 is 2.4 GHz only
  // Modem power save OFF. Left on -- the ESP32 default -- the radio batches
  // traffic to DTIM beacons, which is latency an interactive protocol cannot
  // afford. Measured on this board, A/B, three runs each way at RSSI -67:
  //
  //                     sleep ON (default)      setSleep(false)
  //   UDP median            112 ms                  10 ms
  //   UDP p90               353 ms                  41 ms
  //   UDP worst             819 ms                  70 ms
  //   ping average    147 / 155 / 226 ms     17 / 23 / 23 ms
  //
  // No packet loss either way at this signal strength; the cost is purely
  // latency and jitter. On the EGG C3 twin at -91 dBm the same batching
  // compounded into 1015 ms RTT and 66 % ping loss, so the weaker the link,
  // the worse it gets. The pathology is the default, not the board.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    if (displayOK && (i % 4) == 0) redraw();  // keep the OLED honest meanwhile
  }

  if (WiFi.status() == WL_CONNECTED) {
    Udp.begin(OSC_PORT);
#if XC6_HTTP_BRIDGE
    // 4-arg form: the raw handler is what routes the body around the
    // String-based arg parser (see httpOscRaw above)
    http.on("/osc",   HTTP_POST,    httpOsc, httpOscRaw);
    http.on("/osc",   HTTP_OPTIONS, httpOsc);
    http.on("/state", HTTP_GET,     httpState);
    http.on("/enq", HTTP_GET,     httpEnq);
    http.begin();
#endif
    Serial.print("OSC/UDP on "); Serial.print(WiFi.localIP());
    Serial.print(":"); Serial.println(OSC_PORT);
  } else {
    Serial.println("WiFi did not connect -- check WIFI_SSID/WIFI_PASS");
  }
  redraw();
}

void loop() {
  static uint32_t lastReport = 0, lastNet = 0;

#if XC6_HTTP_BRIDGE
  http.handleClient();
#endif

  // inbound UDP: a datagram IS a packet, so no SLIP and no reassembly
  int avail = Udp.parsePacket();
  if (avail > 0) {
    static uint8_t in[512];
    const int n = Udp.read(in, avail > (int) sizeof in ? (int) sizeof in : avail);
    if (n > 0) {
      handlePacket(in, (size_t) n);
      // reply to whoever asked, so no peer address is configured here
      buildState();
      if (stateLen) {
        Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        Udp.write(stateBuf, stateLen);
        Udp.endPacket();
      }
    }
  }

  const uint32_t now = millis();

  if (buzzUntil && now >= buzzUntil) { noTone(PIN_BUZZ); buzzUntil = 0; }

  if (reportMs != 0 && now - lastReport >= reportMs) {
    lastReport = now;
    seq++;                                    // the one place seq advances
    buildState();                             // keeps /state fresh for HTTP
  }

  // refresh the network panel occasionally so RSSI and a lost link show up
  if (showNet && now - lastNet >= 1000) { lastNet = now; redraw(); }
}

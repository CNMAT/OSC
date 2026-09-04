// Seeed XIAO ESP32-C3: the same OSC vocabulary as XiaoC3Oscuino, over WiFi.
//
//   Page: XiaoC3WiFi.html, generated beside this sketch by extras/webserial;
//   pick HTTP there (extras/webserial/oscuino.html is the same page for any
//   board).
//
// The WiFi twin of XiaoC3Oscuino. Same board, same addresses, different
// transport -- and the pair exists so the two can be compared:
//
//                    XiaoC3Oscuino            XiaoC3WiFi (this one)
//   transport        USB CDC                  802.11 (2.4 GHz only on C3)
//   framing          SLIP, because a stream   none: a UDP datagram already
//                    has no packet boundaries has a length
//   addressing       whichever port appeared  an IP the board must tell you
//   who may talk     one host, exclusively    anything on the network
//   page reaches it  Web Serial               HTTP fetch (browsers cannot
//                                             send UDP at all)
//
// SLIPEncodedSerial is deliberately absent. SLIP marks packet boundaries in a
// byte stream; UDP preserves them already, so an OSC packet is simply the
// datagram payload. Sending SLIP over UDP would be a bug that happens to work.
//
// THIS BOARD HAS NO USER LED AND NO DISPLAY. The LED marked CH is wired to the
// battery charger, not to a GPIO, and the variant defines no LED_BUILTIN --
// so there is no /s/l here and nothing to show an IP address on. On WiFi that
// matters: a board that cannot tell you its address is a board you cannot
// talk to. It therefore prints the address once over USB serial at boot, and
// answers /enq with /enq/net carrying ip, rssi and port, so a client that can
// already reach it can ask where "here" is.
//
// FQBN: esp32:esp32:XIAO_ESP32C3 with STOCK DEFAULTS. Do not add
// :CDCOnBoot=cdc -- this variant already sets cdc_on_boot=1, and that option
// belongs to the generic esp32c3 devkit whose boards.txt defaults it to 0.
//
// ADDRESSES (ADDRESSES.md): the core /d /a /tone /s set is served by the USB
// twin; this one carries the network half.
//   /rate <ms>       state period, 20..2000; 0 STOPS. Echoed.
//   /state           -> /state <seq> <millis>, with /btn beside it
//   /btn             -> /btn <int>, 1 = BOOT (GPIO9) pressed
//   /enq             -> the greeting: name, /enq/btn 1, /enq/net <ip> <rssi> <port>
//
// STATUS: see the end of this header.

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>

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
// don't. __has_include means this still compiles with no secrets file at all,
// which is what CI and anyone cloning this gets.
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

#define OSC_PORT 8000
static const int PIN_BOOT_BTN = 9;       // D9 on this variant, active LOW

WiFiUDP   Udp;
WebServer http(80);

static int32_t  seq      = 0;
static uint32_t reportMs = 50;           // 0 stops (ADDRESSES.md)

// The state bundle, kept encoded so GET /state and the UDP reply are
// byte-identical rather than two encoders that might disagree.
static uint8_t stateBuf[96];
static size_t  stateLen = 0;

struct Capture : public Print {          // encode into a plain buffer
  uint8_t *b; size_t n, cap;
  size_t write(uint8_t c) override { if (n < cap) b[n++] = c; return 1; }
};

static void buildState() {
  OSCBundle b;
  b.add("/state").add((intOSC_t) seq).add((intOSC_t) millis());
  b.add("/btn").add((intOSC_t) (digitalRead(PIN_BOOT_BTN) == LOW ? 1 : 0));
  Capture cap; cap.b = stateBuf; cap.n = 0; cap.cap = sizeof stateBuf;
  b.send(cap);
  stateLen = cap.n;
}

static void routeRate(OSCMessage &m) {
  if (!(m.size() >= 1 && m.isInt(0))) return;
  const int32_t v = m.getInt(0);
  reportMs = (v <= 0) ? 0 : (uint32_t) constrain(v, 20, 2000);
}

// One entry point for both transports, so UDP and HTTP cannot drift apart.
static void handlePacket(const uint8_t *data, size_t len) {
  OSCMessage m;
  m.fill((uint8_t *) data, (int) len);
  if (m.hasError()) return;
  m.dispatch("/rate", routeRate);
}

static void cors() {
  http.sendHeader("Access-Control-Allow-Origin", "*");
  http.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  http.sendHeader("Access-Control-Allow-Headers", "content-type");
}

static void httpOsc() {
  cors();
  if (http.method() == HTTP_OPTIONS) { http.send(204); return; }
  http.send(200, "text/plain", "");
}

// The raw body handler. An OSC packet carries NUL bytes, so the body must be
// taken as bytes rather than as a string -- and WebServer hands it over in
// chunks, so it is accumulated here and dispatched once at RAW_END. The
// handler takes no argument and reads http.raw(); the four-argument on() form
// is what makes the server's canRaw() true.
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

static void httpState() {
  cors();
  http.send_P(200, "application/octet-stream", (const char *) stateBuf, stateLen);
}

// GET /enq answers the same OSC greeting every other transport gets, rather
// than a JSON shape of its own: one decoder on the page side, not one per
// board (ADDRESSES.md).
static void httpEnq() {
  cors();
  OSCBundle b;
  b.add("/enq").add("XiaoC3WiFi");
  b.add("/enq/btn").add((intOSC_t) 1);
  b.add("/enq/net").add(WiFi.localIP().toString().c_str())
                   .add((intOSC_t) WiFi.RSSI()).add((intOSC_t) OSC_PORT);
  static uint8_t buf[160];
  Capture cap; cap.b = buf; cap.n = 0; cap.cap = sizeof buf;
  b.send(cap);
  http.send_P(200, "application/octet-stream", (const char *) buf, cap.n);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  // Measured on the C6 twin, three runs each way: the ESP32's default modem
  // power save batches receives against the DTIM beacon, taking the UDP round
  // trip from a 10 ms median to 112 ms with a 819 ms worst case. The radio
  // costs more current without it; a control surface that stutters costs more.
  WiFi.setSleep(false);

  const uint32_t until = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < until) delay(200);

  if (WiFi.status() == WL_CONNECTED) {
    Udp.begin(OSC_PORT);
    // This board has no display, so the USB log is the only place the address
    // can appear. Without it you would have to scan the network to find it.
    Serial.print("OSC/UDP on ");
    Serial.print(WiFi.localIP());
    Serial.print(":");
    Serial.println(OSC_PORT);
  } else {
    Serial.println("WiFi association failed -- check arduino_secrets.h");
  }

  http.on("/osc",   HTTP_POST,    httpOsc, httpOscRaw);
  http.on("/osc",   HTTP_OPTIONS, httpOsc);
  http.on("/state", HTTP_GET,     httpState);
  http.on("/enq",   HTTP_GET,     httpEnq);
  http.begin();

  buildState();
}

void loop() {
  http.handleClient();

  uint8_t in[512];
  const int n = Udp.parsePacket();
  if (n > 0) {
    const int got = Udp.read(in, sizeof in);
    if (got > 0) {
      handlePacket(in, (size_t) got);
      // Reply to whoever asked, so nothing needs configuring at this end.
      if (stateLen) {
        Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        Udp.write(stateBuf, stateLen);
        Udp.endPacket();
      }
    }
  }

  static uint32_t last = 0;
  const uint32_t now = millis();
  if (reportMs != 0 && now - last >= reportMs) {
    last = now;
    seq++;
    buildState();
  }
}

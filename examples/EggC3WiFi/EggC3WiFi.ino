/*
 * EggC3WiFi — the EGG ESP32-C3 demo over WiFi: OSC on UDP, no SLIP.
 * -----------------------------------------------------------------------------
 * The twin of EggC3Oscuino. Same board, same measured hardware (0.42" OLED
 * at 0x3C on SDA=5/SCL=6, BOOT button GPIO9, plain active-low LED GPIO8),
 * same OSC vocabulary — only the transport differs, on the pattern of the
 * XiaoC6ExpOscuino / XiaoC6ExpWiFi pair (see that sketch's header for the
 * transport comparison table and why SLIP is deliberately absent here).
 *
 * Board : EGG ESP32-C3 (ESP32-C3, single core 160 MHz, 4 MB flash)
 * FQBN  : esp32:esp32:esp32c3:CDCOnBoot=cdc
 * Libs  : U8g2
 *
 * TWO WAYS IN, one OSC vocabulary:
 *   UDP port 8000    for real OSC software — replies go to whoever asked.
 *   HTTP port 80     for the browser page (browsers cannot send UDP):
 *                    POST raw OSC bytes to /osc, GET /state for the latest
 *                    /egg packet, GET /hello for board facts as JSON.
 *
 * ADDRESSES (matching the USB twin where they overlap):
 *   /egg/t <s> [...] up to 5 strings: lines on the OLED (replaces screen)
 *   /egg/net         back to the network panel (IP, port, RSSI)
 *   /egg/rate <ms>   state-packet period, 20..2000
 *   /s/l <int>       the LED (active low, measured)
 * State packet, sent as the UDP reply and served at GET /state:
 *   /egg <seq> <button> <millis> — seq advances only on the periodic tick.
 *
 * The OLED shows the IP once connected — on WiFi a board that cannot tell
 * you its address is a board you cannot talk to.
 *
 * The HTTP body must come through the WebServer's RAW path, not
 * http.arg("plain"): OSC packets contain NUL bytes and the String-based arg
 * parser truncates at the first one. That bug cost the XIAO twin its whole
 * browser path once; the four-argument http.on() below is the cure.
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <OSCMessage.h>

// ---------------------------------------------------------------------------
// CREDENTIALS LIVE OUTSIDE THIS FILE, and outside the repository.
//
//     cp arduino_secrets.h.example arduino_secrets.h   # then edit it
//
// arduino_secrets.h is git-ignored and a pre-commit hook rejects any attempt
// to add one. __has_include keeps this compiling with no secrets file, which
// is what CI and a fresh clone get.
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

static const int PIN_SDA = 5, PIN_SCL = 6;
static const int PIN_BOOT_BTN = 9;
static const int PIN_LED = 8;              // plain LED, ACTIVE LOW (measured)

U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
WiFiUDP Udp;
WebServer http(80);

static bool     dispOK   = false;
static int32_t  seq      = 0;
static uint32_t reportMs = 50;
static bool     showNet  = true;
static char     lines[5][16];
static int      nlines   = 0;

static void redraw() {
  if (!dispOK) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tr);
  if (showNet) {
    char b[16];
    oled.drawStr(0, 7, "EggC3 OSC/WiFi");
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(b, sizeof b, "%s", WiFi.localIP().toString().c_str());
      oled.drawStr(0, 16, b);
      snprintf(b, sizeof b, "udp %d", OSC_PORT);
      oled.drawStr(0, 25, b);
      snprintf(b, sizeof b, "%d dBm", (int)WiFi.RSSI());
      oled.drawStr(0, 34, b);
    } else {
      oled.drawStr(0, 16, "connecting...");
    }
  } else {
    for (int i = 0; i < nlines; i++)
      oled.drawStr(0, 8 * (i + 1) - 1, lines[i]);
  }
  oled.sendBuffer();
}

/* ------------------------------------------------------------- OSC handling */

static void routeText(OSCMessage &m) {
  nlines = 0;
  const int n = m.size() < 5 ? m.size() : 5;
  for (int i = 0; i < n; i++)
    if (m.isString(i)) {
      m.getString(i, lines[nlines], sizeof lines[0]);
      nlines++;
    }
  showNet = false;
  redraw();
}

static void routeNet(OSCMessage &) { showNet = true; redraw(); }

static void routeLed(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0))
    digitalWrite(PIN_LED, m.getInt(0) > 0 ? LOW : HIGH);   // active low
}

static void routeRate(OSCMessage &m) {
  if (m.size() >= 1 && m.isInt(0)) reportMs = constrain(m.getInt(0), 20, 2000);
}

// One entry point for both transports.
static void handlePacket(const uint8_t *data, size_t len) {
  OSCMessage m;
  m.fill((uint8_t *)data, (int)len);
  if (m.hasError()) return;
  m.dispatch("/egg/t",    routeText);
  m.dispatch("/egg/net",  routeNet);
  m.dispatch("/egg/rate", routeRate);
  m.dispatch("/s/l",      routeLed);
}

// The last state packet, kept encoded so GET /state and the UDP reply are
// byte-identical. seq advances only on the periodic tick in loop().
static uint8_t stateBuf[48];
static size_t  stateLen = 0;

static void buildState() {
  OSCMessage m("/egg");
  m.add((intOSC_t)seq)
   .add((intOSC_t)(digitalRead(PIN_BOOT_BTN) == LOW ? 1 : 0))
   .add((intOSC_t)millis());
  struct Cap : public Print {
    uint8_t *b; size_t n, cap;
    size_t write(uint8_t c) override { if (n < cap) b[n++] = c; return 1; }
  } cap;
  cap.b = stateBuf; cap.n = 0; cap.cap = sizeof stateBuf;
  m.send(cap);
  stateLen = cap.n;
}

/* ------------------------------------------------------------------- HTTP */

static void cors() { http.sendHeader("Access-Control-Allow-Origin", "*"); }

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

static void httpOsc() {
  cors();
  if (http.method() == HTTP_OPTIONS) {
    http.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    http.sendHeader("Access-Control-Allow-Headers", "content-type");
    http.send(204);
    return;
  }
  http.send(200, "text/plain", "ok");
}

static void httpState() {
  cors();
  buildState();
  http.send_P(200, "application/octet-stream", (const char *)stateBuf, stateLen);
}

static void httpHello() {
  cors();
  char j[128];
  snprintf(j, sizeof j,
           "{\"name\":\"EggC3WiFi\",\"display\":%s,\"ip\":\"%s\","
           "\"rssi\":%d,\"udp\":%d}",
           dispOK ? "true" : "false", WiFi.localIP().toString().c_str(),
           (int)WiFi.RSSI(), OSC_PORT);
  http.send(200, "application/json", j);
}

/* ------------------------------------------------------------------------- */

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);               // off (active low)

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.beginTransmission(0x3C);
  dispOK = (Wire.endTransmission() == 0);
  Wire.end();
  if (dispOK) {
    oled.begin();
    redraw();
  }

  WiFi.mode(WIFI_STA);
  // Modem power save OFF: with it on (the default), traffic batches to DTIM
  // beacons — measured on this board as 1015 ms ping RTT, 66 % ping loss and
  // HTTP connections timing out entirely, while UDP occasionally got
  // through. OSC is interactive; latency is the product.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    if (dispOK && (i % 4) == 0) redraw();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Udp.begin(OSC_PORT);
    http.on("/osc",   HTTP_POST,    httpOsc, httpOscRaw);
    http.on("/osc",   HTTP_OPTIONS, httpOsc);
    http.on("/state", HTTP_GET,     httpState);
    http.on("/hello", HTTP_GET,     httpHello);
    http.begin();
    Serial.print("OSC/UDP on ");
    Serial.print(WiFi.localIP());
    Serial.print(":");
    Serial.println(OSC_PORT);
  } else {
    Serial.println("WiFi did not connect -- check arduino_secrets.h");
  }
  redraw();
}

void loop() {
  http.handleClient();

  int avail = Udp.parsePacket();
  if (avail > 0) {
    static uint8_t in[512];
    const int n = Udp.read(in, avail > (int)sizeof in ? (int)sizeof in : avail);
    if (n > 0) {
      handlePacket(in, (size_t)n);
      buildState();
      if (stateLen) {
        Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        Udp.write(stateBuf, stateLen);
        Udp.endPacket();
      }
    }
  }

  const uint32_t now = millis();
  static uint32_t lastReport = 0, lastNet = 0;
  if (now - lastReport >= reportMs) {
    lastReport = now;
    seq++;
    buildState();
  }
  if (showNet && now - lastNet >= 1000) {
    lastNet = now;
    redraw();
  }
}

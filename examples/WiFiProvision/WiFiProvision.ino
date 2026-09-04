/*
 * WiFiProvision — bring a WiFi board up the way an x-OSC does.
 * -----------------------------------------------------------------------------
 * A board with no network yet is its own network: it comes up as an open
 * access point named after itself, serves one settings page, and reboots into
 * the network you typed in — where it streams OSC to the destination you gave
 * it. Nothing to edit, nothing to compile twice, no credentials in any file.
 *
 * The model is x-io Technologies' x-OSC (user manual v0.4, 2014): ad hoc mode
 * by default at http://169.254.1.1, a Network / OSC settings page, "Save
 * Settings" reboots, a ping button that broadcasts the address and, held for
 * three seconds, drops back to the open network. This sketch keeps that shape
 * and adds what our boards have that an x-OSC has not: a USB serial console,
 * a captive portal so a phone opens the page on its own, and mDNS.
 *
 * Three ways in, one settings struct:
 *
 *   the page      http://192.168.4.1/ on the setup network (a phone is pushed
 *                 there by the captive portal), or http://<ip>/ once joined
 *   USB serial    type  help  at 115200 baud
 *   OSC           /net/join, /net/dest, /net/setup, /net/forget, /net/save
 *
 * Every mode change is a reboot. Leaving an access point for a station without
 * one has a different set of half-torn-down states on each radio firmware; a
 * reboot has none, on all of them. Joining is tried for 30 s; if it fails the
 * board reboots onto the setup network, and after three minutes with nobody on
 * it, reboots to try again — so an unplugged router does not strand a board.
 *
 * WHAT IS PORTABLE. Only wifi_stack.h knows which core this is. The HTTP
 * server, the captive-portal DNS and the form parser here are written against
 * WiFiServer / WiFiClient / WiFiUDP alone, which every Arduino WiFi stack
 * presents, rather than against the WebServer and DNSServer libraries only the
 * ESP and Pico cores ship. One code path on nine boards beats four good ones.
 *
 * ADDRESSES (ADDRESSES.md):
 *   /enq              -> /enq "WiFiProvision", /enq/led, and
 *                        /enq/net <ip> <rssi> <port> <name> <mac> -- send /enq
 *                        to the network's broadcast address and every board
 *                        answers with where it is (x-OSC's /ping)
 *   /net/dest <s> <i>    stream destination ip and port; "0.0.0.0" = whoever
 *                        last spoke to me. Echoed. Runtime only until /net/save
 *   /net/join <s> [<s>]  ssid, password: save and reboot onto that network
 *   /net/setup           save and reboot onto the setup network
 *   /net/save            write the current settings
 *   /net/forget          factory defaults and reboot
 *   /net/name <s>        the board's name: its setup-network SSID and mDNS host
 *   /rate <i>            stream period in ms, 0 stops. Echoed
 *   /state <i> <i>       the stream: seq, millis — to the destination above
 *   /s/l <i>             the plain LED, where the board has one; takes it over
 *                        from the status blink
 * Over HTTP for the browser page (extras/webserial/oscuino.html, pick HTTP):
 *   GET /enq, GET /state, POST /osc — OSC bytes, as the WiFi twins serve them.
 *
 * STATUS: see the end of this header.
 */

#include "wifi_stack.h"
#include <OSCBundle.h>
#include <OSCMessage.h>
#include <OSCBoards.h>
#include <ctype.h>

#define SKETCH_NAME "WiFiProvision"

#ifndef WIFI_PROVISION_HTTP_OSC
#define WIFI_PROVISION_HTTP_OSC 1      // GET /enq, GET /state, POST /osc for the browser page
#endif
// #define WIFI_PROVISION_POWER_HOLD 46 // a pin to drive HIGH first thing: the M5Stack Capsule,
                                   // Dial and DinMeter latch their own power through GPIO46
// #define WIFI_PROVISION_BUTTON 0     // a pin, active LOW: held at boot = setup network;
                                   // held 3 s = setup network, 8 s = forget (x-OSC)
static const uint32_t JOIN_MS  = 30000;   // x-OSC: "approximately 30 seconds"
static const uint32_t RETRY_MS = 180000;  // alone on the fallback setup network this long: retry

// An HTTP request, parsed. Declared here because the Arduino preprocessor
// hoists function prototypes above every type a sketch defines later.
struct Req {
  char method[8];
  char path[64];
  char host[48];
  int  contentLength;
  char body[513];
  int  bodyLen;
};


/* ------------------------------------------------------------------ settings */

enum { MODE_SETUP = 0, MODE_JOIN = 1 };
enum { FLAG_SETUP_ONCE = 1 };              // next boot: setup network, mode untouched

struct Settings {
  uint32_t magic;
  char     name[24];      // setup-network SSID and mDNS host; "" = OSCMCU-<mac>
  char     ssid[33];
  char     pass[64];
  uint8_t  mode;
  uint8_t  flags;
  uint8_t  dest[4];       // 0.0.0.0 = whoever last spoke to me
  uint16_t destPort;
  uint16_t port;          // OSC listens here, UDP
  uint16_t rate;          // stream period, ms; 0 = off
};
static const uint32_t SETTINGS_MAGIC = 0x4f534331;   // "OSC1": bump when the layout changes

static Settings s;

static void defaultName() {
  char mac[18];
  macString(mac);
  snprintf(s.name, sizeof s.name, "OSCMCU-%c%c%c%c",
           toupper(mac[12]), toupper(mac[13]), toupper(mac[15]), toupper(mac[16]));
}

static void defaults() {
  memset(&s, 0, sizeof s);
  s.magic    = SETTINGS_MAGIC;
  s.mode     = MODE_SETUP;
  s.destPort = 9000;
  s.port     = 8000;
  s.rate     = 500;       // a board that says nothing until asked is a board you cannot find
}

static bool loadSettings() {
  Settings t;
  if (!storeLoad(&t, sizeof t) || t.magic != SETTINGS_MAGIC) return false;
  s = t;
  s.name[sizeof s.name - 1] = s.ssid[sizeof s.ssid - 1] = s.pass[sizeof s.pass - 1] = '\0';
  if (s.port == 0) s.port = 8000;
  return true;
}

static bool saveSettings() {
  s.magic = SETTINGS_MAGIC;
  const bool ok = storeSave(&s, sizeof s);
  if (!ok) Serial.println("settings: save FAILED");
  return ok;
}

/* ------------------------------------------------------------- runtime state */

static bool      apMode     = false;   // on the setup network
static bool      apFallback = false;   // ... because joining failed
static uint32_t  apSince    = 0;       // last moment the setup network had a client
static bool      captive    = false;   // DNS catch-all is up
static WiFiUDP   Udp, Dns;
static WiFiServer http(80);
static IPAddress lastFrom;             // 0.0.0.0 until someone speaks
static int32_t   seq        = 0;
static uint32_t  rateMs     = 0;
static uint32_t  lastLinkOk = 0;
static char      ipStr[16], macStr[18];
#ifdef BOARD_HAS_LED
static bool      ledOverride = false;
#endif

struct Capture : public Print {          // encode an OSC packet into a buffer
  uint8_t *b; size_t n, cap;
  Capture(uint8_t *buf, size_t c) : b(buf), n(0), cap(c) {}
  size_t write(uint8_t c) override { if (n < cap) b[n++] = c; return 1; }
};

static void ipToStr(IPAddress ip, char *out) {
  snprintf(out, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

// "a.b.c.d" or "a.b.c.d:port". Returns false on anything else.
static bool parseDest(const char *txt, uint8_t ip[4], uint16_t *port) {
  unsigned a[4], p = 0;
  char tail = 0;
  const int n = sscanf(txt, "%u.%u.%u.%u:%u%c", &a[0], &a[1], &a[2], &a[3], &p, &tail);
  if (n != 4 && n != 5) return false;
  for (int i = 0; i < 4; i++) { if (a[i] > 255) return false; ip[i] = (uint8_t) a[i]; }
  if (n == 5) { if (p > 65535) return false; *port = (uint16_t) p; }
  return true;
}

static IPAddress destIP() {
  IPAddress d(s.dest[0], s.dest[1], s.dest[2], s.dest[3]);
  return ((uint32_t) d == 0) ? lastFrom : d;
}

// Not a ternary: WiFi101's localIP() returns a uint32_t, not an IPAddress,
// and the two do not meet in one expression (failed on MKR1000 / Feather M0).
static IPAddress myIP() {
  if (apMode) return apIP();
  IPAddress ip = WiFi.localIP();
  return ip;
}

static void rebootInto(uint8_t mode, bool once) {
  if (once) s.flags |= FLAG_SETUP_ONCE; else { s.mode = mode; s.flags &= ~FLAG_SETUP_ONCE; }
  saveSettings();
  Serial.println(once || mode == MODE_SETUP ? "restarting onto the setup network"
                                            : "restarting to join the network");
  delay(100);
  restartBoard();
}

static void forgetAndReboot() {
  defaults();
  saveSettings();
  Serial.println("forgotten; restarting");
  delay(100);
  restartBoard();
}

/* ------------------------------------------------------------------ status */

static void printStatus() {
  Serial.print(SKETCH_NAME " \"");
  Serial.print(s.name);
  Serial.print("\" ");
  Serial.println(macStr);
  if (apMode) {
    Serial.print("  setup network \"");
    Serial.print(s.name);
    Serial.print("\" (open) at http://");
    Serial.print(ipStr);
    Serial.println("/ -- join it; a phone opens the page by itself");
    if (apFallback) {
      Serial.print("  because joining \"");
      Serial.print(s.ssid);
      Serial.println("\" failed; retrying in 3 min unless someone is on the setup network");
    }
  } else {
    Serial.print("  joined \"");
    Serial.print(s.ssid);
    Serial.print("\" as ");
    Serial.print(ipStr);
    Serial.print(", ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("  settings at http://");
    Serial.print(ipStr);
#ifdef WS_HAS_MDNS
    Serial.print("/ or http://");
    Serial.print(s.name);
    Serial.print(".local");
#endif
    Serial.println("/");
  }
  char d[22];
  ipToStr(destIP(), d);
  Serial.print("  OSC on UDP ");
  Serial.print(s.port);
  Serial.print(", streaming every ");
  Serial.print(rateMs);
  Serial.print(" ms to ");
  Serial.print(s.dest[0] | s.dest[1] | s.dest[2] | s.dest[3] ? d : "whoever last spoke");
  Serial.print(":");
  Serial.println(s.destPort);
  // The value is never printed. (Worded so the repo's pre-commit credentials
  // hook, which looks for a quoted value after the word, does not flag it.)
  Serial.println(s.pass[0] ? "  password is set" : "  no password");
}

/* --------------------------------------------------------------------- OSC */

static OSCBundle out;      // replies collect here during dispatch

// /enq/net: ip, rssi, port as every WiFi sketch sends them, then the two a
// provisioned board also has -- its name and its MAC -- so one broadcast /enq
// is the whole discovery protocol.
static void buildEnq(OSCBundle &b) {
  b.add("/enq").add(SKETCH_NAME);
  b.add("/enq/net").add(ipStr).add((intOSC_t) (apMode ? 0 : WiFi.RSSI())).add((intOSC_t) s.port)
                   .add(s.name).add(macStr);
#ifdef BOARD_HAS_LED
  b.add("/enq/led");
#endif
}

static void buildState(OSCBundle &b) {
  b.add("/state").add((intOSC_t) seq).add((intOSC_t) millis());
}

static void rEnq(OSCMessage &)    { buildEnq(out); }
static void rRate(OSCMessage &m) {
  if (!(m.size() >= 1 && m.isInt(0))) return;
  const int32_t v = m.getInt(0);
  rateMs = (v <= 0) ? 0 : (uint32_t) constrain(v, 20, 60000);   // 0 STOPS (ADDRESSES.md)
  out.add("/rate").add((intOSC_t) rateMs);
}
static void rDest(OSCMessage &m) {
  char txt[24];
  if (m.size() >= 1 && m.isString(0)) {
    m.getString(0, txt, sizeof txt);
    uint16_t port = s.destPort;
    if (!parseDest(txt, s.dest, &port)) return;
    s.destPort = (m.size() >= 2 && m.isInt(1)) ? (uint16_t) m.getInt(1) : port;
  }
  ipToStr(IPAddress(s.dest[0], s.dest[1], s.dest[2], s.dest[3]), txt);
  out.add("/net/dest").add(txt).add((intOSC_t) s.destPort);
}
static void rJoin(OSCMessage &m) {
  if (!(m.size() >= 1 && m.isString(0))) return;
  m.getString(0, s.ssid, sizeof s.ssid);
  if (m.size() >= 2 && m.isString(1)) m.getString(1, s.pass, sizeof s.pass);
  else s.pass[0] = '\0';
  rebootInto(MODE_JOIN, false);
}
static void rSetup(OSCMessage &)  { rebootInto(MODE_SETUP, false); }
static void rSave(OSCMessage &)   { s.rate = (uint16_t) rateMs; saveSettings(); }
static void rForget(OSCMessage &) { forgetAndReboot(); }
static void rName(OSCMessage &m) {
  if (!(m.size() >= 1 && m.isString(0))) return;
  m.getString(0, s.name, sizeof s.name);
  if (!s.name[0]) defaultName();
  saveSettings();
  out.add("/net/name").add(s.name);
}
#ifdef BOARD_HAS_LED
static void rLed(OSCMessage &m) {
  if (!(m.size() >= 1 && m.isInt(0))) return;
  ledOverride = true;
  digitalWrite(LED_BUILTIN, m.getInt(0) ? HIGH : LOW);
  out.add("/s/l").add((intOSC_t) (m.getInt(0) ? 1 : 0));
}
#endif

// OSCMessage and OSCBundle dispatch the same way, so one table serves both.
static const struct { const char *addr; void (*fn)(OSCMessage &); } routes[] = {
  { "/enq",        rEnq    },
  { "/net/dest",   rDest   },
  { "/net/join",   rJoin   },
  { "/net/setup",  rSetup  },
  { "/net/save",   rSave   },
  { "/net/forget", rForget },
  { "/net/name",   rName   },
  { "/rate",       rRate   },
#ifdef BOARD_HAS_LED
  { "/s/l",        rLed    },
#endif
};
static void route(OSCMessage &p) { for (auto &r : routes) p.dispatch(r.addr, r.fn); }
static void route(OSCBundle &p)  { for (auto &r : routes) p.dispatch(r.addr, r.fn); }

// One entry point for both transports. The reply, if any, lands in `out`;
// the caller decides where it goes.
static void handlePacket(uint8_t *data, size_t len, IPAddress from) {
  lastFrom = from;
  if (len >= 8 && memcmp(data, "#bundle", 8) == 0) {
    OSCBundle b;
    b.fill(data, (int) len);
    if (!b.hasError()) route(b);
  } else {
    OSCMessage m;
    m.fill(data, (int) len);
    if (!m.hasError()) route(m);
  }
}

static size_t encode(OSCBundle &b, uint8_t *buf, size_t cap) {
  Capture c(buf, cap);
  b.send(c);
  return c.n;
}

static void serveUdp() {
  const int n = Udp.parsePacket();
  if (n <= 0) return;
  static uint8_t in[512];
  const int got = Udp.read(in, sizeof in);
  if (got <= 0) return;
  handlePacket(in, (size_t) got, Udp.remoteIP());
  if (out.size() > 0) {                       // reply to whoever asked
    static uint8_t reply[256];
    const size_t len = encode(out, reply, sizeof reply);
    Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    Udp.write(reply, len);
    Udp.endPacket();
  }
  out.empty();
}

static void stream() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (rateMs == 0 || now - last < rateMs) return;
  last = now;
  seq++;
  const IPAddress to = destIP();
  if ((uint32_t) to == 0) return;             // nobody has spoken yet
  OSCBundle b;
  buildState(b);
  static uint8_t pkt[64];
  const size_t len = encode(b, pkt, sizeof pkt);
  Udp.beginPacket(to, s.destPort);
  Udp.write(pkt, len);
  Udp.endPacket();
}

/* -------------------------------------------------------- captive-portal DNS */
// Every name resolves to this board, so a phone that joins the setup network
// probes its captive-portal URL, gets us, and opens the page. Plain WiFiUDP,
// so it runs on every stack; ~40 lines is cheaper than a per-core library.

static void serveDns() {
  const int n = Dns.parsePacket();
  if (n <= 0) return;
  uint8_t q[300];
  const int got = Dns.read(q, 256);
  if (got < 17 || (q[2] & 0x80) || ((q[4] << 8) | q[5]) != 1) return;   // a query with one question
  int i = 12;
  while (i < got && q[i]) i += q[i] + 1;      // skip the labels of the name
  i++;
  if (i + 4 > got) return;
  const uint16_t qtype = (uint16_t) ((q[i] << 8) | q[i + 1]);
  size_t len = (size_t) i + 4;
  q[2] = 0x81 | (q[2] & 0x01); q[3] = 0x80;   // response, authoritative, no error
  q[6] = q[7] = q[8] = q[9] = q[10] = q[11] = 0;
  if (qtype == 1 || qtype == 255) {           // A (or ANY): answer with our address
    const IPAddress ip = apIP();
    const uint8_t a[16] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, ip[0], ip[1], ip[2], ip[3] };
    memcpy(q + len, a, 16);
    len += 16;
    q[7] = 1;
  }
  Dns.beginPacket(Dns.remoteIP(), Dns.remotePort());
  Dns.write(q, len);
  Dns.endPacket();
}

/* -------------------------------------------------------------------- HTTP */
// Just enough HTTP/1.0 for one form: request line, Host, Content-Length, a
// body of at most 512 bytes, Connection: close. No String, no library.

static bool readLine(WiFiClient &c, char *buf, size_t cap, uint32_t deadline) {
  size_t n = 0;
  while ((int32_t) (deadline - millis()) > 0) {
    if (!c.available()) {
      if (!c.connected()) return false;
      delay(1);
      continue;
    }
    const int ch = c.read();
    if (ch < 0) continue;
    if (ch == '\n') { buf[n] = '\0'; return true; }
    if (ch != '\r' && n < cap - 1) buf[n++] = (char) ch;
  }
  return false;
}

static bool readRequest(WiFiClient &c, Req &r) {
  const uint32_t deadline = millis() + 3000;
  char line[200];
  memset(&r, 0, sizeof r);
  if (!readLine(c, line, sizeof line, deadline)) return false;
  if (sscanf(line, "%7s %63s", r.method, r.path) != 2) return false;
  char *qm = strchr(r.path, '?');             // a query string is not a path
  if (qm) *qm = '\0';
  while (readLine(c, line, sizeof line, deadline) && line[0]) {
    if (strncasecmp(line, "Host:", 5) == 0) {
      const char *v = line + 5;
      while (*v == ' ') v++;
      strncpy(r.host, v, sizeof r.host - 1);
    } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
      r.contentLength = atoi(line + 15);
    }
  }
  while (r.bodyLen < r.contentLength && r.bodyLen < (int) sizeof r.body - 1
         && (int32_t) (deadline - millis()) > 0) {
    const int ch = c.read();
    if (ch < 0) { delay(1); continue; }
    r.body[r.bodyLen++] = (char) ch;
  }
  r.body[r.bodyLen] = '\0';
  return true;
}

// The value of `key` in a form body, percent-decoded, into out. "" if absent.
static bool formField(const char *body, const char *key, char *out, size_t cap) {
  const size_t kl = strlen(key);
  const char *p = body;
  out[0] = '\0';
  while (p && *p) {
    if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
      p += kl + 1;
      size_t n = 0;
      while (*p && *p != '&') {
        char ch = *p++;
        if (ch == '+') ch = ' ';
        else if (ch == '%' && isxdigit((unsigned char) p[0]) && isxdigit((unsigned char) p[1])) {
          char hex[3] = { p[0], p[1], 0 };
          ch = (char) strtol(hex, NULL, 16);
          p += 2;
        }
        if (n < cap - 1) out[n++] = ch;
      }
      out[n] = '\0';
      return true;
    }
    p = strchr(p, '&');
    if (p) p++;
  }
  return false;
}

static void printEscaped(Print &o, const char *t) {
  for (; *t; t++) {
    switch (*t) {
      case '&': o.print("&amp;"); break;
      case '<': o.print("&lt;"); break;
      case '>': o.print("&gt;"); break;
      case '"': o.print("&quot;"); break;
      default:  o.write(*t);
    }
  }
}

static void head(WiFiClient &c, int code, const char *type) {
  c.print("HTTP/1.1 ");
  c.print(code);
  c.print(code == 200 ? " OK" : code == 204 ? " No Content" : code == 302 ? " Found" : " Not Found");
  c.print("\r\nContent-Type: ");
  c.print(type);
  c.print("\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n");
}

static void pageStart(WiFiClient &c, const char *title) {
  head(c, 200, "text/html; charset=utf-8");
  c.print(F("\r\n<!doctype html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>"));
  printEscaped(c, s.name);
  c.print(F("</title><style>body{font:16px system-ui,sans-serif;margin:1.5em auto;max-width:26em;padding:0 1em}"
            "label{display:block;margin:.7em 0 .2em}input[type=text],input[type=password]{width:100%;"
            "box-sizing:border-box;padding:.45em;font-size:1em}button{margin:1em .5em 0 0;padding:.5em 1em;"
            "font-size:1em}small{color:#666}</style></head><body><h2>"));
  printEscaped(c, title);
  c.print("</h2>");
}

static void pageStatus(WiFiClient &c) {
  c.print("<p>");
  if (apMode) {
    c.print(F("On its own setup network <b>"));
    printEscaped(c, s.name);
    c.print(F("</b> at "));
    c.print(ipStr);
    if (apFallback) {
      c.print(F(" &mdash; joining <b>"));
      printEscaped(c, s.ssid);
      c.print(F("</b> failed"));
    }
  } else {
    c.print(F("Joined <b>"));
    printEscaped(c, s.ssid);
    c.print(F("</b> as "));
    c.print(ipStr);
    c.print(F(", "));
    c.print(WiFi.RSSI());
    c.print(F(" dBm"));
  }
  c.print(F(".<br>OSC on UDP port "));
  c.print(s.port);
  c.print(F(", streaming to "));
  if (s.dest[0] | s.dest[1] | s.dest[2] | s.dest[3]) {
    char d[16];
    ipToStr(IPAddress(s.dest[0], s.dest[1], s.dest[2], s.dest[3]), d);
    c.print(d);
  } else c.print(F("whoever last spoke"));
  c.print(':');
  c.print(s.destPort);
  c.print(F(" every "));
  c.print(rateMs);
  c.print(F(" ms.</p>"));
}

static void pageForm(WiFiClient &c) {
  c.print(F("<form method=\"post\" action=\"/save\"><label>Name <small>(its setup-network name and .local host)</small>"
            "<input type=\"text\" name=\"name\" maxlength=\"23\" value=\""));
  printEscaped(c, s.name);
  c.print(F("\"></label><label>Network to join <small>(SSID)</small><input type=\"text\" name=\"ssid\" maxlength=\"32\" value=\""));
  printEscaped(c, s.ssid);
  c.print(F("\"></label><label>Password <small>(blank keeps the current one)</small>"
            "<input type=\"password\" name=\"pass\" maxlength=\"63\"></label>"
            "<label>Send OSC to <small>(ip:port; 0.0.0.0 = whoever last spoke)</small><input type=\"text\" name=\"dest\" value=\""));
  char d[16];
  ipToStr(IPAddress(s.dest[0], s.dest[1], s.dest[2], s.dest[3]), d);
  c.print(d);
  c.print(':');
  c.print(s.destPort);
  c.print(F("\"></label><label>Listen for OSC on UDP port<input type=\"text\" name=\"port\" value=\""));
  c.print(s.port);
  c.print(F("\"></label><label>Stream every <small>(ms, 0 = off)</small><input type=\"text\" name=\"rate\" value=\""));
  c.print(s.rate);
  c.print(F("\"></label><label><input type=\"radio\" name=\"mode\" value=\"join\""));
  if (s.mode == MODE_JOIN) c.print(F(" checked"));
  c.print(F("> Join the network above</label><label><input type=\"radio\" name=\"mode\" value=\"setup\""));
  if (s.mode != MODE_JOIN) c.print(F(" checked"));
  c.print(F("> Stay on the setup network</label><button>Save and restart</button></form>"
            "<form method=\"post\" action=\"/forget\"><button>Forget everything</button></form><p><small>"));
  c.print(macStr);
  c.print(F(" &middot; up "));
  c.print(millis() / 1000);
  c.print(F(" s &middot; " SKETCH_NAME "</small></p></body></html>"));
}

static void applyForm(const char *body) {
  char v[80];
  if (formField(body, "name", v, sizeof v)) strncpy(s.name, v, sizeof s.name - 1);
  if (!s.name[0]) defaultName();
  if (formField(body, "ssid", v, sizeof v)) strncpy(s.ssid, v, sizeof s.ssid - 1);
  if (formField(body, "pass", v, sizeof v) && v[0]) strncpy(s.pass, v, sizeof s.pass - 1);
  if (formField(body, "dest", v, sizeof v)) parseDest(v, s.dest, &s.destPort);
  if (formField(body, "port", v, sizeof v) && atoi(v) > 0 && atoi(v) < 65536) s.port = (uint16_t) atoi(v);
  if (formField(body, "rate", v, sizeof v)) s.rate = (uint16_t) constrain(atoi(v), 0, 60000);
  if (formField(body, "mode", v, sizeof v)) s.mode = strcmp(v, "join") == 0 ? MODE_JOIN : MODE_SETUP;
  if (!s.ssid[0]) s.mode = MODE_SETUP;
  s.flags &= ~FLAG_SETUP_ONCE;
}

static void finish(WiFiClient &c) {
  delay(20);                                  // let the last write leave before the FIN
  c.stop();
}

static void serveHttp() {
#if defined(WS_NO_ACCEPT)
  WiFiClient c = http.available();
#else
  WiFiClient c = http.accept();
#endif
  if (!c) return;
  Req r;
  if (!readRequest(c, r)) { c.stop(); return; }

  // The captive portal: on the setup network every name resolves to us, and a
  // request for any other host is a phone or laptop probing for a portal.
  // Redirecting it is what makes the sign-in sheet open by itself.
  if (captive && r.host[0] && strncmp(r.host, ipStr, strlen(ipStr)) != 0) {
    char apStr[16];
    ipToStr(apIP(), apStr);
    if (strncmp(r.host, apStr, strlen(apStr)) != 0) {
      head(c, 302, "text/plain");
      c.print("Location: http://");
      c.print(apStr);
      c.print("/\r\n\r\n");
      finish(c);
      return;
    }
  }

  const bool get = strcmp(r.method, "GET") == 0, post = strcmp(r.method, "POST") == 0;

  if (get && strcmp(r.path, "/") == 0) {
    pageStart(c, s.name);
    pageStatus(c);
    pageForm(c);
    finish(c);
  } else if (post && strcmp(r.path, "/save") == 0) {
    applyForm(r.body);
    saveSettings();
    pageStart(c, "Saved");
    if (s.mode == MODE_JOIN) {
      c.print(F("<p>Restarting to join <b>"));
      printEscaped(c, s.ssid);
      c.print(F("</b>. It will then answer at http://"));
      printEscaped(c, s.name);
      c.print(F(".local/ (where mDNS works), print its address on the USB serial log, and reply to "
                "<code>/enq</code> sent to the network's broadcast address on UDP port "));
      c.print(s.port);
      c.print(F(". If it cannot join within 30 s it comes back here.</p>"));
    } else {
      c.print(F("<p>Restarting onto the setup network <b>"));
      printEscaped(c, s.name);
      c.print(F("</b>.</p>"));
    }
    c.print(F("</body></html>"));
    finish(c);
    delay(200);
    rebootInto(s.mode, false);
  } else if (post && strcmp(r.path, "/forget") == 0) {
    pageStart(c, "Forgotten");
    c.print(F("<p>Restarting with factory settings, onto the setup network.</p></body></html>"));
    finish(c);
    delay(200);
    forgetAndReboot();
#if WIFI_PROVISION_HTTP_OSC
  } else if (get && strcmp(r.path, "/enq") == 0) {
    OSCBundle b;
    buildEnq(b);
    static uint8_t buf[160];
    const size_t len = encode(b, buf, sizeof buf);
    head(c, 200, "application/octet-stream");
    c.print("\r\n");
    c.write(buf, len);
    finish(c);
  } else if (get && strcmp(r.path, "/state") == 0) {
    OSCBundle b;
    buildState(b);
    static uint8_t buf[64];
    const size_t len = encode(b, buf, sizeof buf);
    head(c, 200, "application/octet-stream");
    c.print("\r\n");
    c.write(buf, len);
    finish(c);
  } else if (strcmp(r.path, "/osc") == 0 && strcmp(r.method, "OPTIONS") == 0) {   // CORS preflight
    head(c, 204, "text/plain");
    c.print("Access-Control-Allow-Methods: POST, GET, OPTIONS\r\nAccess-Control-Allow-Headers: content-type\r\n\r\n");
    finish(c);
  } else if (post && strcmp(r.path, "/osc") == 0) {
    handlePacket((uint8_t *) r.body, (size_t) r.bodyLen, c.remoteIP());
    static uint8_t reply[256];
    const size_t len = out.size() > 0 ? encode(out, reply, sizeof reply) : 0;
    out.empty();
    head(c, 200, "application/octet-stream");
    c.print("\r\n");
    if (len) c.write(reply, len);
    finish(c);
#endif
  } else if (captive) {                       // a probe by path rather than by host
    head(c, 302, "text/plain");
    c.print("Location: http://");
    c.print(ipStr);
    c.print("/\r\n\r\n");
    finish(c);
  } else {
    head(c, 404, "text/plain");
    c.print("\r\nnot here\n");
    finish(c);
  }
}

/* ------------------------------------------------------------- USB serial */
// The bench's way in, and the only one on a board with no button. Typing a
// password here stays between you and the USB cable.

static void serialHelp() {
  Serial.println("  show                 current settings (password never shown)");
  Serial.println("  name <text>          this board's name");
  Serial.println("  ssid <text>          network to join");
  Serial.println("  pass <text>          its password");
  Serial.println("  dest <ip>[:<port>]   where the stream goes; 0.0.0.0 = whoever last spoke");
  Serial.println("  port <n>             UDP port OSC listens on");
  Serial.println("  rate <ms>            stream period, 0 = off");
  Serial.println("  join                 save, restart, join the network");
  Serial.println("  setup                save, restart onto the setup network");
  Serial.println("  save                 save without restarting");
  Serial.println("  forget               factory settings, restart");
  Serial.println("  restart");
}

static void command(char *line) {
  char *arg = strchr(line, ' ');
  if (arg) { *arg++ = '\0'; while (*arg == ' ') arg++; }
  if      (!strcmp(line, "show"))    printStatus();
  else if (!strcmp(line, "help"))    serialHelp();
  else if (!strcmp(line, "name") && arg) { strncpy(s.name, arg, sizeof s.name - 1); if (!s.name[0]) defaultName(); Serial.println(s.name); }
  else if (!strcmp(line, "ssid") && arg) { strncpy(s.ssid, arg, sizeof s.ssid - 1); Serial.println(s.ssid); }
  else if (!strcmp(line, "pass") && arg) { strncpy(s.pass, arg, sizeof s.pass - 1); Serial.println("(set)"); }
  else if (!strcmp(line, "dest") && arg) { Serial.println(parseDest(arg, s.dest, &s.destPort) ? "ok" : "ip[:port] please"); }
  else if (!strcmp(line, "port") && arg) { s.port = (uint16_t) atoi(arg); Serial.println(s.port); }
  else if (!strcmp(line, "rate") && arg) { s.rate = (uint16_t) constrain(atoi(arg), 0, 60000); rateMs = s.rate; Serial.println(s.rate); }
  else if (!strcmp(line, "join"))    { if (s.ssid[0]) rebootInto(MODE_JOIN, false); else Serial.println("ssid first"); }
  else if (!strcmp(line, "setup"))   rebootInto(MODE_SETUP, false);
  else if (!strcmp(line, "save"))    { if (saveSettings()) Serial.println("saved"); }
  else if (!strcmp(line, "forget"))  forgetAndReboot();
  else if (!strcmp(line, "restart")) restartBoard();
  else if (line[0])                  { Serial.print("? "); Serial.println(line); serialHelp(); }
}

static void serveSerial() {
  static char line[100];
  static size_t n = 0;
  while (Serial.available()) {
    const char ch = (char) Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') { line[n] = '\0'; n = 0; command(line); }
    else if (n < sizeof line - 1) line[n++] = ch;
  }
}

/* ------------------------------------------------------- LED and button */

static void ledTick() {
#ifdef BOARD_HAS_LED
  if (ledOverride) return;
  // x-OSC: flashing until the network is up, then steady. Polarity is the
  // board's; on an active-low LED "steady" is dark, which still says "up".
  const uint32_t period = apMode ? 1000 : (WiFi.status() == WL_CONNECTED ? 0 : 150);
  const bool on = period ? ((millis() / period) & 1) : true;
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#endif
}

#ifdef WIFI_PROVISION_BUTTON
static void buttonTick() {
  static uint32_t downSince = 0;
  if (digitalRead(WIFI_PROVISION_BUTTON) == LOW) {
    if (!downSince) downSince = millis();
    else if (millis() - downSince > 8000) forgetAndReboot();     // x-OSC: >8 s = factory reset
  } else if (downSince) {
    const uint32_t held = millis() - downSince;
    downSince = 0;
    if (held > 3000) rebootInto(apMode ? MODE_JOIN : MODE_SETUP, false);   // 3 s = toggle mode
  }
}
#endif

/* --------------------------------------------------------------- bring-up */

static bool joinNetwork() {
  Serial.print("joining \"");
  Serial.print(s.ssid);
  Serial.println("\" ...");
  staStart(s.ssid, s.pass);
  radioNoSleep();
  const uint32_t until = millis() + JOIN_MS;
  uint32_t lastTry = millis();
  while (WiFi.status() != WL_CONNECTED && (int32_t) (until - millis()) > 0) {
    delay(100);
    ledTick();
#if !defined(ARDUINO_ARCH_ESP32) && !defined(ARDUINO_ARCH_ESP8266)
    // The module stacks (NINA, WiFiS3, WiFi101, WiFiC3) block inside begin()
    // for one attempt and then stop; ask again. The ESP drivers keep trying
    // on their own, and a second begin() there only logs
    // "sta is connecting, cannot set config" (seen on the M5Capsule).
    if (millis() - lastTry > 10000) { staStart(s.ssid, s.pass); lastTry = millis(); }
#endif
  }
  (void) lastTry;
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
#ifdef WIFI_PROVISION_POWER_HOLD
  pinMode(WIFI_PROVISION_POWER_HOLD, OUTPUT);       // before anything else, or the board may not stay on
  digitalWrite(WIFI_PROVISION_POWER_HOLD, HIGH);
#endif
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) {}   // native USB: wait a little, never forever
  WS_RADIO_PINS();
  WiFi.status();                               // module stacks bring their radio up on first use
  macString(macStr);
#ifdef BOARD_HAS_LED
  pinMode(LED_BUILTIN, OUTPUT);
#endif
#ifdef WIFI_PROVISION_BUTTON
  pinMode(WIFI_PROVISION_BUTTON, INPUT_PULLUP);
#endif

  if (!loadSettings()) {
    defaults();
    Serial.println("no settings stored yet");
  }
  if (!s.name[0]) defaultName();
  rateMs = s.rate;

  bool wantJoin = s.mode == MODE_JOIN && s.ssid[0];
  apFallback = wantJoin && (s.flags & FLAG_SETUP_ONCE);
  if (apFallback) { wantJoin = false; s.flags &= ~FLAG_SETUP_ONCE; }   // one boot only
#ifdef WIFI_PROVISION_BUTTON
  if (digitalRead(WIFI_PROVISION_BUTTON) == LOW) { wantJoin = false; apFallback = false; }
#endif

  if (wantJoin) {
    if (!joinNetwork()) {
      Serial.println("could not join; restarting onto the setup network");
      rebootInto(MODE_JOIN, true);             // FLAG_SETUP_ONCE: next boot is the setup network
    }
    apMode = false;
    lastLinkOk = millis();
#ifdef WIFI_PROVISION_KEEP_AP
    // Bench harness, ESP32 only: keep the setup network up beside the joined
    // one so its DNS and captive redirect can be exercised from the LAN.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(s.name);
    Dns.begin(53);
    captive = true;
#endif
  } else {
    if (!apStart(s.name)) {
      Serial.println("could not start the setup network");
      delay(5000);
      restartBoard();
    }
    apMode  = true;
    apSince = millis();
    captive = Dns.begin(53);
  }
  ipToStr(myIP(), ipStr);

  Udp.begin(s.port);
  http.begin();
#ifdef WS_HAS_MDNS
  if (!apMode && MDNS.begin(s.name)) MDNS.addService("osc", "udp", s.port);
#endif
  printStatus();
  Serial.println("type  help  for the serial commands");
}

void loop() {
  serveSerial();
  serveHttp();
  if (captive) serveDns();
  serveUdp();
  stream();
  ledTick();
#ifdef WIFI_PROVISION_BUTTON
  buttonTick();
#endif

  const uint32_t now = millis();
  if (apMode) {
    if (apHasClient()) apSince = now;         // someone is (or was just) configuring
    if (apFallback && now - apSince > RETRY_MS) {
      Serial.println("nobody on the setup network; restarting to try the network again");
      rebootInto(MODE_JOIN, false);
    }
  } else if (WiFi.status() == WL_CONNECTED) {
    lastLinkOk = now;
  } else if (now - lastLinkOk > 30000) {
    Serial.println("link lost for 30 s; restarting");
    restartBoard();
  }
}

/*
 * STATUS -- RUN ON HARDWARE 2026-09-04, M5Stack Capsule (ESP32-S3, MAC
 * c0:4e:30:11:4d:f8, esp32 core 3.3.11, FQBN esp32:esp32:m5stack_capsule,
 * built with -DWIFI_PROVISION_POWER_HOLD=46 -DWIFI_PROVISION_BUTTON=42),
 * driven by test/hardware/wifiprovision.py, seven runs. Under the earlier
 * vocabulary (WiFiSetup, oscuino-XXXX, a separate /net ask): 37/37 on the
 * bench build (-DWIFI_PROVISION_KEEP_AP, which keeps the setup network up
 * beside the joined one so its DNS and redirect can be reached from the
 * LAN), 31/31 on the plain build twice, 33/33 with --long (the 3-minute
 * fallback retry fired at 173 s and joined). Under the decided vocabulary
 * in this file: 31/31 on the plain build.
 * Seen: forget -> setup network OSCMCU-4DF8 on the air (this Mac's scan),
 * stream defaulting to 500 ms; ssid/pass/join over the console -> joined in
 * 4 s; /enq over UDP 20/20 at a 10 ms median, /enq/net carrying ip rssi
 * port name mac; /state at 100 ms and 50 ms with no gaps; /rate 0 silent;
 * /enq to both broadcast forms 10/10; the settings page, a save that
 * rebooted and moved the stream; DNS catch-all and captive 302; a wrong
 * SSID back on the setup network inside 30 s; settings survived reflashes.
 *
 * Run 8 was a phone: joined OSCMCU-4DF8, the captive-portal sheet opened the
 * page by itself, a save from it rebooted the board onto the network at
 * -59 dBm, streaming to the form's destination at the 100 ms it asked for.
 *
 * NOT seen: the button (GPIO42 was compiled in, never pressed); the LED (the
 * Capsule has no LED_BUILTIN); any board that is not this one. Every other
 * stack in wifi_stack.h is compile-only.
 * See README.md beside this file for the numbers and the open questions.
 */

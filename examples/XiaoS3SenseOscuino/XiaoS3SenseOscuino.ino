// XIAO ESP32S3 Sense — sensors and camera frames as OSC over SLIP/USB.
//
//   arduino-cli compile -b esp32:esp32:XIAO_ESP32S3 \
//       --upload --port /dev/cu.usbmodemXXXX examples/XiaoS3SenseOscuino
//
// Use the STOCK FQBN. Do not add USBMode or CDCOnBoot: this board already has
// cdc_on_boot=1, and passing the options another ESP32-S3 needs (the Adafruit
// QT Py wants USBMode=hwcdc,CDCOnBoot=cdc) produces a board that flashes,
// verifies its hash and then says nothing at all.
//
// PSRAM is OFF on the stock FQBN — boards.txt lists PSRAM.disabled first, so
// psramFound() is false and ESP.getPsramSize() is 0. The camera does not need
// it: with the frame buffer in DRAM this runs to SXGA. Add :PSRAM=opi for the
// 8 MB if you want a bigger buffer; it does not disturb the USB settings.
//
// Open XiaoS3SenseOscuino.html to see it. Chrome or Edge, served over
// http://localhost — Web Serial does not work from a file:// URL.
//
// Addresses: the capability-named space of ADDRESSES.md. Nothing is sent
// under this board's name.
//
// Inbound
//   /enq              ask for the capability bundle again -- see below
//   /rate ,i ms         stream period, 20..10000; 0 stops the stream. Echoed.
//                       ONE rate paces everything: the analog inputs, the
//                       die temperature, the microphone AND the camera frames
//                       all travel in the same bundle, every /rate ms. It
//                       starts at 200 ms, the old frame gap, so the mic and
//                       the sensors now arrive at the frame rate too.
//   /stream ,i 0|1      camera frames on or off; the rest of the stream
//                       keeps going. Only the JPEG costs real wire.
//   /cam/size ,i 0..3   QQVGA 160x120, QVGA 320x240, VGA 640x480, SVGA 800x600
//   /cam/quality ,i     JPEG quality 4..63, lower is better and bigger
//   /s/l ,i 0|1         the XIAO's own LED (active LOW on the board). Echoed.
//   /s/a                analog input count -> /s/a ,i
//   /temp               read the die temperature once -> /temp ,f
//   /mic                read the microphone once -> /mic ,ii (mic up only)
//   /a/0 .. /a/5        read one analog input once -> /a/<n> ,i
//                       Every request that reads something answers on its
//                       own address, so with /rate 0 the board can be
//                       polled instead of streamed. The write form of
//                       /a/<n> (an argument) is not implemented here.
// Outbound, the /enq bundle: the name, then one /enq per capability that
// is actually here. Booleans became presence: a board whose camera or
// microphone failed to initialise answers a shorter list, not a false one.
//   /enq ,s           "XiaoS3SenseOscuino"
//   /enq/temp           the die temperature is always there
//   /enq/diag           free-text diagnostics (free heap)
//   /enq/cam ,ii        width, height of the current frame size
//   /enq/mic            the PDM microphone
// Outbound, the stream: one bundle every /rate ms, everything sampled in the
// same pass so every reading in it belongs to the same instant.
//   /state ,ii          sequence, millis -- the gap detector
//   /a/0 .. /a/5 ,i     the six analog inputs, 0..4095
//   /temp ,f            die temperature, degrees C
//   /diag ,s            "heap <n> KB" -- free text, never parsed
//   /mic ,ii            rms, peak, full scale 0..32767 (when the mic is up)
//   /cam ,b             one JPEG frame (camera up and /stream 1)
//
// The stream bundle is written to the wire element by element rather than
// built with OSCBundle: OSCMessage::add(uint8_t*,int) mallocs len+4 and
// copies the whole JPEG, and at 14 KB a VGA frame that is a copy and a
// fragmentation risk for no benefit. The bundle format is a header, then
// each element's byte count followed by the element.

#define OSC_SLIP_TX_BUFFER 1024   // must precede the include; the header is #ifndef.
                                  // A VGA frame is ~14 KB, and 64 bytes a block
                                  // makes that hundreds of writes.

#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SLIPEncodedSerial.h>
#include "esp_camera.h"
#include <ESP_I2S.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

// PDM microphone on the Sense board. Seeed's wiki gives CLK 42, DATA 41, and
// says only PDM mono 16-bit is supported on the S3, 16 kHz being the stable
// rate. ESP_I2S.h is the 3.x API; the old I2S.h with setAllPins() is 2.0.x.
#define PDM_CLK_PIN 42
#define PDM_DATA_PIN 41
static I2SClass I2Sin;
static bool micOK = false;

// STATUS: this microphone block compiles but has never run -- the board left
// the USB bus before it could be flashed. The camera path above it is
// measured (19/19 valid JPEGs); treat /mic as unconfirmed until a capture
// shows real audio. The pin map and PDM-mono-16k constraint are from Seeed's
// wiki, not from this hardware. Addresses renamed onto ADDRESSES.md on
// 2026-09-03 (/led -> /s/l; /cam/rate and /mic/rate -> /rate; /xiao ->
// /state + /a/<n> + /temp + /diag in one bundle; /cam ,iiiib -> /cam ,b;
// /mic ,iiib -> /mic ,ii; /enq ,siTF -> /enq ,s + /enq/...; /temp, /mic
// and /a/<n> asks added); that build is compile-checked and has not been
// re-run on the board.
//
// 512 samples at 16 kHz is 32 ms of audio per reading: enough for an rms
// and a peak that mean something.
static int16_t pcm[512];

// Pin map for CAMERA_MODEL_XIAO_ESP32S3, copied from the esp32 core's own
// CameraWebServer/camera_pins.h so this sketch stands alone.
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

static const uint8_t ANALOG_PINS[] = { A0, A1, A2, A3, A4, A5 };
static const int     NANALOG       = sizeof ANALOG_PINS;

// The four sizes /cam/size selects, with the dimensions /enq/cam announces.
struct FrameSize { framesize_t fs; uint16_t w, h; };
static const FrameSize SIZES[] = {
  { FRAMESIZE_QQVGA, 160, 120 }, { FRAMESIZE_QVGA, 320, 240 },
  { FRAMESIZE_VGA,   640, 480 }, { FRAMESIZE_SVGA, 800, 600 },
};

static bool     cameraOK   = false;
static bool     streaming  = true;     // /stream: camera frames in the bundle
static int32_t  seq        = 0;        // /state sequence
static uint32_t rateMs     = 200;      // /rate: ms between bundles; 0 stops
static int      curSizeIdx = 1;        // QVGA
static int      curQuality = 12;       // 0..63, lower is better and bigger

/* ------------------------------------------------------------------ camera */

static bool cameraBegin(framesize_t fs, int quality) {
  camera_config_t c = {};              // zero-init also clears sccb_i2c_port
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk     = XCLK_GPIO_NUM;
  c.pin_pclk     = PCLK_GPIO_NUM;
  c.pin_vsync    = VSYNC_GPIO_NUM;
  c.pin_href     = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;      // SCCB is on I2C port 1; Wire stays free
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn     = PWDN_GPIO_NUM;
  c.pin_reset    = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;     // the OV2640 does JPEG in hardware
  c.frame_size   = fs;
  c.jpeg_quality = quality;
  c.fb_count     = 1;
  // Asking for CAMERA_FB_IN_PSRAM without PSRAM returns ESP_FAIL, and the
  // stock FQBN has no PSRAM, so branch rather than assume.
  c.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  if (esp_camera_deinit() != ESP_OK) { /* first call: nothing to tear down */ }
  return esp_camera_init(&c) == ESP_OK;
}

/* ------------------------------------------------------------ wire helpers */

static void writePadded(Print &p, const char *s) {
  size_t n = strlen(s) + 1;
  p.write((const uint8_t *)s, n);
  while (n & 3) { p.write((uint8_t)0); n++; }
}
static void writeBE32(Print &p, uint32_t v) {
  const uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
  p.write(b, 4);
}

// "#bundle" then the immediate timetag (0, 1).
static void beginBundle(Print &p) {
  static const uint8_t header[16] = { '#','b','u','n','d','l','e',0, 0,0,0,0, 0,0,0,1 };
  p.write(header, 16);
}
// One element: its byte count, then the message.
static void bundleAdd(Print &p, OSCMessage &m) {
  writeBE32(p, (uint32_t) m.bytes());
  m.send(p);
}
// /cam ,b -- the JPEG straight from the frame buffer to the wire.
// "/cam\0" pads to 8, ",b\0" to 4, then the blob: its length and its bytes
// padded to a multiple of 4.
static void bundleAddFrame(Print &p, camera_fb_t *fb) {
  const uint32_t len    = fb->len;
  const uint32_t padded = (len + 3) & ~3u;
  writeBE32(p, 8 + 4 + 4 + padded);
  writePadded(p, "/cam");
  writePadded(p, ",b");
  writeBE32(p, len);
  p.write(fb->buf, len);
  for (uint32_t pad = padded - len; pad; pad--) p.write((uint8_t)0);
}

// A one-message reply or echo.
static void reply(OSCMessage &m) {
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

/* ------------------------------------------------------------------ mic */

// rms and peak of one reading, both on the 16-bit scale (0..32767).
static bool readMic(int32_t &rms, int32_t &peak) {
  const size_t got = I2Sin.readBytes((char *) pcm, sizeof pcm);
  const int n = got / sizeof(int16_t);
  if (n <= 0) return false;

  // uint64, not a pre-shift: (s*s)>>16 truncates every sample quieter than
  // 256 counts to zero, so a quiet room reads rms 0. That exact failure was
  // measured on the PyBadge's mic before this sketch could repeat it.
  uint64_t sumsq = 0;
  int16_t pk = 0;
  for (int i = 0; i < n; i++) {
    const int32_t s = pcm[i];
    sumsq += (uint32_t)(s * s);
    const int16_t a = s < 0 ? -s : s;
    if (a > pk) pk = a;
  }
  rms  = (int32_t) sqrtf((float) sumsq / (float) n);
  peak = pk;
  return true;
}

/* ----------------------------------------------------------------- stream */

// One bundle, everything sampled in one pass, so every reading in it belongs
// to the same instant. The frame goes last: it is the only element that
// costs real wire, and a reader can act on /state before it arrives.
static void sendStream(uint32_t now) {
  SLIPSerial.beginPacket();
  beginBundle(SLIPSerial);
  {
    OSCMessage m("/state");
    m.add((intOSC_t) seq++).add((intOSC_t) now);
    bundleAdd(SLIPSerial, m);
  }
  for (int i = 0; i < NANALOG; i++) {
    char addr[8];
    snprintf(addr, sizeof addr, "/a/%d", i);
    OSCMessage m(addr);
    m.add((intOSC_t) analogRead(ANALOG_PINS[i]));
    bundleAdd(SLIPSerial, m);
  }
  {
    OSCMessage m("/temp");
    m.add(temperatureRead());                       // float, degrees C
    bundleAdd(SLIPSerial, m);
  }
  {
    char text[32];
    snprintf(text, sizeof text, "heap %u KB", (unsigned) (ESP.getFreeHeap() / 1024));
    OSCMessage m("/diag");
    m.add((const char *) text);
    bundleAdd(SLIPSerial, m);
  }
  if (micOK) {
    int32_t rms, peak;
    if (readMic(rms, peak)) {
      OSCMessage m("/mic");
      m.add((intOSC_t) rms).add((intOSC_t) peak);
      bundleAdd(SLIPSerial, m);
    }
  }
  if (cameraOK && streaming) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) { bundleAddFrame(SLIPSerial, fb); esp_camera_fb_return(fb); }
  }
  SLIPSerial.endPacket();
}

/* ----------------------------------------------------------------- inbound */

static void routeLed(OSCMessage &m) {                 // /s/l 0|1, echoed
  if (m.size() < 1 || !m.isInt(0)) return;
  const int on = m.getInt(0) ? 1 : 0;
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, on ? LOW : HIGH);         // active LOW
  OSCMessage r("/s/l"); r.add((intOSC_t) on); reply(r);
}
static void routeAnalogCount(OSCMessage &) {          // /s/a -> /s/a <count>
  OSCMessage r("/s/a"); r.add((intOSC_t) NANALOG); reply(r);
}
static void routeTemp(OSCMessage &) {                 // /temp -> /temp <deg C>
  OSCMessage r("/temp"); r.add(temperatureRead()); reply(r);
}
static void routeMic(OSCMessage &) {                  // /mic -> /mic <rms> <peak>
  int32_t rms, peak;
  if (!micOK || !readMic(rms, peak)) return;          // not announced, not answered
  OSCMessage r("/mic"); r.add((intOSC_t) rms).add((intOSC_t) peak); reply(r);
}
// /a/<n> with no argument -> /a/<n> <0..4095>: the same six inputs the stream
// carries, by index. addrOffset points past "/a"; the rest must be exactly
// "/<n>". The write form (an argument) is not implemented on this board.
static void routeAnalog(OSCMessage &m, int addrOffset) {
  for (int i = 0; i < NANALOG; i++) {
    char tail[4];
    snprintf(tail, sizeof tail, "/%d", i);
    if (!m.fullMatch(tail, addrOffset)) continue;
    if (m.size() > 0) return;
    char addr[8];
    snprintf(addr, sizeof addr, "/a/%d", i);
    OSCMessage r(addr); r.add((intOSC_t) analogRead(ANALOG_PINS[i])); reply(r);
    return;
  }
}
static void routeStream(OSCMessage &m) {              // /stream 0|1: camera frames
  if (m.size() >= 1 && m.isInt(0)) streaming = m.getInt(0) != 0;
}
static void routeRate(OSCMessage &m) {                // /rate <ms>; 0 stops; echoed
  if (m.size() < 1 || !m.isInt(0)) return;
  const int32_t v = m.getInt(0);
  rateMs = v <= 0 ? 0 : (uint32_t) constrain(v, 20, 10000);
  OSCMessage r("/rate"); r.add((intOSC_t) rateMs); reply(r);
}
static void routeQuality(OSCMessage &m) {             // /cam/quality <0..63>
  if (m.size() < 1 || !m.isInt(0)) return;
  curQuality = constrain(m.getInt(0), 4, 63);
  sensor_t *s = esp_camera_sensor_get();
  if (s) s->set_quality(s, curQuality);
}
static void routeSize(OSCMessage &m) {                // /cam/size 0..3
  if (m.size() < 1 || !m.isInt(0)) return;
  curSizeIdx = constrain(m.getInt(0), 0, 3);
  sensor_t *s = esp_camera_sensor_get();
  if (s) s->set_framesize(s, SIZES[curSizeIdx].fs);
}

/* -------------------------------------------------------------------- main */

// The boot /enq is very nearly always lost: the board resets, its USB
// device re-enumerates, and the host opens the port some hundreds of
// milliseconds later, by which time setup() has long finished. Measured on
// this repo's ESP32 and SAMD boards -- a probe opening the port straight
// after flashing never once caught it. So /enq is also an INBOUND address
// and the page asks for it on connect.
//
// The capability bundle of ADDRESSES.md: the name, then one /enq per thing
// that is actually here. What failed to initialise is simply not listed.
static void sendEnq() {
  OSCBundle b;
  b.add("/enq").add("XiaoS3SenseOscuino");
  b.add("/enq/temp");
  b.add("/enq/diag");
  if (cameraOK) b.add("/enq/cam").add((intOSC_t) SIZES[curSizeIdx].w)
                                 .add((intOSC_t) SIZES[curSizeIdx].h);
  if (micOK)    b.add("/enq/mic");
  SLIPSerial.beginPacket(); b.send(SLIPSerial); SLIPSerial.endPacket();
}

static void routeEnq(OSCMessage &) { sendEnq(); }

void setup() {
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
  // HWCDC installs its 256-byte rings only if none is preset, so these have to
  // come before begin(). The 256-byte rx ring is what makes inbound bursts
  // stop at 266 bytes on every HWCDC ESP32 part measured here.
  Serial.setRxBufferSize(1024);
  Serial.setTxBufferSize(8192);
#endif
  SLIPSerial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);          // off; active LOW
  cameraOK = cameraBegin(SIZES[curSizeIdx].fs, curQuality);

  I2Sin.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);
  micOK = I2Sin.begin(I2S_MODE_PDM_RX, 16000,
                      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);

  sendEnq();          // nearly always lost; the page asks again
}

// Non-blocking receive, the extras/webserial/template.ino pattern. Two rules,
// both learned the hard way there: endofPacket() must be called BEFORE
// available() on every pass (available() drives the SLIP state machine and can
// eat a packet boundary if it runs first), and the pump must RETURN rather
// than block when the buffer runs dry -- an unplug mid-frame, or one lost
// byte, would otherwise wedge loop() forever, taking the outbound reports
// with it. The message is filled across several loop() passes, which is why
// it lives at file scope instead of inside loop().
static OSCMessage inMsg;

static bool pollOSC() {
  while (!SLIPSerial.endofPacket()) {
    int size = SLIPSerial.available();
    if (size <= 0) return false;              // nothing buffered -- try later
    while (size--) {
      int c = SLIPSerial.read();
      if (c >= 0) inMsg.fill((uint8_t) c);    // read() returns -1 on SLIP error
    }
  }
  return true;
}

void loop() {
  static uint32_t lastStream = 0;

  // inbound first, so a /stream 0 takes effect before the next capture
  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/s/l",         routeLed);
      inMsg.dispatch("/s/a",         routeAnalogCount);
      inMsg.dispatch("/temp",        routeTemp);
      inMsg.dispatch("/mic",         routeMic);
      inMsg.route("/a",              routeAnalog);
      inMsg.dispatch("/stream",      routeStream);
      inMsg.dispatch("/rate",        routeRate);
      inMsg.dispatch("/cam/quality", routeQuality);
      inMsg.dispatch("/cam/size",    routeSize);
      inMsg.dispatch("/enq",       routeEnq);
    }
    inMsg.empty();
  }

  const uint32_t now = millis();
  if (rateMs && now - lastStream >= rateMs) {
    lastStream = now;
    sendStream(now);
  }
}

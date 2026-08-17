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

#define OSC_SLIP_TX_BUFFER 1024   // must precede the include; the header is #ifndef.
                                  // A VGA frame is ~14 KB, and 64 bytes a block
                                  // makes that hundreds of writes.

#include <OSCMessage.h>
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
// wiki, not from this hardware.
//
// A scope trace to draw, and a level to meter with. 128 samples at 16 kHz is
// 8 ms of audio -- enough to see waveform shape without costing much wire.
#define SCOPE_POINTS 128
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

static bool     cameraOK   = false;
static bool     streaming  = true;
static int32_t  frameSeq   = 0;
static uint32_t frameEvery = 200;      // ms between frames; /cam/rate changes it
static uint32_t sensorEvery = 100;
static uint32_t micEvery    = 50;      // 20 Hz scope + level
static framesize_t curSize = FRAMESIZE_QVGA;
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

// Write the frame straight to the wire. OSCMessage::add(uint8_t*,int) mallocs
// len+4 and copies the whole JPEG; at 14 KB a frame that is a copy and a
// fragmentation risk for no benefit, and the message format is four lines.
static void writePadded(Print &p, const char *s) {
  size_t n = strlen(s) + 1;
  p.write((const uint8_t *)s, n);
  while (n & 3) { p.write((uint8_t)0); n++; }
}
static void writeBE32(Print &p, uint32_t v) {
  const uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
  p.write(b, 4);
}

// /cam ,iiiib  seq, millis, width, height, jpeg
static void sendFrame(camera_fb_t *fb) {
  SLIPSerial.beginPacket();
  writePadded(SLIPSerial, "/cam");
  writePadded(SLIPSerial, ",iiiib");
  writeBE32(SLIPSerial, (uint32_t) frameSeq++);
  writeBE32(SLIPSerial, millis());
  writeBE32(SLIPSerial, fb->width);
  writeBE32(SLIPSerial, fb->height);
  writeBE32(SLIPSerial, fb->len);          // OSC blob: length then bytes
  SLIPSerial.write(fb->buf, fb->len);
  for (size_t pad = (4 - (fb->len & 3)) & 3; pad; pad--) SLIPSerial.write((uint8_t)0);
  SLIPSerial.endPacket();
}

/* ------------------------------------------------------------------ mic */

// /mic ,iiib  rmsQ15, peakQ15, sampleRate, scope
// The scope is SCOPE_POINTS signed bytes -- the top 8 bits of each sample --
// sent as a blob. Full 16-bit PCM would be twice the wire for detail no
// on-screen trace can show.
static void sendMic() {
  const size_t want = sizeof pcm;
  const size_t got  = I2Sin.readBytes((char *) pcm, want);
  const int n = got / sizeof(int16_t);
  if (n <= 0) return;

  // uint64, not a pre-shift: (s*s)>>16 truncates every sample quieter than
  // 256 counts to zero, so a quiet room reads rms 0. That exact failure was
  // measured on the PyBadge's mic before this sketch could repeat it.
  uint64_t sumsq = 0;
  int16_t peak = 0;
  for (int i = 0; i < n; i++) {
    const int32_t s = pcm[i];
    sumsq += (uint32_t)(s * s);
    const int16_t a = s < 0 ? -s : s;
    if (a > peak) peak = a;
  }
  const int32_t rms = (int32_t) sqrtf((float) sumsq / (float) n);

  int8_t scope[SCOPE_POINTS];
  const int step = n > SCOPE_POINTS ? n / SCOPE_POINTS : 1;
  for (int i = 0; i < SCOPE_POINTS; i++) {
    const int j = i * step;
    scope[i] = (int8_t) (j < n ? (pcm[j] >> 8) : 0);
  }

  SLIPSerial.beginPacket();
  writePadded(SLIPSerial, "/mic");
  writePadded(SLIPSerial, ",iiib");
  writeBE32(SLIPSerial, (uint32_t) rms);
  writeBE32(SLIPSerial, (uint32_t) peak);
  writeBE32(SLIPSerial, 16000);
  writeBE32(SLIPSerial, SCOPE_POINTS);
  SLIPSerial.write((const uint8_t *) scope, SCOPE_POINTS);   // already a multiple of 4
  SLIPSerial.endPacket();
}

/* ----------------------------------------------------------------- inbound */

static void routeLed(OSCMessage &m) {                 // /led 0|1
  if (m.size() < 1 || !m.isInt(0)) return;
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, m.getInt(0) ? LOW : HIGH);   // active LOW
}
static void routeStream(OSCMessage &m) {              // /stream 0|1
  if (m.size() >= 1 && m.isInt(0)) streaming = m.getInt(0) != 0;
}
static void routeRate(OSCMessage &m) {                // /cam/rate <ms>
  if (m.size() >= 1 && m.isInt(0)) frameEvery = constrain(m.getInt(0), 0, 10000);
}
static void routeQuality(OSCMessage &m) {             // /cam/quality <0..63>
  if (m.size() < 1 || !m.isInt(0)) return;
  curQuality = constrain(m.getInt(0), 4, 63);
  sensor_t *s = esp_camera_sensor_get();
  if (s) s->set_quality(s, curQuality);
}
static void routeMicRate(OSCMessage &m) {              // /mic/rate <ms>
  if (m.size() >= 1 && m.isInt(0)) micEvery = constrain(m.getInt(0), 20, 5000);
}
static void routeSize(OSCMessage &m) {                // /cam/size 0..3
  if (m.size() < 1 || !m.isInt(0)) return;
  static const framesize_t sizes[] = { FRAMESIZE_QQVGA, FRAMESIZE_QVGA,
                                       FRAMESIZE_VGA,   FRAMESIZE_SVGA };
  curSize = sizes[constrain(m.getInt(0), 0, 3)];
  sensor_t *s = esp_camera_sensor_get();
  if (s) s->set_framesize(s, curSize);
}

/* -------------------------------------------------------------------- main */

// The boot /hello is very nearly always lost: the board resets, its USB
// device re-enumerates, and the host opens the port some hundreds of
// milliseconds later, by which time setup() has long finished. Measured on
// this repo's ESP32 and SAMD boards -- a probe opening the port straight
// after flashing never once caught it. So /hello is also an INBOUND address
// and the page asks for it on connect.
static void sendHello() {
  OSCMessage hello("/hello");
  hello.add("XiaoS3SenseOscuino").add((intOSC_t) NANALOG).add(cameraOK).add(micOK);
  SLIPSerial.beginPacket(); hello.send(SLIPSerial); SLIPSerial.endPacket();
}

static void routeHello(OSCMessage &) { sendHello(); }

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
  cameraOK = cameraBegin(curSize, curQuality);

  I2Sin.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);
  micOK = I2Sin.begin(I2S_MODE_PDM_RX, 16000,
                      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);

  sendHello();          // nearly always lost; the page asks again
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
  static uint32_t lastSensors = 0, lastFrame = 0, lastMic = 0;

  // inbound first, so a /stream 0 takes effect before the next capture
  if (pollOSC()) {
    if (!inMsg.hasError()) {
      inMsg.dispatch("/led",         routeLed);
      inMsg.dispatch("/stream",      routeStream);
      inMsg.dispatch("/cam/rate",    routeRate);
      inMsg.dispatch("/cam/quality", routeQuality);
      inMsg.dispatch("/cam/size",    routeSize);
      inMsg.dispatch("/mic/rate",    routeMicRate);
      inMsg.dispatch("/hello",       routeHello);
    }
    inMsg.empty();
  }

  uint32_t now = millis();

  if (now - lastSensors >= sensorEvery) {
    lastSensors = now;
    // One message, everything sampled in one pass, so every reading in it
    // belongs to the same instant -- the same shape as EsploraOscuino.
    OSCMessage m("/xiao");
    for (int i = 0; i < NANALOG; i++) m.add((intOSC_t) analogRead(ANALOG_PINS[i]));
    m.add((intOSC_t) (temperatureRead() * 100.0f));   // centi-degrees C
    m.add((intOSC_t) (ESP.getFreeHeap() / 1024));
    m.add((intOSC_t) frameSeq);
    m.add(cameraOK);
    SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  }

  if (micOK && streaming && now - lastMic >= micEvery) {
    lastMic = now;
    sendMic();
  }

  if (cameraOK && streaming && frameEvery && now - lastFrame >= frameEvery) {
    lastFrame = now;
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) { sendFrame(fb); esp_camera_fb_return(fb); }
  }
}

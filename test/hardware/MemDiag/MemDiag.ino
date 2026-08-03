// Instrumented echo: reports free RAM and error state so the overflow wedge can
// be attributed to buffer space rather than guessed at.
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>
#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
// Boards with no SLIPEncodedUSBSerial land here and bind to Serial through
// HardwareSerial. That is the right port either way on the UNO R4: the
// Minima does #define Serial SerialUSB (native USB, derives from
// HardwareSerial), and the WiFi builds with -DNO_USB so its Serial is a real
// UART that the on-board ESP32-S3 bridges to the host.
SLIPEncodedSerial SLIPSerial(Serial);
#endif

extern unsigned int __heap_start;
extern void *__brkval;
static int freeRam() {
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

static OSCMessage msgIn;
static int32_t frames = 0, errs = 0;
static int minFree = 32767;

void setup() { SLIPSerial.begin(115200); }

void loop() {
  bool got = false;
  while (!SLIPSerial.endofPacket()) {
    int a = SLIPSerial.available();
    if (a <= 0) break;
    while (a--) { int c = SLIPSerial.read(); if (c >= 0) msgIn.fill((uint8_t) c); }
    int f = freeRam(); if (f < minFree) minFree = f;
  }
  if (SLIPSerial.endofPacket()) got = true;

  if (got) {
    frames++;
    int err = (int) msgIn.getError();
    if (err) errs++;
    int32_t idx = msgIn.size() ? msgIn.getInt(0) : -1;
    int f = freeRam(); if (f < minFree) minFree = f;

    OSCMessage r("/diag");
    r.add(idx).add(frames).add((int32_t) err).add((int32_t) errs)
     .add((int32_t) f).add((int32_t) minFree).add((int32_t) msgIn.size());
    SLIPSerial.beginPacket(); r.send(SLIPSerial); SLIPSerial.endPacket();
    msgIn.empty();
  }
}

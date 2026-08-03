// Transmit throughput: send a fixed OSC message N times, report microseconds.
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

static const int N = 200;
void setup() { SLIPSerial.begin(115200); delay(1500); }

void loop() {
  OSCMessage m("/bench");
  m.add((int32_t)1).add((int32_t)2).add((int32_t)3).add(4.5f)
   .add("hello world").add((int32_t)6).add((int32_t)7).add((int32_t)8);
  int bytes = m.bytes();

  unsigned long t0 = micros();
  for (int i = 0; i < N; i++) {
    SLIPSerial.beginPacket();
    m.send(SLIPSerial);
    SLIPSerial.endPacket();
  }
  unsigned long dt = micros() - t0;

  delay(400);
  thisBoardsSerialUSB.print("BENCH n="); thisBoardsSerialUSB.print(N);
  thisBoardsSerialUSB.print(" bytes="); thisBoardsSerialUSB.print(bytes);
  thisBoardsSerialUSB.print(" us="); thisBoardsSerialUSB.print(dt);
  thisBoardsSerialUSB.print(" us_per_pkt="); thisBoardsSerialUSB.println(dt / N);
  delay(2000);
}

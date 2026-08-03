// Transmit throughput: send a fixed OSC message N times, report microseconds.
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

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

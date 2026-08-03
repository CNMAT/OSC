// Drains through the library's SLIP layer, so _SLIPSerial::available() runs and
// with it the 32U4 ZLP workaround. Reports the raw byte count over the same port.
#include <SLIPEncodedSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
unsigned long total = 0, last = 0;
void setup() { SLIPSerial.begin(115200); }
void loop() {
  int a = SLIPSerial.available();          // <- workaround fires here
  while (a-- > 0) { SLIPSerial.read(); total++; }
  SLIPSerial.endofPacket();                // <- and here
  unsigned long now = millis();
  if (now - last >= 500) {
    last = now;
    thisBoardsSerialUSB.print("RX ");
    thisBoardsSerialUSB.println(total);
  }
}

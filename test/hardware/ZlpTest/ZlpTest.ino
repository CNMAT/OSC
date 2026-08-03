// Drains through the library's SLIP layer, so _SLIPSerial::available() runs and
// with it the 32U4 ZLP workaround. Reports the raw byte count over the same port.
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

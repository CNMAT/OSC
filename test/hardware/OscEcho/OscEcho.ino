// Pure OSC echo: decode an inbound packet with the library, re-encode it with
// the library, send it straight back. A byte-identical reply means decode and
// encode agree on the wire format for whatever was sent -- on the target, not
// on a host.
#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

static uint8_t buf[600];
static size_t  n = 0;          // MUST persist across loop() iterations

void setup() {
  SLIPSerial.begin(115200);
}

void loop() {
  while (!SLIPSerial.endofPacket()) {
    int avail = SLIPSerial.available();
    if (avail <= 0) return;                 // come back next loop()
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0 && n < sizeof buf) buf[n++] = (uint8_t) c;
    }
  }
  if (n == 0) return;

  if (buf[0] == '#') {
    OSCBundle b;
    b.fill(buf, n);
    if (!b.hasError()) { SLIPSerial.beginPacket(); b.send(SLIPSerial); SLIPSerial.endPacket(); }
  } else {
    OSCMessage m;
    m.fill(buf, n);
    if (!m.hasError()) { SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket(); }
  }
  n = 0;
}

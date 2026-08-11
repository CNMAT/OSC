// Counting bench: attribute loss to a segment instead of asserting a cliff.
//
// The pipeline under test has four places to count:
//
//   host wrote --USB--> board received --sketch--> board sent --USB--> host received
//       A                    B                        C                    D
//
// bench.py holds A and D; this sketch holds B and C. A frame missing from
// A->B is a host->device loss (kernel tty, USB, device RX ring, or drain
// rate); missing from C->D is device->host; B vs C is the sketch itself.
// "Lost somewhere" is not a reportable result -- that rule exists because
// every burst figure this directory ever withdrew turned out to be the
// instrument, not the board.
//
// Traffic:
//   /b/s ,ii seq crc     inbound test frame. Counted, never answered --
//                        reporting must not compete with what it measures.
//   /b/q                 report and reset:
//                        /b/r ,iiiiiii rxFrames rxBytes seqErrs crcErrs
//                                      decodeErrs firstGap spanMs
//   /b/f ,ii n gap_us    flood n /b/s frames host-ward, gap_us apart
//   /b/lazy ,i ms        delay ms per loop() before draining, to emulate a
//                        slow application against a fixed RX ring
//
// crc is zlib crc32 over the 4 big-endian bytes of seq, so corruption and
// loss are distinguishable. USB CDC preserves order, so any out-of-order
// sequence is counted as an error, not reordered away.
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static uint8_t buf[128];       // /b/* frames are ~20 bytes; headroom, not slack
static size_t  n = 0;          // MUST persist across loop() iterations

static uint32_t rxFrames = 0, rxBytes = 0;
static uint32_t seqErrs = 0, crcErrs = 0, decodeErrs = 0;
static int32_t  firstGap = -1;         // expected seq at the first discontinuity
static uint32_t expectSeq = 0;
static uint32_t tFirst = 0, tLast = 0; // millis() at first/last /b/s
static uint32_t lazyMs = 0;

static uint32_t crc32_of_seq(uint32_t v) {   // zlib crc32 of the 4 BE bytes
  uint32_t crc = 0xFFFFFFFFul;
  for (int8_t k = 3; k >= 0; k--) {
    crc ^= (uint8_t)(v >> (8 * k));
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc >> 1) ^ (0xEDB88320ul & (uint32_t)(-(int32_t)(crc & 1)));
  }
  return crc ^ 0xFFFFFFFFul;
}

static void sendSeqFrame(uint32_t seq) {
  OSCMessage m("/b/s");
  m.add((int32_t) seq).add((int32_t) crc32_of_seq(seq));
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
}

static void handle(OSCMessage &m) {
  if (m.fullMatch("/b/s")) {
    const uint32_t now = millis();
    if (!rxFrames) tFirst = now;
    tLast = now;
    rxFrames++;
    const uint32_t seq = (uint32_t) m.getInt(0);
    const uint32_t crc = (uint32_t) m.getInt(1);
    if (crc != crc32_of_seq(seq)) crcErrs++;
    if (seq != expectSeq) {
      seqErrs++;
      if (firstGap < 0) firstGap = (int32_t) expectSeq;
      expectSeq = seq + 1;       // resync so one gap counts once, not forever
    } else {
      expectSeq++;
    }
    return;                      // counted, never answered
  }

  if (m.fullMatch("/b/q")) {
    OSCMessage r("/b/r");
    r.add((int32_t) rxFrames).add((int32_t) rxBytes)
     .add((int32_t) seqErrs).add((int32_t) crcErrs).add((int32_t) decodeErrs)
     .add(firstGap).add((int32_t) (tLast - tFirst));
    SLIPSerial.beginPacket(); r.send(SLIPSerial); SLIPSerial.endPacket();
    rxFrames = rxBytes = seqErrs = crcErrs = decodeErrs = 0;
    firstGap = -1; expectSeq = 0; tFirst = tLast = 0;
    return;
  }

  if (m.fullMatch("/b/f")) {
    const int32_t  cnt = m.getInt(0);
    const uint32_t gap = (uint32_t) m.getInt(1);
    for (int32_t i = 0; i < cnt; i++) {
      sendSeqFrame((uint32_t) i);
      if (gap >= 1000) delay(gap / 1000);
      if (gap % 1000)  delayMicroseconds(gap % 1000);
    }
    return;
  }

  if (m.fullMatch("/b/lazy")) {
    lazyMs = (uint32_t) m.getInt(0);
    return;
  }
}

void setup() {
  SLIPSerial.begin(115200);
}

void loop() {
  if (lazyMs) delay(lazyMs);               // the deliberately slow application

  while (!SLIPSerial.endofPacket()) {
    int avail = SLIPSerial.available();
    if (avail <= 0) return;                 // come back next loop()
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) {
        rxBytes++;
        if (n < sizeof buf) buf[n++] = (uint8_t) c;
      }
    }
  }
  if (n == 0) return;

  OSCMessage m;
  m.fill(buf, n);
  if (m.hasError()) decodeErrs++;
  else handle(m);
  n = 0;
}

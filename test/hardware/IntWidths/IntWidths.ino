// Emits one message built from every integer spelling, so the host can check
// which OSC type tag each one produced ON TARGET. This is the half of the
// bug-1 fix that only runs where long is 32 bits and int is 16 (classic AVR).
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);

void setup(){ SLIPSerial.begin(115200); }

void loop(){
  while (!SLIPSerial.endofPacket()) {
    int a = SLIPSerial.available();
    if (a <= 0) return;
    while (a--) SLIPSerial.read();          // discard: any packet triggers a report
  }
  OSCMessage m("/w");
  m.add((signed char)-1)
   .add((unsigned char)255)
   .add((short)-2)
   .add((unsigned short)65535)
   .add((int)-3)
   .add((unsigned int)65534u)
   .add((long)-100000L)
   .add((unsigned long)4000000000UL)
   .add((long long)-5000000000LL)
   .add((int32_t)123456)
   .add((int64_t)-9000000000LL);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  // report the compiler's view too
  OSCMessage s("/sizes");
  s.add((int32_t)sizeof(int)).add((int32_t)sizeof(long)).add((int32_t)sizeof(long long))
   .add((int32_t)sizeof(double));
  SLIPSerial.beginPacket(); s.send(SLIPSerial); SLIPSerial.endPacket();
}

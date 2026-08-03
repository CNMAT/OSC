// SLIP over TCP against a stub Client: no shield, no network, no hardware.
//
// SLIPEncodedTCP began as a hand-maintained copy of the _SLIPSerial state
// machine and missed that template's fixes. Run against the copy, the first
// three checks here fail (read() narrowing -1 to 0xFF, write(buf,n) returning
// the last byte's count, one client write per byte); run against
// _SLIPSerial<Client> they pass. The rest pin the framing protocol itself:
// exact escaping, round-trips, back-to-back packets, underrun-and-resume.
#include "OSCMessage.h"
#include "SLIPEncodedTCP.h"
#include <stdio.h>
#include <string.h>
#include <vector>
unsigned long millis(void){return 0;} unsigned long micros(void){return 0;}
osctime_t oscTime(){ osctime_t t={0,0}; return t; }

struct FakeClient : public Client {
  std::vector<uint8_t> rx; size_t rpos=0;   // bytes the peer has sent us
  std::vector<uint8_t> tx;                  // bytes we handed to the socket
  int writeCalls=0;
  int available() override { return (int)(rx.size()-rpos); }
  int read() override { return rpos<rx.size() ? rx[rpos++] : -1; }
  int peek() override { return rpos<rx.size() ? rx[rpos]   : -1; }
  size_t write(uint8_t b) override { writeCalls++; tx.push_back(b); return 1; }
  size_t write(const uint8_t *b, size_t n) override
    { writeCalls++; tx.insert(tx.end(), b, b+n); return n; }
  void flush() override {}
};

static int fails=0;
static void chk(const char*n,bool ok){ if(!ok) fails++; printf("  %-52s %s\n",n,ok?"ok":"FAIL"); }

// the receive loop from ETC_EOS_TCP, bounded so a broken build cannot hang.
// available() legitimately reports 0 while it eats framing bytes, so keep
// calling it the way a sketch's loop() would.
static bool receiveOne(SLIPEncodedTCP &slip, OSCMessage &msg){
  for (int spin=0; spin<1000; ++spin) {
    if (!slip.available()) continue;
    int guard = 100000;
    while (!slip.endofPacket() && guard-- > 0)
      while (slip.available()) { int c = slip.read(); if (c>=0) msg.fill((uint8_t)c); }
    return guard > 0;
  }
  return false;
}

int main(){
  // a TCP stream can go empty mid-packet at any point; that must read as
  // "nothing", never as a 0xFF data byte
  { FakeClient c; SLIPEncodedTCP slip(c);
    chk("read() on an empty stream returns -1", slip.read() == -1);
    chk("peek() on an empty stream returns -1", slip.peek() == -1); }

  { FakeClient c; SLIPEncodedTCP slip(c);
    const uint8_t buf[8] = {1,2,3,4,5,6,7,8};
    chk("write(buf,8) returns 8, not the last byte's count",
        slip.write(buf, sizeof buf) == sizeof buf); }

  // every Client::write on a W5100/W5500 is an SPI transaction, so the
  // packet should reach the client in a few blocks, not a call per byte
  { FakeClient c; SLIPEncodedTCP slip(c);
    OSCMessage m("/eos/ping"); m.add((int32_t)1); m.add(3.25f); m.add("hello");
    slip.beginPacket(); m.send(slip); slip.endPacket();
    chk("a packet reaches the client in blocks, not bytes",
        c.writeCalls <= 3 && c.tx.size() > 30); }

  { FakeClient c; SLIPEncodedTCP slip(c);
    const uint8_t payload[] = {0x01, 0xC0, 0x02, 0xDB, 0x03};
    slip.beginPacket(); slip.write(payload, sizeof payload); slip.endPacket();
    const uint8_t expect[] = {0xC0, 0x01, 0xDB,0xDC, 0x02, 0xDB,0xDD, 0x03, 0xC0};
    chk("C0 and DB are escaped exactly",
        c.tx.size()==sizeof expect && !memcmp(c.tx.data(), expect, sizeof expect)); }

  // a message whose wire image itself contains C0 and DB
  { FakeClient sender; SLIPEncodedTCP out(sender);
    OSCMessage m("/t/esc"); m.add((int32_t)0xC0DBC0DB); m.add((int32_t)-1); m.add("x");
    out.beginPacket(); m.send(out); out.endPacket();
    FakeClient rcv; rcv.rx = sender.tx; SLIPEncodedTCP in(rcv);
    OSCMessage got;
    bool ok = receiveOne(in, got);
    chk("escaped message round-trips",
        ok && !got.hasError() && got.fullMatch("/t/esc")
        && got.getInt(0)==(int32_t)0xC0DBC0DB && got.getInt(1)==-1); }

  { FakeClient sender; SLIPEncodedTCP out(sender);
    OSCMessage a("/a"); a.add((int32_t)1);
    OSCMessage b("/b"); b.add((int32_t)2);
    out.beginPacket(); a.send(out); out.endPacket();
    out.beginPacket(); b.send(out); out.endPacket();
    FakeClient rcv; rcv.rx = sender.tx; SLIPEncodedTCP in(rcv);
    OSCMessage got1, got2;
    bool ok1 = receiveOne(in, got1), ok2 = receiveOne(in, got2);
    chk("two packets in one stream decode as two messages",
        ok1 && ok2 && got1.fullMatch("/a") && got2.fullMatch("/b")
        && got1.getInt(0)==1 && got2.getInt(0)==2); }

  // the stream dries up mid-packet, then the rest arrives
  { FakeClient sender; SLIPEncodedTCP out(sender);
    OSCMessage m("/resume"); m.add((int32_t)0x11223344);
    out.beginPacket(); m.send(out); out.endPacket();
    std::vector<uint8_t> wire = sender.tx;
    size_t half = wire.size()/2;
    FakeClient rcv; SLIPEncodedTCP in(rcv);
    rcv.rx.assign(wire.begin(), wire.begin()+half);
    OSCMessage got;
    for (int spin=0; spin<100; ++spin)
      while (in.available()) { int c=in.read(); if(c>=0) got.fill((uint8_t)c); }
    chk("underrun mid-packet reads as -1, not 0xFF", in.read() == -1);
    rcv.rx.insert(rcv.rx.end(), wire.begin()+half, wire.end());
    bool done = false;
    for (int spin=0; spin<1000 && !done; ++spin) {
      while (in.available()) { int c=in.read(); if(c>=0) got.fill((uint8_t)c); }
      if (in.endofPacket()) done = true;
    }
    chk("reception resumes after the gap",
        done && !got.hasError() && got.fullMatch("/resume")
        && got.getInt(0)==0x11223344); }

  printf("\n%s\n", fails?"FAILURES":"all slip-tcp tests passed");
  return fails?1:0;
}

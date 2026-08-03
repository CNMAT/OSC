// SLIP over TCP against a stub Client: no shield, no network, no hardware.
//
// SLIPEncodedTCP began as a hand-maintained copy of the _SLIPSerial state
// machine and missed that template's fixes: read() and peek() narrowing -1
// to 0xFF, write(buf,n) returning the last byte's count, one client write
// per byte. Run against that copy, five checks here fail (both narrowing
// checks, the write count, the block check, and the underrun check, which
// is the narrowing again mid-packet); run against _SLIPSerial<Client> all
// pass. The rest pin the framing protocol itself: exact escaping,
// round-trips, back-to-back packets, underrun-and-resume.
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

  // a packet several times OSC_SLIP_TX_BUFFER, all of it escape pairs, so
  // pairs straddle the block-flush boundary; the wire bytes are compared
  // against a SLIP encoding computed here, not by the class under test
  { struct Cap : public Print { std::vector<uint8_t> b;
      size_t write(uint8_t c) override { b.push_back(c); return 1; } } plain;
    uint8_t blob[120];
    for (int i=0;i<120;i++) blob[i] = (i&1) ? 0xDB : 0xC0;
    OSCMessage m("/blk"); m.add(blob, (int)sizeof blob);
    m.send(plain);
    std::vector<uint8_t> expect;
    expect.push_back(0xC0);
    for (size_t i=0;i<plain.b.size();i++) {
      uint8_t c = plain.b[i];
      if (c==0xC0) { expect.push_back(0xDB); expect.push_back(0xDC); }
      else if (c==0xDB) { expect.push_back(0xDB); expect.push_back(0xDD); }
      else expect.push_back(c);
    }
    expect.push_back(0xC0);
    FakeClient sender; SLIPEncodedTCP out(sender);
    out.beginPacket(); m.send(out); out.endPacket();
    chk("escape pairs straddling block flushes encode exactly",
        sender.tx == expect && sender.writeCalls > 1);
    FakeClient rcv; rcv.rx = sender.tx; SLIPEncodedTCP in(rcv);
    OSCMessage got;
    bool ok = receiveOne(in, got);
    uint8_t back[120] = {0};
    bool blobOk = ok && !got.hasError() && got.getBlobLength(0)==sizeof blob
                  && got.getBlob(0, back, (int)sizeof back)>0
                  && !memcmp(back, blob, sizeof blob);
    chk("multi-block packet round-trips", blobOk); }

  // more than 255 bytes pending at once: available()'s narrowed count is
  // compensated by its peek()==-1 guard, and this pins that it stays so
  { FakeClient sender; SLIPEncodedTCP out(sender);
    uint8_t big[300];
    for (int i=0;i<300;i++) big[i] = (uint8_t)(i*7);
    OSCMessage m("/big"); m.add(big, (int)sizeof big);
    out.beginPacket(); m.send(out); out.endPacket();
    FakeClient rcv; rcv.rx = sender.tx; SLIPEncodedTCP in(rcv);
    OSCMessage got;
    bool ok = receiveOne(in, got);
    uint8_t back[300] = {0};
    chk("a packet with >255 bytes pending decodes completely",
        ok && !got.hasError() && got.getBlobLength(0)==sizeof big
        && got.getBlob(0, back, (int)sizeof back)>0
        && !memcmp(back, big, sizeof big)); }

  // peek() must translate an escape it is already inside, not show DC/DD raw
  { FakeClient c; SLIPEncodedTCP slip(c);
    c.rx.push_back(0xDB); c.rx.push_back(0xDC);
    slip.available();               // consumes the DB, enters the escape state
    chk("peek() inside an escape returns the decoded byte", slip.peek() == 0xC0);
    chk("read() then completes the escape",                 slip.read() == 0xC0); }

  // the stream dries up mid-packet -- cut BETWEEN an escape marker and its
  // second byte, the exact state a TCP segment boundary creates -- then the
  // rest arrives
  { FakeClient sender; SLIPEncodedTCP out(sender);
    OSCMessage m("/resume"); m.add((int32_t)0x11223344); m.add((int32_t)0xC0DBC0DB);
    out.beginPacket(); m.send(out); out.endPacket();
    std::vector<uint8_t> wire = sender.tx;
    size_t half = 0;
    while (half < wire.size() && wire[half] != 0xDB) ++half;
    ++half;                         // stream now ends just after the 0xDB
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
    chk("reception resumes across a split escape pair",
        done && !got.hasError() && got.fullMatch("/resume")
        && got.getInt(0)==0x11223344 && got.getInt(1)==(int32_t)0xC0DBC0DB); }

  printf("\n%s\n", fails?"FAILURES":"all slip-tcp tests passed");
  return fails?1:0;
}

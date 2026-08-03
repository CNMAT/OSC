// OSCBufferedPrint against a recording Print: no radio, no shield, no hardware.
//
// The class exists because send() writes a packet in many small pieces and
// some transports charge a round trip for each one. So the two things worth
// pinning are that the bytes are unchanged -- byte-for-byte against the same
// packet sent straight to the sink -- and that they arrive in one write.
//
// The rest is the awkward cases: a buffer smaller than the packet, a sink that
// takes less than it is offered (which truncated the packet silently before
// flush() learned to loop), a sink taking nothing at all, and a zero-length
// buffer, which must degrade to writing through rather than spin or drop.
#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCBufferedPrint.h"
#include <stdio.h>
#include <string.h>
#include <vector>
unsigned long millis(void){return 0;} unsigned long micros(void){return 0;}
osctime_t oscTime(){ osctime_t t={0,0}; return t; }

// records every byte and how many write() calls it took to get them
struct Recorder : public Print {
  std::vector<uint8_t> bytes;
  int calls = 0;
  size_t limit = 0;          // 0 = take everything offered
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    calls++;
    size_t take = (limit && n > limit) ? limit : n;
    bytes.insert(bytes.end(), b, b + take);
    return take;
  }
};

// a sink that refuses everything, then relents
struct Stubborn : public Print {
  std::vector<uint8_t> bytes;
  bool open = false;
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    if (!open) return 0;
    bytes.insert(bytes.end(), b, b + n);
    return n;
  }
};

static int fails=0;
static void chk(const char*n,bool ok){ if(!ok) fails++; printf("  %-52s %s\n",n,ok?"ok":"FAIL"); }

static OSCMessage sample(){
  OSCMessage msg("/analog/0");
  msg.add((intOSC_t)512);
  return msg;
}

int main(){
  // the packet the examples send, direct: this is the behaviour being replaced
  Recorder direct;
  { OSCMessage msg = sample(); msg.send(direct); }
  chk("a direct send costs one write per piece", direct.calls == 8);
  chk("  and 20 bytes go out in those 8 writes", direct.bytes.size() == 20);

  // the same packet, buffered
  { Recorder r; uint8_t buf[128];
    OSCMessage msg = sample();
    OSCBufferedPrint out(r, buf, sizeof buf);
    msg.send(out);
    chk("nothing reaches the sink before flush()", r.calls == 0 && out.pending() == 20);
    out.flush();
    chk("buffered, the packet leaves in one write", r.calls == 1);
    chk("the bytes are identical to the direct send", r.bytes == direct.bytes);
    chk("nothing is left pending", out.pending() == 0);
    out.flush();
    chk("flush() on an empty buffer is a no-op", r.calls == 1); }

  // a bundle costs more writes than a message, and buffers the same way
  Recorder bdirect;
  { OSCBundle bndl;
    bndl.add("/analog/0").add((intOSC_t)512);
    bndl.add("/micros").add((intOSC_t)99);
    bndl.send(bdirect); }
  { Recorder r; uint8_t buf[256];
    OSCBundle bndl;
    bndl.add("/analog/0").add((intOSC_t)512);
    bndl.add("/micros").add((intOSC_t)99);
    OSCBufferedPrint out(r, buf, sizeof buf);
    bndl.send(out);
    out.flush();
    chk("a bundle leaves in one write too", r.calls == 1);
    chk("a bundle's bytes are identical to the direct send", r.bytes == bdirect.bytes);
    chk("  and the direct bundle took a good many more", bdirect.calls > 8); }

  // a buffer smaller than the packet must cost writes, not data
  { Recorder r; uint8_t buf[8];
    OSCMessage msg = sample();
    OSCBufferedPrint out(r, buf, sizeof buf);
    msg.send(out);
    out.flush();
    chk("an undersized buffer still sends every byte", r.bytes == direct.bytes);
    chk("  in ceil(20/8) = 3 writes", r.calls == 3); }

  // a sink that takes 3 bytes per call: the tail of the packet must not be
  // left sitting in the buffer when flush() returns
  { Recorder r; r.limit = 3; uint8_t buf[128];
    OSCMessage msg = sample();
    OSCBufferedPrint out(r, buf, sizeof buf);
    msg.send(out);
    out.flush();
    chk("a short-writing sink still gets the whole packet", r.bytes == direct.bytes);
    chk("  and flush() leaves nothing behind", out.pending() == 0); }

  // a sink taking nothing must not spin, and must not lose what it refused
  { Stubborn s; uint8_t buf[16];
    OSCMessage msg = sample();
    OSCBufferedPrint out(s, buf, sizeof buf);
    msg.send(out);          // 20 bytes into a 16 byte buffer, sink refusing
    out.flush();
    chk("a refusing sink receives nothing", s.bytes.empty());
    chk("  and the bytes it refused are still pending", out.pending() == 16);
    s.open = true;
    out.flush();
    chk("once it relents the buffered bytes go out in order",
        s.bytes.size() == 16 &&
        memcmp(s.bytes.data(), direct.bytes.data(), 16) == 0); }

  // a zero-length buffer has nothing to reorder, so it writes through
  { Recorder r; uint8_t dummy = 0;
    OSCMessage msg = sample();
    OSCBufferedPrint out(r, &dummy, 0);
    msg.send(out);
    out.flush();
    chk("a zero-length buffer writes through unchanged", r.bytes == direct.bytes);
    chk("  at the direct send's cost", r.calls == direct.calls); }

  printf("%s\n", fails ? "FAILED" : "passed");
  return fails ? 1 : 0;
}

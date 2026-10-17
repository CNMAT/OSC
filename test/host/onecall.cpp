// The one-call forms -- msg.add(a, b, ...), OSCMessage("/x", a, b, ...) and
// bundle.add("/x", a, b, ...) -- must send exactly the bytes the chained
// single add() calls send. Each case builds both and compares them byte for
// byte, so this checks the argument typing and ordering, not the encoder;
// vectors.cpp puts one-call packets through the independent decoder.
//
// The blob cases are the reason this file exists. A list add() is a template
// that matches (uint8_t *, length) exactly for any length type, so a blob
// overload taking a plain int lost to it whenever the length was a size_t or
// a uint16_t, and the pointer went out as T. Measured on the prototype
// 2026-09-24: add(buf, sizeof buf) sent ,Th and add(buf, (uint16_t) 4) sent ,Ti.
#include "OSCMessage.h"
#include "OSCBundle.h"
#include <stdio.h>
#include <string.h>

unsigned long millis(void){ return 0; }
unsigned long micros(void){ return 0; }

struct Cap : public Print {
  uint8_t b[1024]; size_t n = 0;
  size_t write(uint8_t c) override { if (n < sizeof b) b[n++] = c; return 1; }
};

static int fails = 0, cases = 0;

template <typename A, typename B>
static void same(const char * name, A & oneCall, B & chained){
  Cap x, y; oneCall.send(x); chained.send(y); cases++;
  if (x.n == 0 || x.n != y.n || memcmp(x.b, y.b, x.n) != 0){
    printf("FAIL %s: one call sent %u bytes, the chain %u\n", name, (unsigned) x.n, (unsigned) y.n);
    fails++;
  }
}

static void types(const char * name, OSCMessage & m, const char * want){
  char got[32] = {0};
  for (int i = 0; i < m.size() && i < 31; i++) got[i] = (char) m.getType(i);
  cases++;
  if (strcmp(got, want) != 0 || m.hasError()){
    printf("FAIL %s: types %s, want %s%s\n", name, got, want, m.hasError() ? " (hasError)" : "");
    fails++;
  }
}

int main(){
  uint8_t frame[6] = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02};
  uint8_t other[3] = {7, 8, 9};
  char name[] = "abc";                       // a non-const char * is a string
  osctime_t t = {1, 2};
  oscrgba_t rgba = {1, 2, 3, 4};
  oscmidi_t midi = {0, 0x90, 0, 60, 100};    // port, status, channel, data1, data2
  uint32_t seq = 41;

  // --- the constructor
  { OSCMessage a("/mouse/xy", 312, 118);
    OSCMessage b("/mouse/xy"); b.add(312).add(118);
    same("ctor ints", a, b); types("ctor ints", a, "ii"); }
  { OSCMessage a("/synth/note", 60, 0.8f, "sine", true);
    OSCMessage b("/synth/note"); b.add(60).add(0.8f).add("sine").add(true);
    same("ctor mixed", a, b); types("ctor mixed", a, "ifsT"); }
  { OSCMessage a("/one", 5);                 // one argument after the address
    OSCMessage b("/one"); b.add(5);
    same("ctor one argument", a, b); }

  // --- add() with a list, mixed with chaining
  { OSCMessage a("/sensors"); a.add(512, 300).add(1.5f);
    OSCMessage b("/sensors"); b.add(512).add(300).add(1.5f);
    same("list then chain", a, b); types("list then chain", a, "iif"); }
  { OSCMessage a("/r"); OSCMessage & r = a.add(1, 2);
    cases++; if (&r != &a){ printf("FAIL list add does not return the message\n"); fails++; } }

  // --- every argument type through one list: each typed as add(x) types it
  { OSCMessage a("/all", (signed char) -1, (unsigned char) 255, (short) -2, (unsigned short) 65535,
                 (int) -3, (unsigned int) 3, (long) -4, (unsigned long) 4, (long long) -5,
                 (unsigned long long) 5, 1.5f, 2.5, "lit", name, true, false, t, rgba, midi,
                 OSC_IMPULSE, seq, (int64_t) 1 << 40);
    OSCMessage b("/all");
    b.add((signed char) -1).add((unsigned char) 255).add((short) -2).add((unsigned short) 65535)
     .add((int) -3).add((unsigned int) 3).add((long) -4).add((unsigned long) 4).add((long long) -5)
     .add((unsigned long long) 5).add(1.5f).add(2.5).add("lit").add(name).add(true).add(false)
     .add(t).add(rgba).add(midi).add(OSC_IMPULSE).add(seq).add((int64_t) 1 << 40);
    same("every type", a, b); }

  // --- blobs: every length type selects the blob, alone and in a list
  { OSCMessage a("/b"); a.add(frame, sizeof frame);
    OSCMessage b("/b"); b.add(frame, (int) sizeof frame);
    same("blob size_t length", a, b); types("blob size_t length", a, "b"); }
  { OSCMessage a("/b"); a.add(frame, (uint16_t) 6);
    types("blob uint16_t length", a, "b"); }
  { OSCMessage a("/b"); a.add(frame, (uint8_t) 6);
    types("blob uint8_t length", a, "b"); }
  { OSCMessage a("/b"); a.add(frame, 6L);
    types("blob long length", a, "b"); }
  { OSCMessage a("/b"); a.add(frame, 6ULL);
    types("blob unsigned long long length", a, "b"); }
  { OSCMessage a("/b"); a.add(&frame[0], 6);
    types("blob int length", a, "b"); }
  { OSCMessage a("/cam/jpeg", seq, frame, sizeof frame);
    OSCMessage b("/cam/jpeg"); b.add(seq).add(frame, 6);
    same("blob last in a list", a, b); types("blob last in a list", a, "ib"); }
  { OSCMessage a("/pkt", frame, (uint16_t) 4, "tail", 7);
    OSCMessage b("/pkt"); b.add(frame, 4).add("tail").add(7);
    same("blob first in a list", a, b); types("blob first in a list", a, "bsi"); }
  { OSCMessage a("/two"); a.add(frame, 2, other, sizeof other, 9);
    OSCMessage b("/two"); b.add(frame, 2).add(other, 3).add(9);
    same("two blobs in a row", a, b); types("two blobs in a row", a, "bbi"); }
  { OSCMessage a("/s"); a.add(name, 1);      // char *: a string, never a blob
    types("char * then int", a, "si"); }

  // --- the bundle: one call adds a message and returns it
  { OSCBundle a; a.add("/state", (intOSC_t) seq, (intOSC_t) 1000); a.add("/btn", 1);
    a.add("/display/frame", frame, 2 * 3);
    OSCBundle b; b.add("/state").add((intOSC_t) seq).add((intOSC_t) 1000); b.add("/btn").add(1);
    b.add("/display/frame").add(frame, 6);
    same("bundle", a, b); }
  { OSCBundle a; OSCMessage & m = a.add("/x", 1, 2).add(3);
    cases++;
    if (&m != a.getOSCMessage(0) || a.getOSCMessage(0)->size() != 3){
      printf("FAIL bundle add does not return the message in the bundle\n"); fails++;
    } }

  // --- a temporary built and sent in one statement
  { Cap x; OSCMessage("/btn", 3, true).send(x);
    OSCMessage b("/btn"); b.add(3).add(true); Cap y; b.send(y); cases++;
    if (x.n == 0 || x.n != y.n || memcmp(x.b, y.b, x.n) != 0){
      printf("FAIL temporary send\n"); fails++;
    } }

  if (fails) printf("%d of %d one-call cases failed\n", fails, cases);
  else printf("all %d one-call cases match their chains\n", cases);
  return fails ? 1 : 0;
}

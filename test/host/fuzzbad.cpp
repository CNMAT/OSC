// Malformed-input fuzz: throw hostile bytes at the decoders and then use every
// accessor on whatever comes out. Only meaningful under ASan/UBSan.
#include "OSCBundle.h"
#include <stdio.h>
#include <string.h>
unsigned long millis(void){return 0;} unsigned long micros(void){return 0;}
osctime_t oscTime(){ osctime_t t={0,0}; return t; }
struct Cap : public Print { uint8_t b[1024]; size_t n=0;
  size_t write(uint8_t c) override { if(n<sizeof b) b[n++]=c; return 1; } };

static unsigned long seed=987654321UL;
static unsigned rnd(unsigned m){ seed=seed*1103515245UL+12345UL; return (seed>>16)%(m?m:1); }

// hammer every accessor; none of these should read out of bounds whatever the
// message ended up containing
static void poke(OSCMessage &m){
  int n = m.size();
  for (int i = -2; i <= n + 2; i++){
    m.getInt(i); m.getInt64(i); m.getFloat(i); m.getDouble(i);
    m.getBoolean(i); m.getEvent(i); m.getTime(i); m.getRgba(i); m.getMidi(i);
    m.getType(i); m.isInt(i); m.isString(i); m.isBlob(i); m.getBlobLength(i);
    char sbuf[8]; m.getString(i, sbuf, sizeof sbuf);
    uint8_t bbuf[8]; m.getBlob(i, bbuf, sizeof bbuf);
    m.getString(i, sbuf, sizeof sbuf, 0, 4);
    m.getBlob(i, bbuf, sizeof bbuf, 0, 4);
  }
  m.getAddress(); m.hasError(); m.bytes();
  m.fullMatch("/a"); m.match("/a"); m.dispatch("/a", (void(*)(OSCMessage&))0);
  Cap c; m.send(c);
}

int main(){
  const int ROUNDS = 60000;
  // 1. pure random bytes
  for (int r=0; r<ROUNDS; r++){
    uint8_t buf[128]; int len = 1 + rnd(sizeof buf);
    for (int i=0;i<len;i++) buf[i] = (uint8_t) rnd(256);
    OSCMessage m; m.fill(buf, len); poke(m);
  }
  // 2. random bytes into a bundle
  for (int r=0; r<ROUNDS/2; r++){
    uint8_t buf[160]; int len = 1 + rnd(sizeof buf);
    for (int i=0;i<len;i++) buf[i] = (uint8_t) rnd(256);
    if (rnd(2)) memcpy(buf, "#bundle\0", 8 < len ? 8 : len);
    OSCBundle b; b.fill(buf, len);
    for (int i=0;i<b.size();i++){ OSCMessage* mm=b.getOSCMessage(i); if(mm) poke(*mm); }
    b.hasError(); Cap c; b.send(c);
  }
  // 3. mutate a VALID packet: bit flips, truncation, corrupted length fields
  for (int r=0; r<ROUNDS; r++){
    Cap good;
    { OSCMessage o("/mix");
      o.add((int32_t)7).add("hello").add(2.5f);
      uint8_t bl[5]={1,2,3,4,5}; o.add(bl, 1 + (int)rnd(5));
      o.add(OSC_IMPULSE).add((int64_t)-3);
      o.send(good); }
    uint8_t buf[512]; size_t len = good.n; memcpy(buf, good.b, len);
    int muts = 1 + rnd(4);
    for (int k=0;k<muts;k++){
      switch (rnd(3)){
        case 0: buf[rnd(len)] ^= (uint8_t)(1 << rnd(8)); break;   // bit flip
        case 1: buf[rnd(len)] = (uint8_t) rnd(256); break;        // byte splat
        case 2: if (len > 4) len = 4 + rnd(len - 4); break;       // truncate
      }
    }
    OSCMessage m; m.fill(buf, len); poke(m);
  }
  printf("survived %d rounds of malformed input\n", ROUNDS*5/2);
  return 0;
}

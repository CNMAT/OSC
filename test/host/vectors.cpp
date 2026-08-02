#include "OSCMessage.h"
#include "OSCBundle.h"
#include <stdio.h>
#include <string.h>

unsigned long millis(void){ return 0; }
unsigned long micros(void){ return 0; }
osctime_t oscTime(){ osctime_t t={0,0}; return t; }

struct Cap : public Print {
  uint8_t buf[4096]; size_t n=0;
  size_t write(uint8_t b) override { if(n<sizeof buf) buf[n++]=b; return 1; }
  void reset(){ n=0; }
};

static Cap cap;
static void emit(const char *name){
  printf("%s ", name);
  for(size_t i=0;i<cap.n;i++) printf("%02x", cap.buf[i]);
  printf("\n");
  cap.reset();
}

int main(){
  // --- bug 1 regression: every integer spelling must compile AND be exact-match
  { OSCMessage m("/i"); m.add((int32_t)-2147483647-1).add((int32_t)2147483647); m.send(cap); emit("int32_minmax"); }
  { OSCMessage m("/i"); m.add((intOSC_t)123456).add((int)-7).add((short)-2).add((signed char)-1); m.send(cap); emit("int_spellings"); }
  { OSCMessage m("/u"); m.add((uint32_t)4294967295u).add((unsigned int)0).add((unsigned short)65535).add((unsigned char)255); m.send(cap); emit("uint_spellings"); }
  // long: 8 bytes on this host, so it must come out as 'h'
  { OSCMessage m("/l"); m.add((long)1); m.send(cap); emit("long_host"); }
  // --- int64 correctness (the truncation bug)
  { OSCMessage m("/h"); m.add((int64_t)0x0123456789ABCDEFLL); m.send(cap); emit("int64_pattern"); }
  { OSCMessage m("/h"); m.add((int64_t)-1).add((int64_t)9223372036854775807LL); m.send(cap); emit("int64_minmax"); }
  { OSCMessage m("/h"); m.add((unsigned long long)18446744073709551615ULL); m.send(cap); emit("uint64_max"); }
  // --- other type codes
  { OSCMessage m("/f"); m.add(1.5f).add(-0.0f); m.send(cap); emit("floats"); }
  { OSCMessage m("/d"); m.add((double)1.5); m.send(cap); emit("double"); }
  { OSCMessage m("/b"); m.add(true).add(false); m.send(cap); emit("bools"); }
  // --- strings & padding residues
  { OSCMessage m("/s"); m.add("").add("a").add("ab").add("abc").add("abcd"); m.send(cap); emit("strings_pad"); }
  // --- blobs at every length mod 4
  for (int L=0; L<=5; ++L){
    uint8_t blob[8]; for(int i=0;i<L;i++) blob[i]=(uint8_t)(0xA0+i);
    OSCMessage m("/blob"); m.add(blob, L); m.send(cap);
    char nm[32]; snprintf(nm,sizeof nm,"blob_len%d",L); emit(nm);
  }
  // --- address lengths at every residue, empty arg list
  { OSCMessage m("/a");   m.send(cap); emit("addr3_noargs"); }
  { OSCMessage m("/ab");  m.send(cap); emit("addr4_noargs"); }
  { OSCMessage m("/abc"); m.send(cap); emit("addr5_noargs"); }
  { OSCMessage m("/abcd");m.send(cap); emit("addr6_noargs"); }
  // --- mixed, and a bundle
  { OSCMessage m("/mix"); m.add((int32_t)7).add("hi").add(2.5f).add((int64_t)-2); m.send(cap); emit("mixed"); }
  { OSCBundle b; b.add("/x").add((int32_t)1); b.add("/y").add((int64_t)2); b.send(cap); emit("bundle"); }
  return 0;
}

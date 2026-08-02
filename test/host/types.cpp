#include "OSCMessage.h"
#include "OSCMatch.h"
#include <stdio.h>
#include <string.h>
unsigned long millis(void){ return 0; }
unsigned long micros(void){ return 0; }
osctime_t oscTime(){ osctime_t t={0,0}; return t; }
struct Cap : public Print { uint8_t b[512]; size_t n=0;
  size_t write(uint8_t c) override { if(n<sizeof b) b[n++]=c; return 1; } };

// hand-build "/x" + typetag + payload, then feed to fill()
static void probe(char tag, const uint8_t*payload, size_t plen){
  uint8_t p[256]; size_t n=0;
  memcpy(p+n,"/x\0\0",4); n+=4;
  p[n++]=','; p[n++]=(uint8_t)tag; p[n++]=0; p[n++]=0;
  memcpy(p+n,payload,plen); n+=plen;
  OSCMessage m; m.fill(p,n);
  printf("  decode '%c': %-9s type=%-3s argcount=%d\n", tag,
         m.hasError()?"ERROR":"ok",
         m.hasError()?"-":(char[2]){(char)m.getType(0),0}, m.size());
}
int main(){
  static const uint8_t z8[8]={0,0,0,0,0,0,0,0};
  static const uint8_t s4[4]={'h','i',0,0};
  static const uint8_t b8[8]={0,0,0,1,0xAA,0,0,0};
  printf("=== ENCODE (can OSCData construct it?) ===\n");
  { Cap c; OSCMessage m("/x"); m.add((int32_t)1);   m.send(c); printf("  'i' int32   -> %c\n", m.getType(0)); }
  { Cap c; OSCMessage m("/x"); m.add((int64_t)1);   m.send(c); printf("  'h' int64   -> %c\n", m.getType(0)); }
  { Cap c; OSCMessage m("/x"); m.add(1.0f);         m.send(c); printf("  'f' float   -> %c\n", m.getType(0)); }
  { Cap c; OSCMessage m("/x"); m.add((double)1.0);  m.send(c); printf("  'd' double  -> %c\n", m.getType(0)); }
  { Cap c; OSCMessage m("/x"); m.add("hi");         m.send(c); printf("  's' string  -> %c\n", m.getType(0)); }
  { uint8_t bl[2]={1,2}; OSCMessage m("/x"); m.add(bl,2);      printf("  'b' blob    -> %c\n", m.getType(0)); }
  { OSCMessage m("/x"); m.add(true);                            printf("  'T' bool    -> %c\n", m.getType(0)); }
  { OSCMessage m("/x"); m.add(OSC_IMPULSE);                     printf("  'I' impulse -> %c\n", m.getType(0)); }
  { OSCMessage m("/x"); m.add(OSC_NULL);                        printf("  'N' null    -> %c\n", m.getType(0)); }
  { osctime_t t={1,2}; OSCMessage m("/x"); m.add(t);            printf("  't' timetag -> %c\n", m.getType(0)); }
  { oscrgba_t r={1,2,3,4}; OSCMessage m("/x"); m.add(r);        printf("  'r' rgba    -> %c\n", m.getType(0)); }
  { oscmidi_t d={1,2,3,4,5}; OSCMessage m("/x"); m.add(d);      printf("  'm' midi    -> %c\n", m.getType(0)); }
  printf("  'c' char    -> (no public ctor; add('x') hits the private placeholder)\n");
  printf("  'S' symbol  -> (no ctor at all)\n");
  printf("\n=== DECODE (does fill() accept it?) ===\n");
  probe('i', z8,4); probe('h', z8,8); probe('f', z8,4); probe('d', z8,8);
  probe('s', s4,4); probe('b', b8,8); probe('t', z8,8); probe('r', z8,4);
  probe('m', z8,4); probe('c', z8,4); probe('S', s4,4);
  probe('T', z8,0); probe('F', z8,0); probe('I', z8,0); probe('N', z8,0);
  printf("\n=== ADDRESS PATTERNS (osc_match) ===\n");
  const char* cases[][2] = {
    {"/a/b",   "/a/b"}, {"/a/*",  "/a/b"}, {"/a/?",  "/a/b"},
    {"/a/[bc]","/a/b"}, {"/a/{b,c}","/a/b"},
    {"//c",    "/a/b/c"}, {"//b",  "/a/b"}, {"/a//c", "/a/b/c"},
  };
  for (unsigned k=0;k<sizeof cases/sizeof *cases;k++){
    int po=0, ao=0; int r = osc_match(cases[k][0], cases[k][1], &po, &ao);
    bool full = (r & OSC_MATCH_ADDRESS_COMPLETE) && (r & OSC_MATCH_PATTERN_COMPLETE);
    printf("  pattern %-10s vs %-10s -> %s\n", cases[k][0], cases[k][1], full?"MATCH":"no match");
  }
  return 0;
}

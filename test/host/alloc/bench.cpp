#include "OSCBundle.h"
#include "count.h"
#include <stdio.h>
#include <string.h>
unsigned long millis(void){return 0;} unsigned long micros(void){return 0;}
osctime_t oscTime(){ osctime_t t={0,0}; return t; }
struct Cap : public Print { uint8_t b[2048]; size_t n=0;
  size_t write(uint8_t c) override { if(n<sizeof b) b[n++]=c; return 1; } };
static void run(const char*label, const Cap& c){
  g_realloc=g_malloc=g_free=g_peak=g_live=0;
  OSCMessage in; in.fill((uint8_t*)c.b, c.n);
  printf("  %-30s %4zu B wire | realloc %3d  malloc %2d  free %2d | peak %4d B\n",
         label, c.n, g_realloc, g_malloc, g_free, g_peak);
}
int main(){
  printf("allocator traffic while DECODING one packet:\n\n");
  { Cap c; OSCMessage m("/n"); m.add((int32_t)1); m.send(c); run("1 int", c); }
  { Cap c; OSCMessage m("/n"); for(int i=0;i<4;i++) m.add((int32_t)i); m.send(c); run("4 ints", c); }
  { Cap c; OSCMessage m("/n"); for(int i=0;i<16;i++) m.add((int32_t)i); m.send(c); run("16 ints (Esplora-like)", c); }
  { Cap c; OSCMessage m("/s"); m.add("a short string"); m.send(c); run("1 string (14 ch)", c); }
  { Cap c; OSCMessage m("/b"); uint8_t b[200]; memset(b,7,200); m.add(b,200); m.send(c); run("1 blob (200 B)", c); }
  { Cap c; OSCMessage m("/mix"); m.add((int32_t)1).add("hello").add(2.5f).add((int64_t)9); m.send(c);
    run("mixed i s f h", c); }
  return 0;
}

// Attacker-controlled length fields must not make the decoder grow without bound.
#include "OSCBundle.h"
#include <stdio.h>
#include <string.h>
unsigned long millis(void){return 0;} unsigned long micros(void){return 0;}
osctime_t oscTime(){ osctime_t t={0,0}; return t; }
static int fails=0;
static void chk(const char*n,bool ok){ if(!ok) fails++; printf("  %-54s %s\n",n,ok?"ok":"FAIL"); }
static void be32(uint8_t*p,uint32_t v){ p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }

int main(){
  printf("cap = %d bytes\n\n", OSC_MAX_INCOMING);
  // a blob claiming 4 GB: feed plenty of filler and check it stops, not grows
  { uint8_t p[64]; size_t n=0;
    memcpy(p+n,"/b\0\0",4); n+=4;
    memcpy(p+n,",b\0\0",4); n+=4;
    be32(p+n,0xFFFFFFF0u); n+=4;
    OSCMessage m; m.fill(p,n);
    for (int i=0;i<OSC_MAX_INCOMING*3;i++) m.fill((uint8_t)'A');
    chk("4 GB blob claim is refused, not grown into", m.hasError());
    chk("  and the error is BUFFER_FULL", m.getError()==BUFFER_FULL); }
  // a blob claiming INT_MAX+1
  { uint8_t p[64]; size_t n=0;
    memcpy(p+n,"/b\0\0",4); n+=4; memcpy(p+n,",b\0\0",4); n+=4;
    be32(p+n,0x80000000u); n+=4;
    OSCMessage m; m.fill(p,n);
    for (int i=0;i<OSC_MAX_INCOMING*3;i++) m.fill((uint8_t)'B');
    chk("2 GB blob claim is refused", m.hasError()); }
  // a bundle element claiming INT32_MAX
  { uint8_t p[32]; size_t n=0;
    memcpy(p+n,"#bundle\0",8); n+=8;
    memset(p+n,0,8); p[n+7]=1; n+=8;
    be32(p+n,0x7FFFFFFCu); n+=4;
    OSCBundle b; b.fill(p,n);
    for (int i=0;i<OSC_MAX_INCOMING*3;i++) b.fill((uint8_t)'C');
    chk("2 GB bundle element claim is refused", b.hasError()); }
  // a legitimate packet close to but under the cap still decodes
  { OSCMessage o("/ok");
    int payload = OSC_MAX_INCOMING/2;
    uint8_t *blob = (uint8_t*)malloc(payload);
    memset(blob,0x5A,payload);
    o.add(blob,payload);
    struct Cap : public Print { uint8_t b[OSC_MAX_INCOMING*2]; size_t n=0;
      size_t write(uint8_t c) override { if(n<sizeof b) b[n++]=c; return 1; } } c;
    o.send(c);
    OSCMessage in; in.fill(c.b,c.n);
    chk("a legitimate packet under the cap still decodes", !in.hasError() && in.size()==1);
    free(blob); }
  printf("\n%s\n", fails?"FAILURES":"length fields are bounded");
  return fails?1:0;
}

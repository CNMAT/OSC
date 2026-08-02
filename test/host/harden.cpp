#include "OSCBundle.h"
#include "OSCMatch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
unsigned long millis(void){ return 0; } unsigned long micros(void){ return 0; }
osctime_t oscTime(){ osctime_t t={0,0}; return t; }
int main(){
  // 1. malformed patterns must not read out of bounds (heap-allocated so ASan sees the edges)
  const char* mal[] = {"/a[bc","/a{b,c","/a]b","/a}b","[","]","{","}","/a[","/a{","/[]","/{}"};
  for (unsigned k=0;k<sizeof mal/sizeof *mal;k++){
    char* p = strdup(mal[k]);            // exact-size heap buffer
    int po=0, ao=0;
    int r = osc_match(p, "/abc", &po, &ao);
    printf("  pattern %-8s -> r=%d (no ASan report = ok)\n", mal[k], r);
    free(p);
  }
  // 2. negative index probes must not read data[-1]
  { OSCMessage m("/x"); m.add((int32_t)1);
    printf("\n  getOSCData(-1)  -> %p\n", (void*)m.getOSCData(-1));
    printf("  isInt(-5)       -> %d\n", (int)m.isInt(-5));
    printf("  getInt(-1)      -> %d\n", (int)m.getInt(-1));
    printf("  getType(-3)     -> %d\n", (int)m.getType(-3)); }
  // 3. bundle element with a negative size must be rejected
  { uint8_t p[24]; memset(p,0,sizeof p);
    memcpy(p,"#bundle\0",8);
    p[16]=0xFF; p[17]=0xFF; p[18]=0xFF; p[19]=0xFC;  // msgSize = -4
    OSCBundle b; b.fill(p,20);
    printf("\n  bundle w/ negative element size -> hasError=%d (want 1)\n", b.hasError()); }
  printf("\ncompleted without sanitizer abort\n");
  return 0;
}

#include "OSCMessage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
unsigned long millis(void){ return 0; } unsigned long micros(void){ return 0; }
osctime_t oscTime(){ osctime_t t={0,0}; return t; }
struct Cap : public Print { uint8_t b[2048]; size_t n=0;
  size_t write(uint8_t c) override { if(n<sizeof b) b[n++]=c; return 1; } };
static unsigned long seed=12345;
static unsigned rnd(unsigned m){ seed = seed*1103515245UL+12345UL; return (seed>>16)%m; }
static const char TAGS[]="ihfdsbTFIN";
int main(){
  int fails=0, total=0;
  for (int iter=0; iter<20000; iter++){
    int nargs = 1 + rnd(6);
    char tags[8]; int32_t iv[8]; int64_t hv[8]; float fv[8]; double dv[8];
    char sv[8][8]; uint8_t bv[8][6]; int bl[8];
    OSCMessage out("/f");
    for (int a=0;a<nargs;a++){
      char t = TAGS[rnd(sizeof TAGS - 1)]; tags[a]=t;
      switch(t){
        case 'i': iv[a]=(int32_t)(rnd(65536)*65536u+rnd(65536)); out.add(iv[a]); break;
        case 'h': hv[a]=((int64_t)rnd(65536)<<32)|rnd(65536);    out.add(hv[a]); break;
        case 'f': fv[a]=(float)(int)rnd(1000)/8.0f;              out.add(fv[a]); break;
        case 'd': dv[a]=(double)(int)rnd(1000)/16.0;             out.add(dv[a]); break;
        case 's': { int L=rnd(6); for(int k=0;k<L;k++) sv[a][k]='a'+rnd(26); sv[a][L]=0;
                    out.add(sv[a]); } break;
        case 'b': { bl[a]=rnd(6); for(int k=0;k<bl[a];k++) bv[a][k]=(uint8_t)rnd(256);
                    out.add(bv[a], bl[a]); } break;
        case 'T': out.add(true); break;
        case 'F': out.add(false); break;
        case 'I': out.add(OSC_IMPULSE); break;
        case 'N': out.add(OSC_NULL); break;
      }
    }
    Cap c; out.send(c);
    OSCMessage in; in.fill(c.b, c.n);
    total++;
    bool ok = !in.hasError() && in.size()==nargs;
    for (int a=0; ok && a<nargs; a++){
      if (in.getType(a) != (tags[a]=='T'||tags[a]=='F' ? tags[a] : tags[a])) { ok=false; break; }
      switch(tags[a]){
        case 'i': ok = (in.getInt(a)==iv[a]); break;
        case 'h': ok = (in.getInt64(a)==hv[a]); break;
        case 'f': ok = (in.getFloat(a)==fv[a]); break;
        case 'd': ok = (in.getDouble(a)==dv[a]); break;
        case 's': { char t2[16]={0}; in.getString(a,t2,sizeof t2); ok = (strcmp(t2,sv[a])==0); } break;
        case 'b': { uint8_t t2[16]={0}; int got=in.getBlob(a,t2,sizeof t2);
                    ok = (got==bl[a]) && (memcmp(t2,bv[a],bl[a])==0); } break;
        case 'T': ok = (in.getBoolean(a)==true); break;
        case 'F': ok = (in.getBoolean(a)==false); break;
        case 'I': ok = (in.getEvent(a)==OSC_IMPULSE); break;
        case 'N': ok = (in.getEvent(a)==OSC_NULL); break;
      }
    }
    if(!ok){ if(fails<6){ printf("  MISMATCH iter=%d spec=", iter);
        for(int a=0;a<nargs;a++){ putchar(tags[a]);
          if(tags[a]=='s') printf("(%d)",(int)strlen(sv[a]));
          if(tags[a]=='b') printf("(%d)",bl[a]); }
        printf(" err=%d size=%d/%d wire=", in.hasError(), in.size(), nargs);
        for(size_t q=0;q<c.n;q++) printf("%02x", c.b[q]); printf("\n"); } fails++; }
  }
  printf("\n%d messages round-tripped, %d mismatches\n", total, fails);
  return fails?1:0;
}

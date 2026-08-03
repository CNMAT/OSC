#include "OSCBundle.h"
#include <stdio.h>
#include <string.h>
unsigned long millis(void){return 0;} unsigned long micros(void){return 0;}
osctime_t oscTime(){ osctime_t t={0,0}; return t; }
struct Cap : public Print { uint8_t b[512]; size_t n=0;
  size_t write(uint8_t c) override { if(n<sizeof b) b[n++]=c; return 1; } };
static int fails=0;
static void chk(const char*n,bool ok){ if(!ok) fails++; printf("  %-52s %s\n",n,ok?"ok":"FAIL"); }
static int cbCount = 0;
static void cb(OSCMessage &m, int off){ (void)off; cbCount++; m.add((int32_t)99); }
int main(){
  // the README example: bind add()'s reference to a value. Used to double free.
  { OSCBundle b;
    OSCMessage copy = b.add("/a");        // implicit copy ctor -> now deep
    copy.add("some data");
    chk("copy-by-value does not corrupt the bundle", b.size()==1);
    chk("data went to the copy, bundle message still empty",
        b.getOSCMessage(0)->size()==0);
    chk("the copy has its own data", copy.size()==1); }
  // copy assignment
  { OSCMessage a("/x"); a.add((int32_t)1).add("two");
    OSCMessage c("/y"); c.add(9.5f);
    c = a;
    chk("assignment deep-copies", c.size()==2 && c.getInt(0)==1);
    c.add((int32_t)3);
    chk("assignment left the source alone", a.size()==2); }
  // self-assignment
  { OSCMessage a("/s"); a.add((int32_t)7);
    a = a;
    chk("self-assignment is safe", a.size()==1 && a.getInt(0)==7); }
  // route/dispatch now operate in place, so callbacks can mutate
  { OSCBundle b; b.add("/d/1").add((int32_t)5);
    cbCount=0; b.route("/d", cb);
    chk("route reached the callback", cbCount==1);
    chk("callback mutation is kept (no temporary copy)",
        b.getOSCMessage(0)->size()==2); }
  // geometric growth still yields correct data for many args
  { OSCMessage m("/many");
    for (int i=0;i<40;i++) m.add((int32_t)i);
    bool ok = (m.size()==40);
    for (int i=0;i<40 && ok;i++) ok = (m.getInt(i)==i);
    chk("40 arguments survive doubling", ok);
    Cap c; m.send(c);
    OSCMessage in; in.fill(c.b,c.n);
    bool ok2 = !in.hasError() && in.size()==40;
    for (int i=0;i<40 && ok2;i++) ok2 = (in.getInt(i)==i);
    chk("40 arguments round-trip on the wire", ok2); }
  printf("\n%s\n", fails?"FAILURES":"all copy/growth tests passed");
  return fails?1:0;
}

// Calls that must NOT compile. Before the pointer guard in OSCData.h every one
// of them compiled and sent T, because a pointer converts silently to bool;
// the const blobs did not compile before either, and must stay refused because
// on AVR a PROGMEM table is a const uint8_t *.
//
// The Makefile compiles this once with no case selected, which must succeed,
// then once per case, which must fail with the guard's own message -- so a
// case that "fails" for an unrelated reason fails the suite too.
#include "OSCMessage.h"
#include "OSCBundle.h"

unsigned long millis(void){ return 0; }
unsigned long micros(void){ return 0; }

uint8_t frame[4] = {1, 2, 3, 4};
const uint8_t cframe[4] = {1, 2, 3, 4};
int number = 5;
int * ip = &number;
void * vp = &number;

int main(){
  OSCMessage m("/m");
  OSCBundle b;
  // the valid twin of every case below
  m.add(1, frame, 4).add(frame, sizeof frame).set(0, frame, 4);
  b.add("/x", frame, 4);
  (void) cframe;
#if CASE == 1
  m.add(1, frame);                          // a blob without its length
#elif CASE == 2
  m.add(frame);                             // a lone blob pointer: used to send T
#elif CASE == 3
  m.set(0, frame);                          // the same through set()
#elif CASE == 4
  m.add(cframe, 4);                         // a const blob
#elif CASE == 5
  OSCMessage n("/x", cframe, sizeof cframe); // a const blob through the constructor
#elif CASE == 6
  b.add("/x", frame);                       // the bundle's one call, no length
#elif CASE == 7
  m.add(ip);                                // any other pointer
#elif CASE == 8
  m.add(vp);                                // void *
#endif
  return 0;
}

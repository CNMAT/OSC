/*
 * The two transport helpers live here rather than in the .ino because the
 * Arduino build generates function prototypes and inserts them ABOVE the
 * file's own declarations. A template's prototype hoisted above its
 * `template<class T>` line does not compile ("'T' was not declared"), and
 * that is not a mistake you can fix inside the sketch. Headers are left
 * alone by the generator, so templates belong in one.
 *
 * Both are generic over any _SLIPSerial<T>: the USB one and the BLE one
 * take exactly the same code, which is the whole claim this example makes.
 */
#pragma once
#include <OSCBundle.h>

// Non-blocking receive. Returns true once a whole packet sits in `in`.
// ORDERING MATTERS: endofPacket() must be called before available() on
// every pass, or the SLIP state machine eats the packet boundary.
template <class T>
static bool pollOSC(T &port, OSCBundle &in) {
  while (!port.endofPacket()) {
    int size = port.available();
    if (size <= 0) return false;            // nothing buffered — try later
    while (size--) {
      int c = port.read();
      if (c >= 0) in.fill((uint8_t)c);      // read() returns -1 on SLIP error
    }
  }
  return true;
}

// Send whatever is queued to one transport, then clear it.
template <class T>
static void flushTo(T &port, OSCBundle &out) {
  if (out.size() > 0) {
    port.beginPacket();
    out.send(port);
    port.endPacket();
    out.empty();
  }
}

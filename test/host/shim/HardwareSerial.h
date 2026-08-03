#ifndef SHIM_HARDWARESERIAL_H
#define SHIM_HARDWARESERIAL_H
#include "Stream.h"
// only named by the SLIPEncodedSerial alias; never instantiated on the host
class HardwareSerial : public Stream {
public:
  void begin(unsigned long) {}
  virtual void flush() {}
};
#endif

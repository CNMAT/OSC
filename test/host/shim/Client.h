#ifndef SHIM_CLIENT_H
#define SHIM_CLIENT_H
#include "Stream.h"
// the slice of Arduino's Client that the SLIP transport touches
class Client : public Stream { public: virtual void flush() {} };
#endif

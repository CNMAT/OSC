#ifndef SHIM_STREAM_H
#define SHIM_STREAM_H
#include "Print.h"
class Stream : public Print { public: virtual int available()=0; virtual int read()=0; virtual int peek()=0; };
#endif

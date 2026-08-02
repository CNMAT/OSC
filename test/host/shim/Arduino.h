#ifndef SHIM_ARDUINO_H
#define SHIM_ARDUINO_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "Print.h"
typedef bool boolean;
unsigned long millis(void);
unsigned long micros(void);
#endif

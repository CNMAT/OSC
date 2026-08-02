#ifndef SHIM_PRINT_H
#define SHIM_PRINT_H
#include <stddef.h>
#include <stdint.h>
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n){ size_t r=0; while(n--) r+=write(*b++); return r; }
};
#endif

// Minimal Print shim: collects everything written into a growable buffer.
#ifndef HOST_PRINT_H
#define HOST_PRINT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

class Print {
public:
    uint8_t * buf;
    size_t len;
    size_t cap;

    Print() : buf(NULL), len(0), cap(0) {}
    ~Print() { free(buf); }

    void reset() { len = 0; }

    size_t write(uint8_t b) { return write(&b, 1); }

    size_t write(const uint8_t * data, size_t n) {
        if (len + n > cap) {
            cap = (len + n) * 2 + 64;
            buf = (uint8_t *) realloc(buf, cap);
        }
        memcpy(buf + len, data, n);
        len += n;
        return n;
    }

    // the library calls write(ptr, int) in places
    size_t write(const uint8_t * data, int n) { return write(data, (size_t) n); }
};

#endif

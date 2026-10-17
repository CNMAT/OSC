// What set() leaves behind when its replacement cannot be allocated. Only
// meaningful under AddressSanitizer; see README.md for the build line.
//
// OSCData.cpp is compiled into this file with malloc redirected, so any one of
// its payload allocations -- string and blob bytes -- can be made to fail. At
// c72084d:
//   blob set()      deleted the old datum first, so data[position] was left
//                   pointing at freed memory: heap-use-after-free on the next
//                   read, and ~OSCMessage() deleted it again.
//   blob, decoded   the decoder stores blobs through the same set(); the next
//                   byte of the packet read the freed datum inside fill().
//   set(), string   the template set() built first, but deleted the failed
//                   replacement, and ~OSCData() freed its never-initialized
//                   pointer: SEGV in free().
//   set(), clone    the same, through OSCData(OSCData *).

#include "OSCBundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failNext = 0;    // the next OSCData payload malloc returns NULL
static int failed = 0;      // how many actually did
static void * failableMalloc(size_t n) {
    if (failNext) { failNext = 0; failed++; return NULL; }
    return malloc(n);
}
#define malloc failableMalloc
#include "OSCData.cpp"
#undef malloc

unsigned long millis(void) { return 0; }
unsigned long micros(void) { return 0; }
osctime_t oscTime() { osctime_t t = {0, 0}; return t; }

struct Cap : public Print {
    uint8_t b[512];
    size_t n = 0;
    size_t write(uint8_t c) override { if (n < sizeof b) b[n++] = c; return 1; }
};

static int failures = 0;
static void check(const char *name, bool ok) {
    if (!ok) failures++;
    printf("  %-56s %s\n", name, ok ? "ok" : "FAIL");
}

// hasError() is message-wide and the getters return 0 once it is set, so the
// data are read through OSCData, which does not look at the message's error
static bool blobAt(OSCMessage &m, int i, const uint8_t *want, int n) {
    uint8_t got[128];
    return m.getOSCData(i)->getBlob(got, sizeof got) == n && memcmp(got, want, n) == 0;
}
static bool stringAt(OSCMessage &m, int i, const char *want) {
    char got[16] = {0};
    return m.getOSCData(i)->getString(got, sizeof got) > 0 && strcmp(got, want) == 0;
}

static void blobSet() {
    printf("blob set() whose replacement cannot be allocated\n");
    uint8_t a[4] = {1, 2, 3, 4}, b[4] = {9, 9, 9, 9};
    OSCMessage m("/s");
    m.add(a, 4).add((int32_t)7);
    failed = 0; failNext = 1;
    m.set(0, b, 4);
    check("the allocation failed", failed == 1);
    check("set() reports ALLOCFAILED", m.getError() == ALLOCFAILED);
    check("the old blob is still there", m.size() == 2 && blobAt(m, 0, a, 4));
    check("and so is its neighbour", m.getOSCData(1)->getInt() == 7);
}   // ~OSCMessage() frees data[0]: once, not twice

static void blobDecoded() {
    printf("a received blob that cannot be copied\n");
    uint8_t blob[100];
    for (int k = 0; k < 100; k++) blob[k] = (uint8_t)k;
    Cap c;
    { OSCMessage o("/b"); o.add(blob, 100).add((int32_t)7); o.send(c); }
    OSCMessage in;
    failed = 0; failNext = 1;
    in.fill(c.b, c.n);
    check("the allocation failed", failed == 1);
    check("the message reports ALLOCFAILED", in.getError() == ALLOCFAILED);
    in.empty();
    in.fill(c.b, c.n);
    check("after empty() the same message decodes it",
          !in.hasError() && in.size() == 2 && blobAt(in, 0, blob, 100) &&
          in.getInt(1) == 7);
}

static void stringSet() {
    printf("template set() whose replacement cannot be allocated\n");
    OSCMessage m("/s");
    m.add("abc").add((int32_t)7);
    failed = 0; failNext = 1;
    m.set(0, "xyz");
    check("the allocation failed", failed == 1);
    check("set() reports ALLOCFAILED", m.getError() == ALLOCFAILED);
    check("the old string is still there", m.size() == 2 && stringAt(m, 0, "abc"));
}

static void cloneSet() {
    printf("template set() of an OSCData whose clone cannot be allocated\n");
    OSCMessage src("/t");
    src.add("xyz");
    OSCMessage m("/s");
    m.add("abc");
    failed = 0; failNext = 1;
    m.set(0, src.getOSCData(0));
    check("the allocation failed", failed == 1);
    check("set() reports ALLOCFAILED", m.getError() == ALLOCFAILED);
    check("the old string is still there", m.size() == 1 && stringAt(m, 0, "abc"));
}

int main() {
    blobSet();
    blobDecoded();
    stringSet();
    cloneSet();
    printf("\n%s\n", failures ? "FAILURES" : "every allocation failure left the message intact");
    return failures ? 1 : 0;
}

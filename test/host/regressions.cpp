// Named regression tests. Every case here corresponds to a bug that shipped in
// a released version of this library. Each one failed before its fix and passes
// after; none of them was covered by the on-device test/ sketches.
//
// Build and run with `make` in this directory.

#include "OSCBundle.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

unsigned long millis(void) { return 0; }
unsigned long micros(void) { return 0; }
osctime_t oscTime() { osctime_t t = {0, 0}; return t; }

struct Cap : public Print {
    uint8_t b[512];
    size_t n = 0;
    size_t write(uint8_t c) override { if (n < sizeof b) b[n++] = c; return 1; }
    void reset() { n = 0; }
};

static int failures = 0;
static void check(const char *name, bool ok) {
    if (!ok) failures++;
    printf("  %-56s %s\n", name, ok ? "ok" : "FAIL");
}

// ---------------------------------------------------------------------------
// 'I' (impulse) and 'N' (null) could be encoded but not decoded. OSCData(char)
// treated only 'T'/'F' as valid zero-byte types, so an inbound 'I' or 'N' stayed
// INVALID_OSC forever. hasError() is message-wide, so a single impulse threw
// away every other argument in the message too.
// ---------------------------------------------------------------------------
static void zeroByteTypesDecode() {
    printf("zero-byte types decode\n");
    Cap c;
    { OSCMessage o("/e"); o.add(OSC_IMPULSE); o.send(c);
      OSCMessage in; in.fill(c.b, c.n);
      check("impulse round-trips", !in.hasError() && in.getType(0) == 'I' &&
                                   in.getEvent(0) == OSC_IMPULSE); c.reset(); }
    { OSCMessage o("/e"); o.add(OSC_NULL); o.send(c);
      OSCMessage in; in.fill(c.b, c.n);
      check("null round-trips", !in.hasError() && in.getType(0) == 'N' &&
                                in.getEvent(0) == OSC_NULL); c.reset(); }
    { OSCMessage o("/e");
      o.add((int32_t)7).add(OSC_IMPULSE).add("hi").add(OSC_NULL).add(2.5f);
      o.send(c);
      OSCMessage in; in.fill(c.b, c.n);
      char s[8] = {0}; in.getString(2, s, sizeof s);
      check("impulse/null do not discard their neighbours",
            !in.hasError() && in.size() == 5 && in.getInt(0) == 7 &&
            in.getEvent(1) == OSC_IMPULSE && strcmp(s, "hi") == 0 &&
            in.getEvent(3) == OSC_NULL && in.getFloat(4) == 2.5f); c.reset(); }
    { uint8_t p[8]; memcpy(p, "/x\0\0", 4); p[4] = ','; p[5] = 'Z'; p[6] = 0; p[7] = 0;
      OSCMessage in; in.fill(p, 8);
      check("a genuinely unknown tag is still rejected", in.hasError()); }
}

// ---------------------------------------------------------------------------
// DATA_PADDING scanned backwards for "the datum just finished" using only
// error == OSC_OK. Zero-byte types are OSC_OK from birth, so one sitting after a
// string or blob won the scan; padSize(0) reported no padding and the string's
// pad byte was consumed as the first byte of the next argument, shifting every
// remaining argument by one. No error was raised.
// ---------------------------------------------------------------------------
static void paddingAfterZeroByteType() {
    printf("padding when a zero-byte type follows a string or blob\n");
    Cap c;
    struct { const char *name; char tag; } cases[] = {
        {"s T f", 'T'}, {"s F f", 'F'}, {"s I f", 'I'}, {"s N f", 'N'},
    };
    for (unsigned k = 0; k < sizeof cases / sizeof *cases; k++) {
        OSCMessage o("/e");
        o.add("hi");
        switch (cases[k].tag) {
            case 'T': o.add(true); break;
            case 'F': o.add(false); break;
            case 'I': o.add(OSC_IMPULSE); break;
            case 'N': o.add(OSC_NULL); break;
        }
        o.add(2.5f);
        o.send(c);
        OSCMessage in; in.fill(c.b, c.n);
        check(cases[k].name, !in.hasError() && in.getFloat(2) == 2.5f);
        c.reset();
    }
    { uint8_t bl[2] = {1, 2}; OSCMessage o("/e");
      o.add(bl, 2); o.add(true).add(2.5f); o.send(c);
      OSCMessage in; in.fill(c.b, c.n);
      check("b T f", !in.hasError() && in.getFloat(2) == 2.5f); c.reset(); }
}

// ---------------------------------------------------------------------------
// The blob decoder required incomingBufferSize > 4, which a zero-length blob
// (four bytes of length prefix, no payload) never reaches. The library emitted
// such blobs but could not read them back.
// ---------------------------------------------------------------------------
static void blobLengths() {
    printf("blob lengths 0..5\n");
    Cap c;
    for (int L = 0; L <= 5; L++) {
        uint8_t bl[8];
        for (int k = 0; k < L; k++) bl[k] = (uint8_t)(0xA0 + k);
        OSCMessage o("/b"); o.add(bl, L); o.send(c);
        OSCMessage in; in.fill(c.b, c.n);
        uint8_t got[8] = {0};
        int n = in.hasError() ? -1 : in.getBlob(0, got, sizeof got);
        char nm[32]; snprintf(nm, sizeof nm, "blob length %d", L);
        check(nm, !in.hasError() && n == L && memcmp(got, bl, L) == 0);
        c.reset();
    }
}

// ---------------------------------------------------------------------------
// When the previous argument needed no padding, DATA_PADDING kept the byte in
// the buffer but never handed it to decodeData(). The 's' handler triggers on
// the byte's value, and an empty string is nothing but its terminating null, so
// that null was never seen: the empty string swallowed the next argument's
// first byte.
// ---------------------------------------------------------------------------
static void emptyString() {
    printf("empty string after a 4-aligned argument\n");
    Cap c;
    { OSCMessage o("/e"); o.add("abc").add("").add(2.5f); o.send(c);
      OSCMessage in; in.fill(c.b, c.n);
      char s[8] = {1};
      if (!in.hasError()) in.getString(1, s, sizeof s);
      check("s(3) s(0) f", !in.hasError() && s[0] == 0 && in.getFloat(2) == 2.5f);
      c.reset(); }
    { uint8_t bl[4] = {1, 2, 3, 4}; OSCMessage o("/e");
      o.add(bl, 4); o.add("").add(2.5f); o.send(c);
      OSCMessage in; in.fill(c.b, c.n);
      check("b(4) s(0) f", !in.hasError() && in.getFloat(2) == 2.5f); c.reset(); }
}

// ---------------------------------------------------------------------------
// 'h' was stored into the 32-bit union member, read back from it, and sent by
// byte-swapping 32 bits and then writing 8 bytes from a 4-byte stack local.
// ---------------------------------------------------------------------------
static void int64Values() {
    printf("64-bit integers\n");
    Cap c;
    const long long vals[] = {0LL, 1LL, -1LL, 0x0123456789ABCDEFLL,
                              -9223372036854775807LL - 1, 9223372036854775807LL};
    for (unsigned k = 0; k < sizeof vals / sizeof *vals; k++) {
        OSCMessage o("/h"); o.add((int64_t)vals[k]); o.send(c);
        OSCMessage in; in.fill(c.b, c.n);
        char nm[48]; snprintf(nm, sizeof nm, "int64 %lld", vals[k]);
        check(nm, !in.hasError() && in.isInt64(0) &&
                  (long long)in.getInt64(0) == vals[k]);
        c.reset();
    }
}

// ---------------------------------------------------------------------------
// OSCBundle::empty() did not reset decodeState, so a long-lived bundle refilled
// in loop() accepted one packet and rejected every packet after it. The bundle
// timetag arrived byte-reversed because setTimetag(uint8_t*) memcpy'd the two
// network-order words in raw.
// ---------------------------------------------------------------------------
static void bundleReuseAndTimetag() {
    printf("bundle reuse and timetag byte order\n");
    Cap c;
    OSCBundle out; out.add("/x").add((int32_t)42); out.send(c);
    OSCBundle in;
    in.fill(c.b, c.n);
    check("first fill decodes", !in.hasError() && in.size() == 1);
    in.empty();
    in.fill(c.b, c.n);
    check("same bundle decodes again after empty()", !in.hasError() && in.size() == 1);

    osctime_t t; t.seconds = 0x11223344; t.fractionofseconds = 0x55667788;
    Cap c2; OSCBundle o2(t); o2.add("/y").add((int32_t)1); o2.send(c2);
    OSCBundle i2; i2.fill(c2.b, c2.n);
    Cap c3; i2.send(c3);
    check("timetag survives decode and re-send",
          c3.n >= 16 && memcmp(c2.b + 8, c3.b + 8, 8) == 0);
}

// ---------------------------------------------------------------------------
// 'r' and 'm' were routed through the generic BigEndian() byte-reverser, which
// is right for scalars and wrong for these two: both are byte arrays already in
// wire order. Colours and MIDI therefore went out reversed, and 'm' also sent
// only the first four bytes of a five-field struct, dropping data2 (velocity)
// and transmitting a `channel` byte that OSC's 'm' has no slot for. Encoder and
// decoder were reversed symmetrically, so CNMAT-to-CNMAT links looked fine and
// the fault was invisible unless you talked to other OSC software.
// Ported from the wire-format tests written alongside the 4.0.0 fix.
// ---------------------------------------------------------------------------
static void rgbaAndMidiWireOrder() {
    printf("'r' and 'm' wire byte order\n");
    Cap c;
    // OSC 1.0 'r' is r,g,b,a in that order
    { oscrgba_t col = {0x11, 0x22, 0x33, 0x44};
      OSCMessage o("/r"); o.add(col); o.send(c);
      const uint8_t want[4] = {0x11, 0x22, 0x33, 0x44};
      check("rgba encodes r,g,b,a in order", c.n >= 12 && memcmp(c.b + 8, want, 4) == 0);
      OSCMessage in; in.fill(c.b, c.n);
      oscrgba_t g = in.getRgba(0);
      check("rgba round-trips", !in.hasError() && g.r == 0x11 && g.g == 0x22 &&
                                g.b == 0x33 && g.a == 0x44);
      c.reset(); }
    // OSC 1.0 'm' is port, status, data1, data2; channel folds into status
    { oscmidi_t m = {1, 0x90, 2, 0x3C, 0x40};
      OSCMessage o("/m"); o.add(m); o.send(c);
      const uint8_t want[4] = {0x01, 0x92, 0x3C, 0x40};
      check("midi encodes port,status|channel,data1,data2",
            c.n >= 12 && memcmp(c.b + 8, want, 4) == 0);
      c.reset(); }
    { oscmidi_t m = {1, 0x92, 0, 0x3C, 0x40};   // channel already in status
      OSCMessage o("/m"); o.add(m); o.send(c);
      const uint8_t want[4] = {0x01, 0x92, 0x3C, 0x40};
      check("midi channel already folded into status", c.n >= 12 &&
            memcmp(c.b + 8, want, 4) == 0);
      c.reset(); }
    { oscmidi_t m = {0, 0xF2, 0, 0x2A, 0x01};   // song position: no channel nibble
      OSCMessage o("/m"); o.add(m); o.send(c);
      const uint8_t want[4] = {0x00, 0xF2, 0x2A, 0x01};
      check("midi system-common status left intact", c.n >= 12 &&
            memcmp(c.b + 8, want, 4) == 0);
      c.reset(); }
    { oscmidi_t m = {1, 0x90, 2, 0x3C, 0x40};
      OSCMessage o("/m"); o.add(m); o.send(c);
      OSCMessage in; in.fill(c.b, c.n);
      Cap c2; in.send(c2);
      check("midi decode then re-encode is byte-identical",
            !in.hasError() && c2.n == c.n && memcmp(c.b, c2.b, c.n) == 0);
      c.reset(); }
}

// ---------------------------------------------------------------------------
// A bundle built without an explicit timetag sent 0. OSC reserves 0x...01 for
// "immediately"; 0 is a real absolute time (1900-01-01).
// ---------------------------------------------------------------------------
static void defaultBundleTimetag() {
    printf("default bundle timetag\n");
    OSCBundle b;
    osctime_t t = b.getTimetag();
    check("a bundle with no timetag reports \"immediately\" (0,1)",
          t.seconds == 0 && t.fractionofseconds == 1);
    Cap c; b.add("/x").add((int32_t)1); b.send(c);
    const uint8_t want[8] = {0, 0, 0, 0, 0, 0, 0, 1};
    check("and puts 0x0000000000000001 on the wire",
          c.n >= 16 && memcmp(c.b + 8, want, 8) == 0);
}

// ---------------------------------------------------------------------------
// The blob set() had no lower bound. 00ae88c gave the template set() one and
// missed this overload, so set(-1, blob, n) stored its new datum at data[-1],
// in front of the array. With arguments present that write is invisible except
// under `make asan` (heap-buffer-overflow, WRITE of size 8); with none yet the
// array is NULL and it faults in any build.
// ---------------------------------------------------------------------------
static void blobSetNegativePosition() {
    printf("blob set() out of bounds\n");
    uint8_t bl[4] = {1, 2, 3, 4};
    { OSCMessage m("/s"); m.add((int32_t)1).add((int32_t)2);
      m.set(-1, bl, 4);
      check("set(-1, blob) is INDEX_OUT_OF_BOUNDS",
            m.getError() == INDEX_OUT_OF_BOUNDS);
      check("and writes nothing", m.size() == 2 &&
            m.getOSCData(0)->getInt() == 1 && m.getOSCData(1)->getInt() == 2); }
    { OSCMessage m("/s");
      m.set(-1, bl, 4);
      check("set(-1, blob) on a message with no arguments",
            m.getError() == INDEX_OUT_OF_BOUNDS && m.size() == 0); }
    { OSCMessage m("/s"); m.add((int32_t)1);
      m.set(2, bl, 4);
      check("set(size() + 1, blob) is INDEX_OUT_OF_BOUNDS too",
            m.getError() == INDEX_OUT_OF_BOUNDS && m.size() == 1); }
}

// i 7, b <want>, s "tail", and nothing else
static bool intBlobString(OSCMessage &m, const uint8_t *want, int n) {
    uint8_t got[16] = {0}; char s[8] = {0};
    return !m.hasError() && m.size() == 3 && m.getInt(0) == 7 &&
           m.getBlob(1, got, sizeof got) == n && memcmp(got, want, n) == 0 &&
           m.getString(2, s, sizeof s) == 5 && strcmp(s, "tail") == 0;
}

// ---------------------------------------------------------------------------
// The same set() destroyed the old datum before building its replacement, so
// a failed allocation left data[position] pointing at freed memory. It now
// builds first, as the template set() does. Allocation failure itself needs a
// failing malloc (alloc/setfail.cpp); this pins what the reordering must keep:
// a replacement of a different padded length, with a sizeof length, touches
// only its own position and still encodes.
// ---------------------------------------------------------------------------
static void blobSetKeepsTheRest() {
    printf("blob set() replaces only its own position\n");
    uint8_t a[3] = {1, 2, 3}, b[6] = {9, 8, 7, 6, 5, 4};
    OSCMessage m("/s");
    m.add((int32_t)7).add(a, 3).add("tail");
    m.set(1, b, sizeof b);
    check("replacing a blob keeps the other data", intBlobString(m, b, 6));
    Cap c; m.send(c);
    OSCMessage in; in.fill(c.b, c.n);
    check("and the message still encodes and decodes", intBlobString(in, b, 6));
    m.set(3, a, 3);
    check("set(size(), blob) still appends",
          !m.hasError() && m.size() == 4 && m.getType(3) == 'b');
}

// ---------------------------------------------------------------------------
// The blob constructor took any length. For -1..-3 it allocated len + 4 bytes,
// wrote the 4-byte size into them (ASan: heap-buffer-overflow) and then copied
// a negative count, which crashes any build. From -4 down it allocated nothing
// yet reported OSC_OK: -4 sent a 'b' with no size and -5 crashed send().
// INT_MAX overflowed len + 4. Every integer type reaches it through
// add(blob, N). It is now refused like an allocation failure: INVALID_OSC,
// nothing stored, nothing sent, and set() keeps the blob it would replace.
// ---------------------------------------------------------------------------
static void blobNegativeLength() {
    printf("blob with a negative length\n");
    uint8_t bl[4] = {1, 2, 3, 4};
    { OSCMessage m("/n"); m.add((int32_t)7).add(bl, -1);
      Cap c; m.send(c);
      check("add(blob, -1) is INVALID_OSC, stores and sends nothing",
            m.getError() == INVALID_OSC && m.size() == 1 && c.n == 0);
      OSCMessage copy(m); Cap c2; copy.send(c2);
      check("and neither does a copy", copy.hasError() && c2.n == 0); }
    { OSCMessage m("/n"); m.add(bl, -4); Cap c; m.send(c);
      check("add(blob, -4) does not send a 'b' with no size",
            m.getError() == INVALID_OSC && m.size() == 0 && c.n == 0); }
    { OSCMessage m("/n"); m.add(bl, -5); Cap c; m.send(c);
      check("add(blob, -5) does not crash send()",
            m.getError() == INVALID_OSC && c.n == 0); }
    { OSCMessage m("/n"); m.add(bl, INT_MAX);
      check("add(blob, INT_MAX) is refused, not overflowed",
            m.getError() == INVALID_OSC && m.size() == 0); }
    { OSCMessage a("/n"), b("/n"), c("/n"), d("/n");
      a.add(bl, (int8_t)-1); b.add(bl, (long)-1); c.add(bl, (int64_t)-1);
      d.add(bl, (uint32_t)0xFFFFFFFFu);
      check("int8_t, long, int64_t and uint32_t lengths too",
            a.getError() == INVALID_OSC && b.getError() == INVALID_OSC &&
            c.getError() == INVALID_OSC && d.getError() == INVALID_OSC &&
            a.size() + b.size() + c.size() + d.size() == 0); }
    { OSCMessage m("/n", (int32_t)1, bl, -1, (int32_t)2);
      check("in a one-call list the others are still added",
            m.getError() == INVALID_OSC && m.size() == 2); }
    { OSCMessage m("/n"); m.add(bl, 4); m.set(0, bl, -1);
      uint8_t got[4] = {0};
      check("set(0, blob, -1) keeps the old blob",
            m.getError() == INVALID_OSC && m.size() == 1 &&
            m.getOSCData(0)->getBlob(got, sizeof got) == 4 &&
            memcmp(got, bl, 4) == 0); }
}

// ---------------------------------------------------------------------------
// The blob length was converted to int unchecked, so a wider one wrapped before
// the negative-length check could see it. (long long)4294967300, -4294967292
// and (unsigned long long)0x8000000000000004 all became 4-byte blobs, through
// add(), set() and a one-call list alike, and -(2^32) + 2000000 read 2000000
// bytes from the caller's buffer. A fractional length was truncated the same
// way. The value is now checked before it becomes an int.
// ---------------------------------------------------------------------------
static void blobWideLength() {
    printf("blob with a length wider than int\n");
    uint8_t bl[4] = {1, 2, 3, 4};
    { OSCMessage a("/w"), b("/w"), c("/w"), d("/w");
      a.add(bl, (long long)4294967300LL);
      b.add(bl, (long long)-4294967292LL);
      c.add(bl, (unsigned long long)0x8000000000000004ULL);
      d.add(bl, (long long)(-4294967296LL + 2000000));
      check("64-bit lengths that wrapped to a small int are refused",
            a.getError() == INVALID_OSC && b.getError() == INVALID_OSC &&
            c.getError() == INVALID_OSC && d.getError() == INVALID_OSC &&
            a.size() + b.size() + c.size() + d.size() == 0); }
    { OSCMessage m("/w"); m.add(bl, 4); m.set(0, bl, (long long)-4294967292LL);
      uint8_t got[4] = {0};
      check("and through set(), which keeps the old blob",
            m.getError() == INVALID_OSC && m.size() == 1 &&
            m.getOSCData(0)->getBlob(got, sizeof got) == 4 &&
            memcmp(got, bl, 4) == 0); }
    { OSCMessage m("/w", (int32_t)1, bl, (long long)-4294967292LL, (int32_t)2);
      check("and in a one-call list",
            m.getError() == INVALID_OSC && m.size() == 2); }
    { OSCMessage m("/w"); m.add(bl, 2.5);
      check("a fractional length is refused, not truncated",
            m.getError() == INVALID_OSC && m.size() == 0); }
    { OSCMessage m("/w");
      m.add(bl, (long long)4).add(bl, (unsigned long long)3);
      m.set(0, bl, (uint16_t)2);
      check("wider and narrower lengths that fit still work",
            !m.hasError() && m.size() == 2 &&
            m.getBlobLength(0) == 2 && m.getBlobLength(1) == 3); }
}

int main() {
    zeroByteTypesDecode();
    paddingAfterZeroByteType();
    blobLengths();
    emptyString();
    int64Values();
    bundleReuseAndTimetag();
    rgbaAndMidiWireOrder();
    defaultBundleTimetag();
    blobSetNegativePosition();
    blobSetKeepsTheRest();
    blobNegativeLength();
    blobWideLength();
    printf("\n%s\n", failures ? "FAILURES" : "all regression tests passed");
    return failures ? 1 : 0;
}

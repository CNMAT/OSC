// Two OSC messages framed as XiaoSamd21Oscuino's page frames them, sent back
// to back, read by that sketch's pollOSC() through _SLIPSerial: both must
// arrive, as two packets, whether every byte is already waiting (two frames in
// one USB transfer) or they come one at a time.
//
// The page used to end each frame with END alone. The decoder takes an END
// followed straight by data for the opening END of the next frame and reads
// on, so a message sent right behind another one was read into it: /wii/id
// behind /i2c/scan was never answered, and the page never drew the extension
// (2026-10-29). The library's own sender puts an END on both sides
// (beginPacket(), endPacket()), and the page now does the same. That old
// framing is run too and its packet count printed, as a note: it is the
// library's behaviour, not a requirement.
//
// pollOSC() below is a copy; test-xiaosamd21-contract.mjs holds it equal to
// the sketch's.
#include "OSCBundle.h"
#include "SLIPEncodedSerial.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
unsigned long millis(void){return 0;} unsigned long micros(void){return 0;}
osctime_t oscTime(){ osctime_t t={0,0}; return t; }

struct FakeStream : public Stream {
  std::vector<uint8_t> rx; size_t rpos = 0;   // bytes the host has sent so far
  int available() override { return (int)(rx.size() - rpos); }
  int read() override { return rpos < rx.size() ? rx[rpos++] : -1; }
  int peek() override { return rpos < rx.size() ? rx[rpos] : -1; }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *, size_t n) override { return n; }
  void flush() {}
};

static FakeStream port;
static _SLIPSerial<FakeStream> SLIPSerial(port);
#define SLIP_PORT port
static OSCBundle bundleIN;

// ---- examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino
static bool pollOSC() {
  for (;;) {
    if (SLIPSerial.endofPacket()) return true;
    int avail = SLIPSerial.available();
    if (avail <= 0) {
      if (SLIP_PORT.available() > 0) continue;   // an END was swallowed; more behind it
      return SLIPSerial.endofPacket();           // the closing END, if that was the last byte
    }
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0) bundleIN.fill((uint8_t) c);   // read() returns -1 on a SLIP error
    }
  }
}
// ----

static int fails = 0, passes = 0;
static void chk(const char *n, bool ok, const std::string &got) {
  ok ? passes++ : fails++;
  printf("  %-66s %s%s%s\n", n, ok ? "ok" : "FAIL", ok ? "" : "  got: ", ok ? "" : got.c_str());
}

// An OSC message with no arguments, as the page's encodeMessage(addr, [])
// makes it: the address and ",", each padded to four bytes.
static std::vector<uint8_t> ask(const char *addr) {
  std::vector<uint8_t> m(addr, addr + strlen(addr));
  do m.push_back(0); while (m.size() % 4);
  const char tag[4] = { ',', 0, 0, 0 };
  m.insert(m.end(), tag, tag + 4);
  return m;
}
static std::vector<uint8_t> frame(const std::vector<uint8_t> &m, bool leadingEnd) {
  std::vector<uint8_t> f;
  if (leadingEnd) f.push_back(0xC0);
  for (uint8_t b : m) {
    if (b == 0xC0) { f.push_back(0xDB); f.push_back(0xDC); }
    else if (b == 0xDB) { f.push_back(0xDB); f.push_back(0xDD); }
    else f.push_back(b);
  }
  f.push_back(0xC0);
  return f;
}

// Every packet pollOSC() completes that holds a message, as "address|address"
// per packet, with " ERROR" on one the bundle could not decode; empty ones
// are counted in *empties. An empty packet is a frame's opening END read on
// its own, with the next byte not yet there: the sketch dispatches nothing
// for it. allAtOnce: every byte is there before the first poll; otherwise one
// more byte arrives before each poll.
static std::string receive(const std::vector<uint8_t> &bytes, bool allAtOnce, int *empties) {
  port.rx.clear(); port.rpos = 0;
  bundleIN.empty();
  std::string out;
  *empties = 0;
  size_t given = allAtOnce ? bytes.size() : 0;
  port.rx.assign(bytes.begin(), bytes.begin() + given);
  for (int pass = 0; pass < 4 * (int) bytes.size() + 8; pass++) {
    if (!allAtOnce && given < bytes.size()) port.rx.push_back(bytes[given++]);
    if (!pollOSC()) continue;
    if (bundleIN.size() == 0 && !bundleIN.hasError()) { (*empties)++; continue; }
    if (!out.empty()) out += " ; ";
    for (int i = 0; i < bundleIN.size(); i++) {
      char a[64] = "";
      bundleIN.getOSCMessage(i)->getAddress(a);
      out += (i ? "|" : "") + std::string(a);
    }
    if (bundleIN.hasError()) out += " ERROR";
    bundleIN.empty();
  }
  return out;
}

int main() {
  const std::vector<uint8_t> scan = ask("/i2c/scan"), id = ask("/wii/id");
  std::vector<uint8_t> both = frame(scan, true), tail = frame(id, true);
  both.insert(both.end(), tail.begin(), tail.end());

  int empties;
  std::string got = receive(both, true, &empties);
  chk("END-framed /i2c/scan then /wii/id, all waiting: two packets", got == "/i2c/scan ; /wii/id", got);
  printf("  (empty packets between: %d)\n", empties);
  got = receive(both, false, &empties);
  chk("... the same arriving a byte at a time: two packets", got == "/i2c/scan ; /wii/id", got);
  printf("  (empty packets between: %d)\n", empties);

  // The control: the two messages in one frame are one packet, so a merge
  // shows up in the count, and the checks above can fail.
  std::vector<uint8_t> merged = scan;
  merged.insert(merged.end(), id.begin(), id.end());
  got = receive(frame(merged, true), true, &empties);
  chk("control: both messages inside one frame read as one packet", got == "/i2c/scan", got);

  std::vector<uint8_t> old = frame(scan, false), oldTail = frame(id, false);
  old.insert(old.end(), oldTail.begin(), oldTail.end());
  got = receive(old, true, &empties);
  printf("  note: the closing END alone, all waiting, as the page sent it before: %s\n", got.c_str());

  printf("slipframes: %d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}

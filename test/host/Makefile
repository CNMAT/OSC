# Host-compiled test suite for the OSC library.
#
# Needs only a C++11 compiler and python3. No Arduino toolchain, no board, no
# extra libraries -- shim/ provides the ~55 lines of Arduino.h, Print.h,
# Stream.h, Client.h and HardwareSerial.h that the library actually uses.
#
#   make           build and run everything
#   make asan      the same, under AddressSanitizer + UBSan (slower, worth it)
#   make clean
#
# The suites:
#   vectors      encodes 26 packets and checks them against oracle.py, an OSC
#                1.0 decoder written from the spec that shares no code with the
#                library. This is the only check that is independent of the
#                implementation; everything else verifies self-consistency.
#   roundtrip    20000 pseudo-random messages (fixed seed, so failures are
#                reproducible) encoded and decoded back. This found three of the
#                bugs regressions.cpp now pins.
#   regressions  named test per shipped bug.
#   types        which type tags encode and decode, plus address patterns.
#                Documentation-as-test: it fails if support silently changes.
#   harden       malformed patterns, negative indices, bad bundle element sizes.
#                Only meaningful under `make asan`.
#   copygrowth   copy/assignment semantics and the geometric data-array growth.
#   patterns     address-pattern semantics, so hardening OSCMatch.c can be shown
#                not to have changed what matches.
#   dos          attacker-controlled length fields must not grow the buffer
#                without bound.
#   sliptcp      SLIP over TCP against a stub Client: framing, escaping,
#                underrun-and-resume. Caught SLIPEncodedTCP's drift from
#                _SLIPSerial without any Ethernet hardware.
#   buffered     OSCBufferedPrint against a recording Print: the buffered bytes
#                must match the direct send exactly, in one write, and survive
#                an undersized buffer and a sink that takes less than offered.
#   fuzzbad      150000 rounds of hostile input into both decoders, then every
#                accessor on whatever came out. The point of the whole suite.
#   onecall      msg.add(a, b, ...), OSCMessage("/x", a, b, ...) and
#                bundle.add("/x", a, b, ...) must send exactly the bytes of the
#                chained single add() calls, blobs included at every length type.
#   mustfail     calls that must not compile -- a blob without its length, a
#                const blob, any other pointer -- each refused with the guard's
#                own message, not merely refused.
#   std11        the library and its templates are standard C++11: the Due's
#                GCC 4.8.3 is the floor, and avr-gcc 7.3 in gnu++11 mode takes a
#                C++17 fold expression with only a warning that the Arduino
#                IDE's default -w hides, so an AVR build proves nothing.
#
# Every result below is checked through its exit status. Until 2026-09-24 the
# piped lines lost it to `tail`: vectors reporting "1 failures" still left make,
# and so CI, green. Hence bash and pipefail on every piped line.

SHELL    := /bin/bash
LIB      := ../..
CXX      ?= c++
CXXFLAGS := -std=c++11 -I shim -I $(LIB) -Wall -Wno-unused-parameter
SRC      := $(LIB)/OSCData.cpp $(LIB)/OSCMessage.cpp $(LIB)/OSCBundle.cpp $(LIB)/OSCMatch.c
HDRS     := $(wildcard $(LIB)/*.h) $(wildcard shim/*.h)
TESTS    := vectors roundtrip regressions types harden copygrowth fuzzbad patterns dos sliptcp buffered onecall
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
MUSTFAIL := 1 2 3 4 5 6 7 8

.PHONY: all asan clean run mustfail std11 FORCE
all: run

asan: CXXFLAGS += $(SAN)
asan: run

# The flags are a dependency too. `asan:` only appends to CXXFLAGS, so after a
# plain `make` the binaries looked up to date and `make asan` ran them
# unsanitized and reported green. .cxxflags is rewritten only when the flags
# change, so switching between `make` and `make asan` rebuilds every suite.
.cxxflags: FORCE
	@echo '$(CXX) $(CXXFLAGS)' | cmp -s - $@ || echo '$(CXX) $(CXXFLAGS)' > $@

# the headers are dependencies too: most of the one-call API is templates, and
# without them a header-only change reran the stale binaries
$(TESTS): %: %.cpp $(SRC) $(HDRS) .cxxflags
	$(CXX) $(CXXFLAGS) -w $< $(SRC) -o $@

run: $(TESTS) mustfail std11
	@echo "== vectors (vs independent decoder)"; set -o pipefail; ./vectors | python3 oracle.py | tail -2
	@echo "== roundtrip";   set -o pipefail; ./roundtrip   | tail -2
	@echo "== regressions"; set -o pipefail; ./regressions | tail -2
	@echo "== types";       ./types       > /dev/null && echo "  type/pattern matrix ok"
	@echo "== harden";      set -o pipefail; ./harden      | tail -2
	@echo "== copygrowth"; set -o pipefail; ./copygrowth | tail -2
	@echo "== patterns";   set -o pipefail; ./patterns   | tail -1
	@echo "== dos";        set -o pipefail; ./dos        | tail -1
	@echo "== sliptcp";    set -o pipefail; ./sliptcp    | tail -1
	@echo "== buffered";   set -o pipefail; ./buffered   | tail -1
	@echo "== onecall";    set -o pipefail; ./onecall    | tail -1
	@echo "== fuzzbad";    set -o pipefail; ASAN_OPTIONS=allocator_may_return_null=1 ./fuzzbad | tail -1

# no case selected must compile; each case must fail, and with the guard's message
mustfail: mustfail.cpp $(HDRS)
	@echo "== mustfail"
	@$(CXX) $(CXXFLAGS) -fsyntax-only mustfail.cpp || { echo "  mustfail.cpp does not compile with no case selected"; exit 1; }
	@for n in $(MUSTFAIL); do \
	  if $(CXX) $(CXXFLAGS) -fsyntax-only -DCASE=$$n mustfail.cpp > mustfail.log 2>&1; then \
	    echo "  case $$n compiled"; exit 1; \
	  elif ! grep -q "OSC pointer argument" mustfail.log; then \
	    echo "  case $$n failed, but not with the guard's message:"; head -5 mustfail.log; exit 1; \
	  fi; \
	done; rm -f mustfail.log; echo "  $(words $(MUSTFAIL)) calls refused, each with the guard's message"

# -pedantic-errors turns extensions into errors, but -w silences even those
# (measured with clang 14 and GCC 15.2.1), so this compile must not pass -w
std11: $(SRC) onecall.cpp $(HDRS)
	@echo "== std11"
	@$(CXX) -std=c++11 -pedantic-errors -fsyntax-only -I shim -I $(LIB) -x c++ $(SRC) onecall.cpp 2>&1 \
	  | grep -E "error" ; test $${PIPESTATUS[0]} -eq 0 && echo "  library and one-call templates are standard C++11"

clean:
	rm -rf $(TESTS) *.dSYM mustfail.log .cxxflags

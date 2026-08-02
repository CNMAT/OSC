# Host-compiled test suite for the OSC library.
#
# Needs only a C++11 compiler and python3. No Arduino toolchain, no board, no
# extra libraries -- shim/ provides the ~40 lines of Arduino.h/Print.h/Stream.h
# that the library actually uses.
#
#   make           build and run everything
#   make asan      the same, under AddressSanitizer + UBSan (slower, worth it)
#   make clean
#
# The suites:
#   vectors      encodes 23 packets and checks them against oracle.py, an OSC
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

LIB      := ../..
CXX      ?= c++
CXXFLAGS := -std=c++11 -I shim -I $(LIB) -Wall -Wno-unused-parameter
SRC      := $(LIB)/OSCData.cpp $(LIB)/OSCMessage.cpp $(LIB)/OSCBundle.cpp $(LIB)/OSCMatch.c
TESTS    := vectors roundtrip regressions types harden
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all asan clean
all: run

asan: CXXFLAGS += $(SAN)
asan: run

$(TESTS): %: %.cpp $(SRC) shim/Arduino.h shim/Print.h shim/Stream.h
	$(CXX) $(CXXFLAGS) -w $< $(SRC) -o $@

run: $(TESTS)
	@echo "== vectors (vs independent decoder)"; ./vectors | python3 oracle.py | tail -2
	@echo "== roundtrip";   ./roundtrip   | tail -2
	@echo "== regressions"; ./regressions | tail -2
	@echo "== types";       ./types       > /dev/null && echo "  type/pattern matrix ok"
	@echo "== harden";      ./harden      | tail -2

clean:
	rm -rf $(TESTS) *.dSYM

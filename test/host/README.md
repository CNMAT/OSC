# Host wire-format test

Checks that the bytes this library puts on the wire are what OSC 1.0 says they
should be. Runs on a development machine — no Arduino, no board, no upload.

```
cd test/host
make run
```

Needs a C/C++ compiler and `python3`. Exits non-zero if any case fails.

## How it works

`shim/` is a ~40-line stand-in for `Arduino.h` and `Print.h` — enough for the
library to compile with a host compiler, with `Print` collecting everything
written into a buffer instead of sending it to a serial port.

`emit.cpp` builds messages and bundles with the library, dumps the encoded
bytes as hex, and also feeds hand-written spec-correct bytes back through
`fill()` to exercise the decoder.

`check.py` is an OSC 1.0 decoder written from the spec in Python stdlib. It
deliberately shares no code with the library under test — that independence is
the point. It decodes each emitted case and asserts the values, so a decoder
bug in the library cannot hide an encoder bug in the library.

## Why this exists

Up to 3.5.8 the `'r'` (RGBA) and `'m'` (MIDI) types were encoded byte-reversed,
and bundle timetags were byte-swapped on receive. In both cases the encode and
decode paths were wrong in mirror-image ways, so the library round-tripped
perfectly with itself and the faults were invisible to any test that used this
library on both ends. Only decoding with something else exposed them.

When adding a type, add a case here rather than a round-trip assertion.

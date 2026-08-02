# Host test suite

Runs the library on a development machine, with no Arduino toolchain and no
board attached. `shim/` supplies the ~40 lines of `Arduino.h`, `Print.h` and
`Stream.h` that `OSCData.cpp`, `OSCMessage.cpp`, `OSCBundle.cpp` and
`OSCMatch.c` actually use.

```sh
make        # build and run
make asan   # the same under AddressSanitizer + UBSan -- use this before a commit
make clean
```

Needs a C++11 compiler and `python3`. Nothing else.

## Why this exists alongside `test/`

The sketches in `test/` are on-device ArduinoUnit tests. They need hardware, they
need a library that is not in the Arduino index, and they cannot be run by CI. They
also cannot check the one thing that matters most about an OSC implementation --
whether the bytes on the wire are right -- because the only decoder available to
them is the one under test.

This suite can, because `oracle.py` is an OSC 1.0 decoder written from the
specification that shares no code with the library. When `vectors` and `oracle.py`
agree, that is evidence. When the library agrees with itself, that is not.

## The suites

| suite | what it does |
|---|---|
| `vectors` | Encodes 23 packets covering every supported type tag, both integer widths and their limits, unsigned wrap, strings and blobs at every length modulo 4, empty argument lists, addresses at every padding residue, and a bundle. Pipes them to `oracle.py`, which independently rejects bad padding, non-multiple-of-4 packets, unterminated strings, negative blob lengths and trailing bytes. |
| `roundtrip` | 20000 pseudo-random messages encoded then decoded and compared field by field. Fixed seed, so a failure is reproducible and bisectable. This found three of the bugs `regressions` now pins. |
| `regressions` | One named test per bug that shipped in a release. Each failed before its fix. |
| `types` | Prints and asserts which type tags can be encoded and which can be decoded — they are not the same set — plus the address-pattern behaviour. Documentation-as-test: it fails loudly if support changes by accident. |
| `harden` | Malformed input: unbalanced `[` and `{` in patterns, stray closers, negative argument indices, negative bundle element sizes. Only meaningful under `make asan`, where an out-of-bounds read aborts instead of passing silently. |

## Adding a test

New bug, new named case in `regressions.cpp`, with a comment saying what the
wrong behaviour was. If the bug is a shape the fuzzer could have found but did
not, widen `roundtrip.cpp` as well so the class is covered, not just the instance.

## Known gaps

- Bundles are only lightly covered by `roundtrip`; nesting and timetag arithmetic
  deserve their own generator.
- Nothing feeds deliberately corrupt frames into `fill()` in bulk. The decoder is
  the only code that touches untrusted bytes, and every bug found so far lived
  there, so this is the most valuable thing to add next.
- `roundtrip` does not yet generate `'r'`, `'m'` or `'t'` arguments; those are
  covered only by the fixed cases in `regressions`.

## Provenance

This suite is the consolidation of two independently written host harnesses: the
`vectors`/`oracle.py` pair with the fuzz, regression and hardening suites, and a
wire-format harness (`emit.cpp`/`check.py`) written alongside the 4.0.0 `'r'`,
`'m'` and bundle-timetag fixes. The latter's cases live in
`rgbaAndMidiWireOrder()` and `defaultBundleTimetag()` in `regressions.cpp`. Both
used the same shape -- encode on the host, dump hex, check with an independent
Python decoder -- so only one of them needs to exist.

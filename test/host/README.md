# Host test suite

Runs the library on a development machine, with no Arduino toolchain and no
board attached. `shim/` supplies the ~50 lines of `Arduino.h`, `Print.h`,
`Stream.h`, `Client.h` and `HardwareSerial.h` that `OSCData.cpp`,
`OSCMessage.cpp`, `OSCBundle.cpp`, `OSCMatch.c` and the SLIP transport
actually use.

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
| `sliptcp` | SLIP over TCP (`SLIPEncodedTCP`, now `_SLIPSerial<Client>`) against a stub `Client`: exact escaping, round-trips, back-to-back packets, and a stream that goes empty mid-packet then resumes. Run against the old hand-copied class it fails on every count that class had drifted from the template — the `read()` -1 narrowing, the `write(buffer, size)` return value, and byte-at-a-time transmit — which is how those were confirmed without any Ethernet hardware. |

## Decoding untrusted bytes

The decoder is the only code that touches bytes from the wire, so it gets its
own treatment. `fuzzbad` throws 150000 rounds of hostile input at both decoders
— pure random bytes, random bytes with a `#bundle` header, and valid packets
mutated by bit flips, byte splats and truncation — then calls every accessor on
whatever came out, including out-of-range and negative indices. Only meaningful
under `make asan`.

It found four classes of problem, all remotely reachable:

**A NULL address dereference.** A message whose address never decoded has
`address == NULL`, and `bytes()`, both `getAddress()` overloads,
`getAddressLength()`, `match()`, `fullMatch()` and `send()` all used it
unguarded. `strlen(NULL)`.

**Unbounded scans in `OSCMatch.c`.** `osc_match_curly_brace()` advanced past the
terminator when an alternative list was unterminated. `osc_match_bracket()` read
`*(pattern+2)` for a range and stepped `pattern += 3` over the end.
`osc_match_star()` walked *backwards* with no lower bound in three places, and
`osc_match_star_r()` scanned forward for a closer that need not exist. Every one
is now fenced by the string's own start and terminator.

This matters more than it first appears. `osc_match()` takes
`(pattern, address)`, but `OSCMessage::match()` and `fullMatch()` call it as
`osc_match(address + offset, pattern, ...)` — **the address received off the
wire is what gets passed as the pattern**. So a malformed inbound address, not
merely a malformed local pattern, reaches all of this.

`patterns` exists to show the fences changed nothing: it runs the same
semantics cases against the hardened `OSCMatch.c` and against the version
before it, and both agree.

**Unbounded growth on attacker-controlled lengths.** Blob lengths and bundle
element sizes are read straight off the wire and the decoders grew their buffer
until the declared length arrived, so a peer claiming a 4 GB blob grew it until
malloc failed — a one-packet denial of service on a 2.5 KB part. `OSC_MAX_INCOMING`
now caps it (512 on AVR, 4096 elsewhere, overridable) and raises `BUFFER_FULL`,
an error code that had been declared but never used anywhere. `dos` covers it.

**Byte-at-a-time reallocation.** `OSCBundle::addToIncomingBuffer()` called
`realloc` for every single received byte. It now grows in blocks.

## Allocation while decoding

`clearIncomingBuffer()` runs after every completed argument, and it used to
`realloc` the buffer back down to 16 bytes each time — even when it was already
16 bytes. That, not the growth path, was where the churn was: about one realloc
per argument. It now keeps the capacity and only resets the counters, and
`empty()` calls `shrinkIncomingBuffer()` to hand back anything an unusually
large packet acquired, so the steady state of a long-lived message stays
allocation-free without holding memory indefinitely. Growth also doubles rather
than adding 16, which is what the large-blob case needed.

| decoding | before | after |
|---|---|---|
| 1 int | 6 reallocs | 1 |
| 16 ints (the Esplora message) | 24 | 4 |
| 1 blob, 200 B | 18 | 5 |

`alloc/` holds the counter used to measure that; see its README.

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

---
name: board-bringup
description: Bring up the OSC library on a newly connected Arduino board — identify the actual hardware, compile, flash, verify the SLIP/OSC transport with the test/hardware suites, and optionally build the XxxOscuino demo sketch plus Web Serial page for boards with built-in peripherals. Use when the user plugs in a board, asks to port or validate OSC on new hardware, or asks for a board demo page.
---

Follow [BRINGUP.md](../../../BRINGUP.md) — it is the authority; this file is
the operating summary. Record results per
[BOARDS.md](../../../BOARDS.md) and the Method rules in
[test/hardware/README.md](../../../test/hardware/README.md).

## Non-negotiables

1. **Identify before flashing — the chip AND the board it sits on.**
   `arduino-cli board list` + `system_profiler SPUSBDataType` (VID/PID) +
   `esptool chip_id` for ESP32s. Boards displace each other on hubs, ports
   renumber after every flash, UF2 bootloader IDs name the bootloader not
   the model, and what the user believes is plugged in has been wrong
   before. Never identify by port name.

   `chip_id` names the silicon only. Every native-USB ESP32 enumerates as
   `303a:1001`, so VID/PID cannot tell a XIAO from a SuperMini — and the
   pin map, LED polarity, populated buses and FQBN defaults all belong to
   the *carrier*. Inferring the board from the chip put the EGG demo on a
   XIAO ESP32-C3 on 2026-09-04, driving the wrong pins and reporting a
   missing peripheral that was really a wrong address. Ask the user what
   the board is and believe them over your own inference; flash the
   board-agnostic `test/hardware/OscEcho` before any pin-driving demo; and
   treat a peripheral that "fails to probe" as a suspected wrong pin map
   first. See BRINGUP.md Phase 0, *The chip is not the board*.
2. **Verify, don't assert.** No number leaves the session without: trickle
   gate passed, 3 repeats, same-day reference board, mechanism named.
   Unverified code carries a STATUS comment saying so. If a measurement
   surprises you, suspect the instrument first — five "board bugs" in this
   repo's history were the apparatus.
3. **Report failures faithfully**, including your own instrument errors.

## Transport flow (every board)

Phase 0 identify → Phase 1 compile (`test/hardware/OscEcho`; new cores may
need a rung in `SLIPEncodedSerial.h`'s detection ladder; ESP32 FQBN options
are per-board — read `boards.txt`, never carry options between boards) →
Phase 2 flash (per-family procedures in BOARDS.md; after every flash,
re-list ports; `lsof` when "no device") → Phase 3 verify, in order:
`echotest.py`, `widths.py`, `oscprobe.py`, then `bench.py`
verify / in 50 -1 / out 200 / compound / ring 20. Place the stack in the
family table (NAK-clean, drop-with-byte-ceiling, or pool-starved-compound)
by fingerprint, then add the BOARDS.md row.

## Demo flow (boards with built-in peripherals)

Phase 4 sketch: `/enq` answered by a bundle — the sketch name, then one
`/enq/<capability>` line per peripheral actually present, carrying its
shape (absence is silence, never a boolean or a sentinel; see
ADDRESSES.md); probe capabilities at runtime by *signal* (not `begin()`'s
return — buses exist without parts on them); respect ISR-shaped driver
contracts; uint64 square-accumulators, float division, window-relative
scope normalisation, full-scale wire values, measured gain; pace with
`millis()` + `/rate` (0 stops; clamping it to a minimum is a bug — it makes
"be quiet" stream faster). Phase 5 page: serve on localhost with no-store;
decode every OSC tag including blobs; draw the board to scale and mirror
outbound state; dBFS meters with peak-hold and latched clip; hide absent
peripherals via `/enq`. Hand-written pages get a contract test
(`extras/webserial/test/test-cpx-contract.mjs` is the pattern).

## Record

BOARDS.md row + any new flashing procedure; test/hardware/README.md if the
stack family taught something new; commit messages state what was measured
and what was not.

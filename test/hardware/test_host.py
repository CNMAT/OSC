#!/usr/bin/env python3
"""Host-side self-test. No hardware, no pty, finishes in seconds.

The previous attempt (selftest.py) modelled a board with a pty and never ran
to completion: a pty is a terminal, with a line discipline, its own buffer
sizes and its own blocking rules, none of which resemble a USB CDC device.
These tests claim less and finish. They test only the host code, on plain
pipes, whose semantics are exactly the ones the code must survive:

  * SLIP codec round-trips, including escape-dense JPEG-like payloads
  * OSC encode -> decode round-trips for the message builder
  * bench.py's seq/CRC frame matches its own decode
  * write_all() delivering every byte through an fd that returns short
    writes -- the exact bug that produced the withdrawn burst figures --
    plus a NEGATIVE control: the old broken write pattern run under the
    same pressure MUST lose data, proving the test can see the bug class

    python3 test_host.py
"""
import fcntl
import hashlib
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import (write_all, slip_encode, slip_frames, msg, decode,
                      Impulse, Null)
from bench import seq_frame

FAILED = []


def check(name, ok, detail=""):
    print(f"  {'ok  ' if ok else 'FAIL'}  {name}{'  ' + detail if detail else ''}")
    if not ok:
        FAILED.append(name)


# ---------------------------------------------------------------- 1. codec
print("SLIP codec:")
for name, payload in [
    ("plain bytes",         bytes(range(64))),
    ("contains END 0xC0",   b"\x01\xc0\x02"),
    ("contains ESC 0xDB",   b"\x01\xdb\x02"),
    ("both, adjacent",      b"\xc0\xdb\xdb\xc0"),
    ("all 256 byte values", bytes(range(256))),
]:
    got = slip_frames(slip_encode(payload))
    check(f"round-trips: {name}", got == [payload])

jpegish = bytes([0xFF, 0xD8] + [0xFF, 0xC0] * 40 + [0xFF, 0xDB] * 40 + [0xFF, 0xD9])
check("round-trips: JPEG-like (escape-dense)",
      slip_frames(slip_encode(jpegish)) == [jpegish])
check("two frames in one stream split correctly",
      slip_frames(slip_encode(b"ab") + slip_encode(b"cd")) == [b"ab", b"cd"])

# ------------------------------------------------------------------ 2. OSC
print("\nOSC encode -> decode:")
addr, args = decode(msg('/x', [1, -2, 3.5, "hi", Impulse, Null]))
check("mixed args survive", addr == '/x' and args == [1, -2, 3.5, "hi", 'I', 'N'],
      repr((addr, args)))

a, ar = decode(slip_frames(seq_frame(12345))[0])
check("bench seq frame decodes", a == '/b/s' and ar[0] == 12345, repr((a, ar)))
a, ar = decode(slip_frames(seq_frame(0xFFFFFFF0))[0])
check("bench seq frame near uint32 max", a == '/b/s'
      and (ar[0] & 0xFFFFFFFF) == 0xFFFFFFF0, repr(ar))

# ------------------------------------------------------- 3. the write path
print("\nwrite path, against an fd that guarantees short writes:")


def slow_reader(fd, chunks, chunk=512, nap=0.002):
    while True:
        try:
            b = os.read(fd, chunk)
        except OSError:
            return
        if not b:
            return
        chunks.append(b)
        time.sleep(nap)


def pressure_pipe():
    r, w = os.pipe()
    fcntl.fcntl(w, fcntl.F_SETFL, os.O_NONBLOCK)
    chunks = []
    t = threading.Thread(target=slow_reader, args=(r, chunks), daemon=True)
    t.start()
    return r, w, chunks, t


payload = bytes(i % 251 for i in range(256 * 1024))

r, w, chunks, t = pressure_pipe()
sent = write_all(w, payload, timeout=10.0)
os.close(w); t.join(5.0); os.close(r)
got = b''.join(chunks)
check(f"write_all delivers all {len(payload)} bytes",
      sent == len(payload) and hashlib.sha256(got).digest()
      == hashlib.sha256(payload).digest(),
      f"delivered {len(got)}")

# NEGATIVE control: the pre-fix pattern -- one os.write, return discarded --
# must LOSE data under the same pressure, or this file cannot claim the
# positive test above means anything.
r, w, chunks, t = pressure_pipe()
try:
    os.write(w, payload)          # short count silently discarded, as before
except BlockingIOError:
    pass
time.sleep(0.2)
os.close(w); t.join(5.0); os.close(r)
lost = len(payload) - len(b''.join(chunks))
check("the OLD broken write pattern loses data here (test can see the bug)",
      lost > 0, f"lost {lost} of {len(payload)}")

print()
if FAILED:
    print(f"FAILED ({len(FAILED)}): " + "; ".join(FAILED))
    sys.exit(1)
print("all host self-tests passed: the codec round-trips and the write path")
print("delivers every byte even when the kernel takes them a few at a time.")

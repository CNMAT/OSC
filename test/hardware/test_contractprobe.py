#!/usr/bin/env python3
"""Self-test for contractprobe.py: a simulated board on a pty, no hardware.

contractprobe drives itself from what a board announces, so its logic can be
exercised without a board -- by a fake that speaks the contract over a
pseudo-terminal. Two fakes run here:

  * a CONFORMANT board, which must pass every check; and
  * a BROKEN board, which announces `/enq/imu 6` and then answers with three
    ints. A probe that passes this one is not checking anything, so the
    negative case is the real test of the test.

Run: python3 test/hardware/test_contractprobe.py     (no arguments, seconds)
"""

import os
import pty
import struct
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import importlib.util
_spec = importlib.util.spec_from_file_location('oscprobe', os.path.join(HERE, 'oscprobe.py'))
op = importlib.util.module_from_spec(_spec)
_argv, sys.argv = sys.argv, ['oscprobe', '/dev/null']
try:
    _spec.loader.exec_module(op)
except SystemExit:
    pass
finally:
    sys.argv = _argv


def f32(x):
    """A float argument, so the fake sends 'f' where the contract says float."""
    return float(x)


class FakeBoard(threading.Thread):
    """Answers the contract over a pty master. `broken` flips one promise."""

    daemon = True

    def __init__(self, fd, broken=False):
        super().__init__()
        self.fd = fd
        self.broken = broken
        self.seq = 0
        self.rate = 0
        self.stop_flag = False
        self.buf = b''

    # --- outbound -------------------------------------------------------
    def send(self, msgs):
        os.write(self.fd, op.slip_encode(op.bundle(
            [op.msg(a, tuple(v)) for a, v in msgs])))

    def enq(self):
        return [('/enq', ('FakeBoard',)),
                ('/enq/btn', (2,)),
                ('/enq/imu', (6,)),
                ('/enq/temp', ()),
                ('/enq/rgb', (1,)),
                ('/enq/buzz', ()),
                ('/enq/touch', (240, 240))]      # passive: must be skipped

    def imu(self):
        if self.broken:
            return ('/imu', (1, 2, 3))           # ints, and only three
        return ('/imu', tuple(f32(v) for v in (0.01, -0.02, 0.99, 0.0, 0.1, -0.1)))

    # --- inbound --------------------------------------------------------
    def handle(self, addr, args):
        if addr == '/enq':
            return self.enq()
        if addr == '/state':
            return [('/state', (self.seq, int(time.time() * 1000) & 0x7fffffff))]
        if addr == '/s/l':
            return [('/s/l', tuple(args))]
        if addr == '/s/m':
            return [('/s/m', (int(time.time() * 1e6) & 0x7fffffff,))]
        if addr == '/rate':
            self.rate = args[0] if args else 0
            return [('/rate', (self.rate,))]
        if addr == '/btn':
            return [('/btn', (0, 1))]
        if addr == '/imu':
            return [self.imu()]
        if addr == '/temp':
            return [('/temp', (f32(21.5),))]
        if addr == '/rgb':
            return [('/rgb', tuple(args))]
        if addr == '/buzz':
            return [('/buzz', tuple(args))]
        return []                                 # retired addresses: silence

    def run(self):
        # Non-blocking, or the read below parks the thread and the /rate
        # stream below it never ticks -- which looks exactly like a board
        # that ignores /rate.
        os.set_blocking(self.fd, False)
        next_tick = None
        while not self.stop_flag:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                chunk = b''
            except OSError:
                return
            if chunk:
                self.buf += chunk
                *frames, self.buf = self._split(self.buf)
                out = []
                for fr in frames:
                    d = op.decode(fr)
                    for addr, args in (d[1] if d[0] == 'bundle' else [d]):
                        out += self.handle(addr, args)
                if out:
                    self.send(out)
                if self.rate:
                    next_tick = time.time()
            if self.rate:
                now = time.time()
                if next_tick is None or now >= next_tick:
                    self.seq += 1
                    self.send([('/state', (self.seq, int(now * 1000) & 0x7fffffff)),
                               self.imu()])
                    next_tick = now + self.rate / 1000.0
            time.sleep(0.002)

    @staticmethod
    def _split(buf):
        """Return complete SLIP frames plus the trailing partial."""
        END = b'\xc0'
        parts = buf.split(END)
        tail = parts.pop()
        frames = []
        for p in parts:
            if not p:
                continue
            frames.append(op.slip_frames(END + p + END)[0])
        return frames + [tail]


def run_case(label, broken, expect_fail):
    master, slave = pty.openpty()
    board = FakeBoard(master, broken=broken)
    board.start()
    r = subprocess.run(
        [sys.executable, os.path.join(HERE, 'contractprobe.py'),
         os.ttyname(slave), 'FakeBoard', '/fake', '/fake/led'],
        capture_output=True, text=True, timeout=120)
    board.stop_flag = True
    os.close(slave)
    try:
        os.close(master)
    except OSError:
        pass
    failed = r.returncode != 0
    ok = failed == expect_fail
    print(f"\n=== {label}: exit {r.returncode} "
          f"({'as expected' if ok else 'NOT as expected'})")
    for line in r.stdout.splitlines():
        if line.strip().startswith('FAIL') or 'passed,' in line or '--   ' in line:
            print('   ' + line.strip())
    if not ok:
        print(r.stdout)
        print(r.stderr)
    return ok


if __name__ == '__main__':
    good = run_case('conformant board must pass', broken=False, expect_fail=False)
    bad = run_case('broken board must be caught', broken=True, expect_fail=True)
    print(f"\nself-test: {'PASS' if good and bad else 'FAIL'}")
    sys.exit(0 if good and bad else 1)

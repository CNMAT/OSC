"""micro:bit MicroPython dev driver over raw REPL, standard library only.

Deploys and interrogates the extras/python/MicrobitOscuino firmware over the
DAPLink serial port, working around the V1 traps recorded in BOARDS.md: the
pending second ctrl-C that fires into the next code executed, the /s/q escape
needed once main.py is serving with kbd_intr off, and a CDC that sometimes
enumerates mute until the port is closed and reopened.

Reuses oscprobe.Port for the tty handling. Commands:
  env                  report sys.version, mem_free, struct/kbd_intr presence
  put <local> [name]   write a file into the board filesystem via raw REPL
  imptest              import main on-device, compare codec bytes to host
  reboot               ctrl-B + ctrl-D: soft reset (runs main.py)
  repl                 try to regain a REPL (ctrl-C) and report what answered
"""
import os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oscprobe

PORT = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2].startswith('/dev/') \
    else '/dev/cu.usbmodem14102'


class Repl:
    def __init__(self, path=PORT):
        self.path = path
        self.p = oscprobe.Port(path)

    def reopen(self):
        try:
            self.p.close()
        except OSError:
            pass
        time.sleep(1.5)
        self.p = oscprobe.Port(self.path)

    def _read_until(self, marker, timeout=4.0):
        got = b''
        end = time.time() + timeout
        while time.time() < end:
            got += self.p.drain(0.05)
            if marker in got:
                return got
        raise TimeoutError('never saw %r, got %r' % (marker, got[-200:]))

    def enter_raw(self):
        # The DAPLink CDC sometimes comes up dead on a fresh open (measured:
        # total silence, no echo); a reopen clears it. A REPL answers a plain
        # interrupt; a deployed main.py (SLIP loop, kbd_intr off) needs the
        # /s/q escape instead — try the cheap thing first.
        for attempt in range(4):
            self.p.write(b'\r\x03\x03\r')
            got = self.p.drain(1.5)
            if b'>>>' not in got:
                self.p.write(oscprobe.slip_encode(
                    oscprobe.bundle([oscprobe.msg('/s/q')])))
                self.p.drain(0.6)
                self.p.write(b'\r\x03\x03\r')
                got = self.p.drain(1.2)
            if b'>>>' in got:
                self.p.write(b'\r\x01')       # ctrl-A: raw REPL
                try:
                    self._read_until(b'raw REPL; CTRL-B to exit')
                    break
                except TimeoutError:
                    pass
            print('  enter_raw attempt %d heard %r' % (attempt, got[-80:]))
            if attempt == 3:
                raise TimeoutError('no REPL after 4 attempts')
            self.reopen()
        # The V1 fork leaves a second ctrl-C pending and fires it into the
        # next code executed. Feed it something disposable.
        try:
            self.exec('pass')
        except Exception:
            pass

    def exec(self, code, timeout=8.0):
        """Run code in raw REPL; returns (stdout, traceback)."""
        self.p.write(code.encode() + b'\x04')
        got = self._read_until(b'\x04>', timeout)
        ok_at = got.find(b'OK')
        body = got[ok_at + 2:] if ok_at >= 0 else got
        body = body[:body.rfind(b'\x04>')]
        out, sep, err = body.partition(b'\x04')
        return out, err

    def exit_raw(self):
        self.p.write(b'\x02')             # ctrl-B: back to friendly REPL
        self.p.drain(0.3)

    def soft_reset(self):
        self.p.write(b'\x02')
        self.p.drain(0.2)
        self.p.write(b'\x04')             # ctrl-D at friendly REPL

    def close(self):
        self.p.close()


ENV_CODE = """
import sys, gc
gc.collect()
print('version:', sys.version)
print('mem_free:', gc.mem_free())
try:
    import struct
    print('struct: yes', struct.pack('>i', 3).hex() if hasattr(bytes, 'hex') else 'nohex')
except ImportError:
    print('struct: NO')
try:
    import micropython
    print('kbd_intr:', hasattr(micropython, 'kbd_intr'))
except ImportError:
    print('micropython module: NO')
try:
    import os
    print('fs:', os.listdir())
except Exception as e:
    print('fs error:', e)
try:
    from utime import ticks_us
    print('ticks_us: yes')
except ImportError:
    print('ticks_us: NO (fallback running_time*1000)')
"""


def cmd_env(r):
    out, err = r.exec(ENV_CODE)
    print(out.decode('utf-8', 'replace'))
    if err:
        print('ERR:', err.decode('utf-8', 'replace'))


def cmd_put(r, local, name=None):
    name = name or os.path.basename(local)
    data = open(local, 'rb').read()
    out, err = r.exec("f = open(%r, 'wb')\nprint('opened')" % name)
    if err:
        print('open failed:', err.decode())
        return 1
    for i in range(0, len(data), 256):
        chunk = data[i:i + 256]
        out, err = r.exec('f.write(%r)' % chunk)
        if err:
            print('write failed at %d: %s' % (i, err.decode()))
            return 1
    out, err = r.exec("f.close()\nimport os\nprint(os.size(%r) if hasattr(os,'size') else 'closed')" % name)
    print('put %s (%d bytes): %s %s' % (name, len(data),
                                        out.decode().strip(), err.decode().strip()))
    return 0


IMPTEST_CODE = """
import gc, sys
gc.collect(); a = gc.mem_free()
print('booted modules:', 'osc' in sys.modules)
import osc
gc.collect(); b = gc.mem_free()
print('mem before/after:', a, b)
m = osc.message('/a/0', 7)
print('msg:', ''.join('%02x' % c for c in m))
print('decode:', osc.messages(osc.bundle([osc.message('/d/5', 1)])))
"""


def cmd_imptest(r):
    out, err = r.exec(IMPTEST_CODE, timeout=20.0)
    print(out.decode('utf-8', 'replace'))
    if err:
        print('ERR:', err.decode('utf-8', 'replace'))
        return 1
    want = oscprobe.msg('/a/0', [7]).hex()
    ok = ('msg: ' + want) in out.decode()
    print('device bytes match host reference:', ok)
    return 0 if ok else 1


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'env'
    r = Repl()
    try:
        if cmd == 'reboot':
            r.soft_reset()
            print('soft reset sent')
            return 0
        if cmd == 'repl':
            r.p.write(b'\r\x03\x03\r')
            print(repr(r.p.drain(1.0)))
            return 0
        r.enter_raw()
        if cmd == 'env':
            cmd_env(r)
        elif cmd == 'put':
            rc = cmd_put(r, sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
            r.exit_raw()
            return rc
        elif cmd == 'imptest':
            rc = cmd_imptest(r)
            r.exit_raw()
            return rc
        r.exit_raw()
        return 0
    finally:
        r.close()


if __name__ == '__main__':
    sys.exit(main())

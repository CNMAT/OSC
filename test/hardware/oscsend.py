#!/usr/bin/env python3
"""Send one OSC message to a board over SLIP, or watch what it sends.

    python3 test/hardware/oscsend.py PORT send /addr [arg ...]   print the replies
    python3 test/hardware/oscsend.py PORT watch SECONDS          print every frame

Arguments are ints unless they contain a '.', then floats; anything else is a
string. Replies are collected for --wait seconds (default 0.6).

The SLIP codec, OSC encoder/decoder and raw-tty Port are oscprobe.py's,
imported from beside this file rather than copied.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import Port, slip_encode, slip_frames, msg, decode  # noqa: E402


def parse(a):
    try:
        return float(a) if '.' in a else int(a)
    except ValueError:
        return a


def flat(d):
    """Yield (addr, args) for a decoded message or bundle."""
    if d[0] == 'bundle':
        for e in d[1]:
            yield from flat(e)
    else:
        yield d


def main():
    argv = sys.argv[1:]
    wait = 0.6
    if '--wait' in argv:
        k = argv.index('--wait')
        wait = float(argv[k + 1])
        del argv[k:k + 2]
    port, cmd, rest = argv[0], argv[1], argv[2:]
    p = Port(port)
    try:
        if cmd == 'send':
            p.write(slip_encode(msg(rest[0], [parse(a) for a in rest[1:]])))
            raw = p.drain(wait)
        elif cmd == 'watch':
            raw = p.drain(float(rest[0]))
        else:
            sys.exit(f'unknown command {cmd!r}')
    finally:
        p.close()
    for f in slip_frames(raw):
        try:
            d = decode(f)
        except Exception as e:  # report, never guess
            print(f'<undecodable {f[:24].hex()}> {e}')
            continue
        if d[0] == 'bundle':
            print('bundle', [m for m in flat(d)])
        else:
            print(d)


if __name__ == '__main__':
    main()

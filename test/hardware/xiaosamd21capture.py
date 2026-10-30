#!/usr/bin/env python3
"""Record the bytes test-xiaosamd21-contract.mjs decodes, from a live board.

    python3 test/hardware/xiaosamd21capture.py PORT OUT.slip [--replace]

Drives a board running XiaoSamd21Oscuino through a fixed sequence and writes
every byte it sends back, SLIP framing included, to OUT.slip. An existing file
is refused unless --replace is given. The sequence, in order:

  FreeTouch         /enq, /cap/config at the defaults, /cap/3/intcap 20 (its
                    echo), then one second of stream at /rate 20
  FastTouch         /cap/lib FastTouch (echo and the hello it sends),
                    /cap/config, half a second of stream
  CapacitiveSensor  /cap/lib CapacitiveSensor (echo and hello), /cap/config
                    with no pins, /cap/send 10 and /cap/recv 9 8 (echoes and
                    hello), /cap/config with those pins, /cap/defaults
  and back          /cap/lib FreeTouch, /cap/defaults

The stream is stopped while CapacitiveSensor has pins: with no resistors
fitted every read times out, which holds the board for about 0.7 s a pad at
the default timeout.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import Port, slip_encode, msg  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    port, out = args[0], args[1]
    if os.path.exists(out) and '--replace' not in sys.argv:
        sys.exit(f'{out} exists; pass --replace to overwrite it')
    p = Port(port)
    rec = b''

    def say(addr, argv=(), wait=0.4):
        nonlocal rec
        p.write(slip_encode(msg(addr, list(argv))))
        rec += p.drain(wait)

    try:
        p.write(slip_encode(msg('/rate', [0])))
        p.drain(0.5)                       # not recorded: whatever was streaming
        p.write(slip_encode(msg('/cap/lib', ['FreeTouch'])))
        p.drain(0.6)
        p.write(slip_encode(msg('/cap/defaults')))
        p.drain(0.4)

        say('/enq')
        say('/cap/config')
        say('/cap/3/intcap', [20])
        say('/rate', [20], wait=1.0)
        say('/rate', [0], wait=0.5)

        say('/cap/lib', ['FastTouch'], wait=0.6)
        say('/cap/config')
        say('/rate', [20], wait=0.5)
        say('/rate', [0], wait=0.5)

        say('/cap/lib', ['CapacitiveSensor'], wait=0.6)
        say('/cap/config')
        say('/cap/send', [10])
        say('/cap/recv', [9, 8], wait=0.6)
        say('/cap/config')
        say('/cap/defaults', wait=0.6)

        say('/cap/lib', ['FreeTouch'], wait=0.6)
        say('/cap/defaults')
    finally:
        p.close()
    tmp = out + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(rec)
    os.replace(tmp, out)
    print(f'{len(rec)} bytes -> {out}')


if __name__ == '__main__':
    main()

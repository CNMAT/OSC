#!/usr/bin/env python3
"""Test a board against ADDRESSES.md -- driven by what the board announces.

    python3 test/hardware/contractprobe.py <port> [Name] [/retired ...] [flags]

The board is asked `/enq`. It answers with its name and one `/enq/<capability>`
line per thing it actually has, carrying that thing's shape. This probe then
tests exactly those capabilities and nothing else: a board that says
`/enq/imu 6` gets six floats checked because it said six; a board that never
mentions an IMU is never asked for one. There is no per-board script and no
per-board knowledge here -- the contract is the test, and the board's own
announcement selects which parts of it apply.

Positional arguments after the port: a bare word is the name `/enq` must
answer; anything starting with `/` is a RETIRED address that must now draw no
reply at all. Pass the ones the board used before the rename (`/mg /mg/led`
for the XIAO MG24) -- that negative test is what proves an old dialect is gone
rather than merely undocumented.

Flags:
  --sound    let /buzz actually make a noise (default: only tests the stop form)
  --actuate  also drive motor/servo/relay. OFF by default because those move
             physical things that may be attached to something breakable.
  --quiet    one summary line, for pasting into TODO.md's ledger

Reuses oscprobe's codec and port class, so run it from the repository root.
Writes stay small on purpose: the XIAO MG24's CMSIS-DAP VCOM drops past ~242 B
in one write and wedges past ~396 B (BOARDS.md).
"""

import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


# --------------------------------------------------------------- the contract
# One row per capability, transcribed from the table in ADDRESSES.md. `ask` is
# the read request; `reply` the address it must answer on; `tag` the OSC type
# every argument must have; `n` how many arguments are expected:
#   an int   -- exactly that many
#   'enq'    -- as many as the /enq line's first parameter said
#   'enq1'   -- same, defaulting to 1 when the /enq line carries no shape
#   'min'    -- at least that many (the tuple's second element)
CAPS = {
    'btn':   dict(ask='/btn',   reply='/btn',   tag='i', n='enq'),
    'imu':   dict(ask='/imu',   reply='/imu',   tag='f', n='enq'),
    'cap':   dict(ask='/cap',   reply='/cap',   tag='i', n='enq'),
    'joy':   dict(ask='/joy',   reply='/joy',   tag='i', n='enq'),
    'pot':   dict(ask='/pot',   reply='/pot',   tag='i', n='enq'),
    'bat':   dict(ask='/bat',   reply='/bat',   tag='i', n='enq1'),
    'light': dict(ask='/light', reply='/light', tag='i', n=1),
    'temp':  dict(ask='/temp',  reply='/temp',  tag='f', n=1),
    'hum':   dict(ask='/hum',   reply='/hum',   tag='f', n=1),
    'chg':   dict(ask='/chg',   reply='/chg',   tag='i', n=2),
    'enc':   dict(ask='/enc',   reply='/enc',   tag='i', n=2),
    'rtc':   dict(ask='/rtc',   reply='/rtc',   tag='i', n=6),
    'mic':   dict(ask='/mic',   reply='/mic',   tag='i', n=('min', 2)),
}

# Actuators: written with a value chosen to be harmless, then checked for the
# echo the contract promises. Anything that moves is behind --actuate.
ACTUATORS = {
    'rgb':     dict(addr='/rgb', args=(0, 0, 0), reply='/rgb',
                    note='sets the LEDs dark'),
    'buzz':    dict(addr='/buzz', args=(0,), reply='/buzz',
                    note='the stop form; --sound to hear one'),
    'display': dict(addr='/display/text', args=('osc contract',),
                    reply='/display/text', note='writes one line'),
}
MOVING = ('motor', 'servo', 'relay')

# Announced but not asserted here, with the reason.
PASSIVE = {
    'touch': 'streamed only while a finger is down',
    'cam':   'frames are large; use /stream from the page',
    'net':   'address and signal came in the enq bundle',
    'diag':  'free text, never parsed',
    'rfid':  'needs a tag present to mean anything',
}


def load_oscprobe(port):
    spec = importlib.util.spec_from_file_location(
        'oscprobe', os.path.join(HERE, 'oscprobe.py'))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ['oscprobe', port]
    try:
        spec.loader.exec_module(mod)      # main() is guarded; this only defines
    except SystemExit:                    # helpers
        pass
    finally:
        sys.argv = saved
    return mod


def main():
    args = sys.argv[1:]
    flags = {a for a in args if a.startswith('--')}
    args = [a for a in args if not a.startswith('--')]
    if not args:
        print(__doc__)
        return 2
    port = args[0]
    retired = [a for a in args[1:] if a.startswith('/')]
    expect_name = next((a for a in args[1:] if not a.startswith('/')), None)
    quiet = '--quiet' in flags

    op = load_oscprobe(port)
    Port = next(c for c in vars(op).values()
                if isinstance(c, type) and hasattr(c, 'drain'))
    p = Port(port)
    time.sleep(0.3)
    p.drain(0.5)

    state = {'pass': 0, 'fail': 0, 'skip': 0}

    def say(*a):
        if not quiet:
            print(*a)

    # Two framings exist in this repository and they are mutually exclusive.
    # A sketch that receives with OSCBundle + route() rejects a bare message;
    # one that receives with OSCMessage + dispatch() rejects a bundle. Neither
    # is wrong -- both are the library's documented API -- so the probe finds
    # out which this board speaks instead of assuming, and says so. Without
    # this it reports a healthy board as answering nothing at all.
    framing = {'mode': 'bundle'}

    def encode(elems):
        msgs = [op.msg(a, tuple(v)) for a, v in elems]
        if framing['mode'] == 'bundle':
            return [op.slip_encode(op.bundle(msgs))]
        return [op.slip_encode(m) for m in msgs]   # one frame per message

    def ask(elems, wait=0.5):
        for frame in encode(elems):
            p.write(frame)
        out = []
        for f in op.slip_frames(p.drain(wait)):
            d = op.decode(f)
            out += d[1] if d[0] == 'bundle' else [d]
        return out

    def check(label, cond, got=''):
        state['pass' if cond else 'fail'] += 1
        say(f"  {'ok  ' if cond else 'FAIL'} {label}" + (f": {got}" if got else ''))
        return cond

    def skip(label, why):
        state['skip'] += 1
        say(f"  --   {label}: {why}")

    say(f"port {port}\n")

    # ---- the greeting, which selects everything that follows ----------------
    say("Announcement")
    reply = ask([('/enq', ())], wait=0.8)
    if not [m for m in reply if m[0] == '/enq']:
        framing['mode'] = 'message'          # try the other one before failing
        reply = ask([('/enq', ())], wait=0.8)
        if [m for m in reply if m[0] == '/enq']:
            say("       (this board takes bare messages, not bundles)")
        else:
            framing['mode'] = 'bundle'       # neither worked; report as bundle
    elif not quiet:
        say("       (this board takes bundles)")
    hello = [m for m in reply if m[0] == '/enq']
    check('/enq answers with a name' + (f' = {expect_name}' if expect_name else ''),
          bool(hello) and (expect_name is None or hello[0][1] == [expect_name]),
          hello[0][1] if hello else reply)
    name = hello[0][1][0] if hello and hello[0][1] else '?'

    enq = {}
    for m in reply:
        if m[0].startswith('/enq/'):
            enq[m[0][5:]] = m[1]
    if not enq:
        say("       no /enq lines: this board announces no capabilities, so the")
        say("       sweep below has nothing to select. Core checks still run.")
    else:
        say(f"       {name} announces: " +
            ', '.join(f"{k}{v if v else ''}" for k, v in sorted(enq.items())))
    unknown = sorted(set(enq) - set(CAPS) - set(ACTUATORS) - set(PASSIVE) - set(MOVING))
    if unknown:
        check('every announced capability is in ADDRESSES.md', False,
              f"unknown: {', '.join(unknown)}")

    # ---- core, on every board ----------------------------------------------
    say("\nCore")
    r = ask([('/state', ())])
    check('/state -> /state <seq> <millis>',
          any(m[0] == '/state' and len(m[1]) == 2 for m in r), r)
    # /s/l is core, but not every board HAS a plain LED -- the XIAO ESP32-C3's
    # only LED belongs to its battery charger, and its variant defines no
    # LED_BUILTIN, so OSCBoards.h leaves BOARD_HAS_LED undefined and the
    # sketch correctly answers nothing. Under "absence is silence" that is the
    # contract being obeyed, not broken, so silence here is a skip. A board
    # that answers wrongly is still caught, because the echo must match.
    first = ask([('/s/l', (1,))])
    if not [m for m in first if m[0] == '/s/l']:
        skip('/s/l', 'no reply: this board has no LED under BOARD_HAS_LED')
    else:
        check('/s/l 1 echoed', any(m[0] == '/s/l' and m[1] == [1] for m in first), first)
        r = ask([('/s/l', (0,))])
        check('/s/l 0 echoed', any(m[0] == '/s/l' and m[1] == [0] for m in r), r)
    r = ask([('/s/m', ())])
    check('/s/m answers', any(m[0] == '/s/m' for m in r), r)

    say("\nStreaming")
    r = ask([('/rate', (50,))], wait=1.0)
    check('/rate 50 echoed', any(m[0] == '/rate' and m[1] == [50] for m in r),
          [m for m in r if m[0] == '/rate'])
    states = [m for m in r if m[0] == '/state']
    seqs = [m[1][0] for m in states]
    check(f'{len(states)} /state packets in ~1 s, sequence strictly increasing',
          len(states) >= 10 and all(b == a + 1 for a, b in zip(seqs, seqs[1:])),
          seqs[:6])
    # What else rides in the stream is the board's business, but every message
    # in it should be one the board announced.
    streamed = sorted({m[0] for m in r} - {'/state', '/rate'})
    if streamed:
        roots = {a.split('/')[1] for a in streamed}
        stray = sorted(roots - set(enq) - {'d', 'a', 's', 'tone'})
        check('every streamed capability was announced', not stray,
              f"streamed {', '.join(streamed)}" + (f"; unannounced: {stray}" if stray else ''))
    r = ask([('/rate', (0,))], wait=0.8)
    time.sleep(0.5)
    quiet_bytes = p.drain(0.5)
    check('/rate 0 stops the stream',
          any(m[0] == '/rate' and m[1] == [0] for m in r)
          and not op.slip_frames(quiet_bytes),
          f"bytes after stop: {len(quiet_bytes)}")

    # ---- the sweep: only what this board said it has ------------------------
    if enq:
        say("\nAnnounced capabilities")
    for capname in sorted(enq):
        shape = enq[capname]
        if capname in CAPS:
            spec = CAPS[capname]
            r = ask([(spec['ask'], ())])
            got = [m for m in r if m[0] == spec['reply']]
            if not check(f"{spec['ask']} answers on {spec['reply']}", bool(got), r):
                continue
            a = got[0][1]
            want = spec['n']
            if want == 'enq':
                want = shape[0] if shape else None
            elif want == 'enq1':
                want = shape[0] if shape else 1
            if isinstance(want, tuple):                     # ('min', k)
                check(f"  {spec['reply']} carries at least {want[1]} values",
                      len(a) >= want[1], a)
            elif want is None:
                check(f"  {spec['reply']} carries values", len(a) >= 1, a)
            else:
                check(f"  {spec['reply']} carries {want} value(s) as announced",
                      len(a) == want, a)
            tag = spec['tag']
            typ = {'i': int, 'f': float, 's': str}[tag]
            # An int where a float belongs is the classic contract slip: the
            # contract says /imu is g, and 0 is not the same as 0.0 on the wire.
            check(f"  every value is a {'float' if tag == 'f' else tag}",
                  all(isinstance(x, typ) and not isinstance(x, bool) for x in a),
                  [type(x).__name__ for x in a])
        elif capname in ACTUATORS:
            act = ACTUATORS[capname]
            if capname == 'buzz' and '--sound' in flags:
                act = dict(act, args=(880, 150), note='880 Hz for 150 ms -- listen')
            r = ask([(act['addr'], act['args'])], wait=0.6)
            check(f"{act['addr']} {' '.join(map(str, act['args']))} echoed on "
                  f"{act['reply']} ({act['note']})",
                  any(m[0] == act['reply'] for m in r), r)
        elif capname in MOVING:
            if '--actuate' in flags:
                addr = f'/{capname}/0'
                arg = (90,) if capname == 'servo' else (0, 0) if capname == 'motor' else (0,)
                r = ask([(addr, arg)], wait=0.6)
                check(f"{addr} echoed", any(m[0] == addr for m in r), r)
            else:
                skip(f'/{capname}/<n>', 'moves something; pass --actuate')
        elif capname in PASSIVE:
            skip(f'/{capname}', PASSIVE[capname])

    # ---- the negative test: the old dialect must be gone --------------------
    if retired:
        say("\nRetired addresses")
        r = ask([(a, ()) for a in retired], wait=0.8)
        # "No reply" means nothing came back ON THOSE ADDRESSES. It does not
        # mean silence: a board streaming at /rate is talking the whole time,
        # and counting that as a reply failed every board with a live stream.
        echoes = [m for m in r
                  if any(m[0] == a or m[0].startswith(a + '/') for a in retired)]
        check(f"{' '.join(retired)} draw no reply", not echoes,
              echoes if echoes else f"(ignored {len(r)} streamed messages)")

    tally = (f"{name}: {state['pass']} passed, {state['fail']} failed"
             + (f", {state['skip']} skipped" if state['skip'] else '')
             + (f" | caps: {', '.join(sorted(enq))}" if enq else ' | no capabilities announced'))
    print(('\n' if not quiet else '') + tally)
    return 1 if state['fail'] else 0


if __name__ == '__main__':
    sys.exit(main())

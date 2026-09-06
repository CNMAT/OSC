# Working rules for this repository

The general rules — sources and evidence, verification, files and state, git,
delegation, working with Adrian — live in his global `~/.claude/CLAUDE.md`,
hoisted 2026-09-06 from every repo on his other machine. **They are the
authority and they are not copied here**, because a copy drifts and those rules
were bought with real failures.

That file does not travel with this repository, so a session on a machine
without it is missing them. If you are such a session, say so rather than
proceeding as though the guidance below is complete. The rules it carries that
this repository has already violated, in one session, are worth naming: *"not
found" means "not where I looked"*, *read the primary source before you assert*,
*nothing that must outlive the turn lives in /tmp*, and *a uniform negative is a
harness bug until shown otherwise*.

What follows is only what is specific to this library.

## The authorities here

[ADDRESSES.md](./ADDRESSES.md) is the OSC address contract.
[BRINGUP.md](./BRINGUP.md) is how a board is brought up, and
[BOARDS.md](./BOARDS.md) records what has actually run on hardware, with the
Method rules for measurement in
[test/hardware/README.md](./test/hardware/README.md) — trickle gate passed,
three repeats, same-day reference board, mechanism named.
`.claude/skills/board-bringup/` is BRINGUP.md's operating summary and is
committed, so it does travel.

## The address space is enforced, not just documented

`ADDRESSES.md` is the contract, and three tests fail on any drift from it:
`test-namespace.mjs` on an address outside the document, `test-announce.mjs`
when a board announces a capability nothing answers, `test-panels.mjs` when the
contract gains a capability the page forgot. `make -C extras/webserial all`
runs them.

**Absence is silence.** A capability a board lacks is missing from its `/enq`
greeting and answers nothing — no booleans, no sentinel values, and never a
reply claiming success from hardware that is not there. A board built against a
generic variant will inherit peripherals it does not have; `boards.json` takes
`defines` (`OSC_NO_RGB`, `OSC_NO_LED`) so it cannot announce them.

Examples are named `<Board><Chip><Transport>`; the convention and its three
deliberate exceptions are in `extras/webserial/README.md`.

## Hardware specifics

**The chip is not the board.** Every native-USB ESP32 enumerates as
`303a:1001`; pin maps, LED polarity and FQBN defaults belong to the carrier.
Ask which board it is and weight the answer above your own inference.

**The vendor's peripheral list is a work list.** Every peripheral the
documentation names ends up either announced as `/enq/<capability>` or written
into the board's `boards.json` note as present-but-not-wired. Prefer the
vendor's own pin header to its README: the T-Encoder-Pro's `pin_config.h` named
a haptic engine, IMU, RTC and PMIC that the README summary did not.

**Ports renumber, and boards drop off hubs.** Re-list immediately before every
upload; never reuse a port name across two commands.

**FQBN options are per-board.** Read `boards.txt`; never carry another board's
options across. Give `arduino-cli` a private `--build-path` per sketch or
parallel builds stomp each other's objects.

**Asking a human to press or turn something needs the ask to arrive first.**
Tool output does not reach them until the turn ends, so a prompt printed inside
a sampling loop is read after the window has shut. Start the sampler in the
background, then ask.

## Credentials

WiFi credentials live in `arduino_secrets.h`: git-ignored twice over, refused by
`tools/pre-commit`, which also greps added lines for `ssid`/`password`/`pass`/
`psk` patterns. Never edit the placeholder defaults in a tracked sketch. Sweep
the staged diff before any push — this remote is public.

## Git here

The default branch is **`master`**; there is no `main`. Commit as
`adrian@adrianfreed.com` — an unset `user.email` makes git invent a hostname
address — and end messages with the `Co-Authored-By` trailer. Commit and push
only when asked.

`git add -A` has twice swept up in-flight files that were not mine. Check
`git status` before staging and leave what you did not write.

## This machine

**The Seeed nRF52 core shells out to `python`**, which modern macOS lacks. Use a
shim directory on PATH holding a *script* that execs an *absolute*
`/usr/bin/python3`. A symlink does not work — `/usr/bin/python3` is the Command
Line Tools stub and dispatches on `argv[0]`. Never write into the Homebrew
prefix, and never omit the absolute path: `exec python3 "$@"` recurses forever,
and that exact line overwrote Homebrew's binary here and broke every ESP32 build
for sixteen hours.

**macOS denies CoreBluetooth to command-line tools** with no prompt at all, so
BLE testing goes through Chrome and Web Bluetooth. Serial is never blocked.

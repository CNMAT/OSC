# Working rules for this repository

The general rules — sources and evidence, verification, files and state, git,
delegation, working with Adrian — live in his global `~/.claude/CLAUDE.md`.
**That file is the authority and is not copied here**, because a copy drifts
and those rules were bought with real failures.

It does not travel with a repository. A session on a machine without it is
missing them and should say so rather than proceeding as though what follows is
complete.

Everything specific to this library is enforced by something executable rather
than written down as a habit to remember. The rule and the thing that checks it
are the same object:

| what it enforces | run |
|---|---|
| every OSC address is in the contract | `extras/webserial/test/test-namespace.mjs` |
| a board answers what it announces | `extras/webserial/test/test-announce.mjs` |
| the page has a panel for every capability | `extras/webserial/test/test-panels.mjs` |
| generated sketches match the template | `extras/webserial/check.mjs` |
| the probe itself still detects a broken board | `test/hardware/test_contractprobe.py` |
| a board obeys the contract on the wire | `test/hardware/contractprobe.py PORT` |
| what is actually plugged in, by identity not port name | `test/hardware/census.py --probe` |
| a person must press, turn or watch something | `test/hardware/humanprobe.py` |
| the `python` a core needs, without breaking Homebrew | `tools/python-shim.sh` |
| no credential reaches a commit | `tools/pre-commit` (installed by `tools/install-hooks.sh`) |

`make -C extras/webserial all` runs the host suite. Each tool's header says
which failure bought it.

## The documents

[ADDRESSES.md](./ADDRESSES.md) is the OSC address contract — capability-named,
and *absence is silence*: what a board lacks is missing from its `/enq`
greeting and answers nothing.

[BRINGUP.md](./BRINGUP.md) is how a board is brought up, and
[BOARDS.md](./BOARDS.md) records what has run on hardware, under the Method in
[test/hardware/README.md](./test/hardware/README.md) — trickle gate, three
repeats, same-day reference, mechanism named.
`.claude/skills/board-bringup/` is BRINGUP.md's operating summary and is
committed, so it travels with the repository.

Naming is `<Board><Chip><Transport>`; the convention and its three deliberate
exceptions are in `extras/webserial/README.md`.

## Git here

The default branch is **`master`**; there is no `main`. Commit as
`adrian@adrianfreed.com` — an unset `user.email` makes git invent a hostname
address — and end messages with the `Co-Authored-By` trailer. Commit and push
only when asked, and check `git status` before staging: `git add -A` has twice
swept up in-flight files belonging to someone else.

# Web page generator for the Oscuino sketches. Needs node; no npm install, no
# dependencies.
#
#   make generate   write examples/<Sketch>/<Sketch>.{ino,html} (only the .html
#                   beside a hand-written sketch), the python firmware pairs
#                   under extras/python/, and the board-less oscuino.html here
#   make check      fail if any generated file drifted from the template,
#                   oscuino.html included
#   make test       codec bytes, the capability model, per-board page wiring,
#                   the CircuitPlayground, XIAO SAMD21 and KB2040 pairs, the
#                   address-space contract, and that the Web Serial pages let
#                   go of their port
#   make all        check + test, the set worth running before a commit
#
# test-cpx-contract.mjs, test-xiaosamd21-contract.mjs and
# test-kb2040-contract.mjs are the odd ones out: they cover hand-written
# sketch-and-page pairs, which `make check` cannot see at all because they are
# not generated from boards.json. The first checks that
# CircuitPlaygroundSensors and its page agree about the shape of the stream; the
# second that XiaoSamd21Oscuino, its page and ADDRESSES.md name the same touch
# libraries, /cap set-up parameters and Wii fields, and checks the page's codec
# on bytes captured from the board, its framing, what it asks back and its
# Nunchuk drawing; the third that KB2040Oscuino's Wii code and page are
# XiaoSamd21Oscuino's where they say so, that it and its page agree on
# addresses, that the page's drawing is the two boards' files' (set
# KB2040_BRD and ADAPTER_BRD to re-read the files themselves), and that its
# CREDITS.md gives those files' terms where the page and brdgeom.py cite them.
# test/mutants-xiaosamd21.mjs and test/mutants-kb2040.mjs grade the last two
# by mutation, and test/quotes-xiaosamd21.mjs checks the quotations in the
# XIAO and KB2040 pages' I2C panes against their sources; run those by hand
# (the quotations need the network). test-serial-close.mjs also reaches past
# the template: it runs seven hand-written pages, and the board-less generated
# one, against a fake port that will not close while its stream is locked, and
# one that will not close at all; test/mutants-serial-close.mjs grades its
# checks of the XIAO SAMD21 page by mutation (run it by hand).

NODE = node

generate:
	$(NODE) generate.mjs

check:
	$(NODE) check.mjs

test:
	$(NODE) test/test-codec.mjs
	$(NODE) test/test-panels.mjs
	$(NODE) test/test-pages.mjs
	$(NODE) test/test-cpx-contract.mjs
	$(NODE) test/test-xiaosamd21-contract.mjs
	$(NODE) test/test-kb2040-contract.mjs
	$(NODE) test/test-namespace.mjs
	$(NODE) test/test-announce.mjs
	$(NODE) test/test-serial-close.mjs

all: check test

.PHONY: generate check test all

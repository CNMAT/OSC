# Web page generator for the Oscuino sketches. Needs node; no npm install, no
# dependencies.
#
#   make generate   write examples/<Sketch>/<Sketch>.{ino,html} (only the .html
#                   beside a hand-written sketch), the python firmware pairs
#                   under extras/python/, and the board-less oscuino.html here
#   make check      fail if any generated file drifted from the template,
#                   oscuino.html included
#   make test       codec bytes, the capability model, per-board page wiring,
#                   the CircuitPlayground and XIAO SAMD21 pairs, and the
#                   address-space contract
#   make all        check + test, the set worth running before a commit
#
# test-cpx-contract.mjs and test-xiaosamd21-contract.mjs are the odd ones out:
# they cover hand-written sketch-and-page pairs, which `make check` cannot see
# at all because they are not generated from boards.json. The first checks that
# CircuitPlaygroundSensors and its page agree about the shape of the stream; the
# second that XiaoSamd21Oscuino, its page and ADDRESSES.md name the same touch
# libraries and /cap set-up parameters, and that the page decodes bytes
# captured from the board through all three libraries.
# test/mutants-xiaosamd21.mjs grades the second by mutation; run it by hand.

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
	$(NODE) test/test-namespace.mjs
	$(NODE) test/test-announce.mjs

all: check test

.PHONY: generate check test all

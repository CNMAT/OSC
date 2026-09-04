# Web page generator for the Oscuino sketches. Needs node; no npm install, no
# dependencies.
#
#   make generate   write examples/<Sketch>/<Sketch>.{ino,html} (only the .html
#                   beside a hand-written sketch), the python firmware pairs
#                   under extras/python/, and the board-less oscuino.html here
#   make check      fail if any generated file drifted from the template,
#                   oscuino.html included
#   make test       codec bytes, the capability model, per-board page wiring,
#                   the CircuitPlayground pair, and the address-space contract
#   make all        check + test, the set worth running before a commit
#
# test-cpx-contract.mjs is the odd one out: it covers the hand-written
# CircuitPlaygroundSensors sketch and page, which `make check` cannot see at all
# because they are not generated from boards.json. It checks that the two still
# agree about the shape of the stream.

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
	$(NODE) test/test-namespace.mjs
	$(NODE) test/test-announce.mjs

all: check test

.PHONY: generate check test all

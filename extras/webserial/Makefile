# Web Serial example generator. Needs node; no npm install, no dependencies.
#
#   make generate   write examples/<Id>Oscuino/<Id>Oscuino.{ino,html}
#   make check      fail if any generated file drifted from the template
#   make test       codec bytes + per-board page wiring + the CircuitPlayground pair
#   make all        check + test, the set worth running before a commit
#
# test-cpx-contract.mjs is the odd one out: it covers the hand-written
# CircuitPlaygroundSensors sketch and page, which `make check` cannot see at all
# because they are not generated from boards.json. It checks that the two still
# agree about the layout of /cpx.

NODE = node

generate:
	$(NODE) generate.mjs

check:
	$(NODE) check.mjs

test:
	$(NODE) test/test-codec.mjs
	$(NODE) test/test-pages.mjs
	$(NODE) test/test-cpx-contract.mjs

all: check test

.PHONY: generate check test all

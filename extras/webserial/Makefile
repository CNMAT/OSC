# Web Serial example generator. Needs node; no npm install, no dependencies.
#
#   make generate   write examples/<Id>Oscuino/<Id>Oscuino.{ino,html}
#   make check      fail if any generated file drifted from the template
#   make test       codec bytes + per-board page wiring
#   make all        check + test, the set worth running before a commit

NODE = node

generate:
	$(NODE) generate.mjs

check:
	$(NODE) check.mjs

test:
	$(NODE) test/test-codec.mjs
	$(NODE) test/test-pages.mjs

all: check test

.PHONY: generate check test all

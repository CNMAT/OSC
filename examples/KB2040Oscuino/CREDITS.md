# Third-party material in this example

## The two boards' geometry

`KB2040Oscuino.html` draws the Adafruit KB2040 and the Adafruit Wii Nunchuck
Breakout Adapter from numbers read out of Adafruit's Eagle board files: each
board's outline, its parts' places and boxes, its pads with their sizes,
holes and nets, and the adapter's mounting holes, fiducials and silkscreen
texts. They are the `KB` and `AD` constants in the page's script (its
section 4, GEOMETRY), and the drawing the page makes from them.
`extras/webserial/test/kb2040-boards.json` holds every number `brdgeom.py`
reads from the two files, and `test-kb2040-contract.mjs` holds the page to
it; `mutants-kb2040.mjs` copies a few of the page's numbers as the places it
breaks.

* Design: **Limor Fried/Ladyada for Adafruit Industries**
* Licence: **CC BY-SA 3.0** — https://creativecommons.org/licenses/by-sa/3.0/
  (each repository's license.txt is the Attribution-ShareAlike 3.0 Unported
  legal code, and its README gives the files under Creative Commons
  Attribution/Share-Alike; both read 2026-10-30)
* Sources:
  * "Adafruit KB2040.brd", from Adafruit KB2040 - RP2040 Kee Boar Driver PCB,
    https://github.com/adafruit/Adafruit-KB2040-PCB at commit
    b4160dcf4385ef2cc30e9652489328321720ee4b
  * "Adafruit Wii Nunchuck Breakout Adapter.brd", from Adafruit Wii Nunchuck
    Breakout Adapter PCB,
    https://github.com/adafruit/Adafruit-Wii-Nunchuck-Breakout-Adapter-PCB at
    commit d3f2e25c4f9211553e32b36ffce03212f414ead3
* Modifications: the board files themselves are not here. `brdgeom.py` reads
  their numbers, turned and mirrored as Eagle places each part, and the page
  keeps the ones it draws, the KB2040 as it lies and the adapter a quarter
  turn clockwise. The cables, the plugs and the parts' faces are drawn by eye
  and come from no file.
* Each README ends by asking that all the text above that request be
  included in any redistribution; that text is in the README at each commit
  above.

CC BY-SA 3.0 is a share-alike licence. It attaches to this material and to
any adapted version of it, not to the rest of the library, which keeps its
own licence — see ../../LICENSE. The sketch (`KB2040Oscuino.ino` and
`WiiExtension.h`) and `brdgeom.py` hold none of it. If that is not acceptable
for your distribution, leave out the page and the fixture above, with the
tests that read them (`test-kb2040-contract.mjs`, `mutants-kb2040.mjs`, and
the KB2040 entries in `test-serial-close.mjs` and `quotes-xiaosamd21.mjs`);
the sketch builds and runs without them.

## The two boards' wiring

The sketch's header and the page's Wiring text say which connector pad
carries which line, and which resistors pull the lines up, with their values,
as the two boards' schematics show them: "Adafruit KB2040.sch" and "Adafruit
Wii Nunchuck Breakout Adapter.sch", in the same repositories at the same
commits as the board files above, by the same designer, under the same
licence.

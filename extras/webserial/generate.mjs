/* Writes every generated example pair: examples/<Id>Oscuino/<Id>Oscuino.{ino,html}
   for Arduino boards, extras/python/<Id>Oscuino/{main.py|code.py,boot.py,<Id>Oscuino.html}
   for python firmwares. Run: node generate.mjs   (or `make generate`). */

import { mkdirSync, writeFileSync, readFileSync, existsSync } from "node:fs";
import { BOARDS, outputs } from "./render.mjs";

let written = 0, unchanged = 0, files = 0;

for (const board of BOARDS) {
  for (const out of outputs(board)) {
    files++;
    mkdirSync(out.dir, { recursive: true });
    // Skip identical writes so mtimes stay put and the Arduino IDE does not
    // decide every example changed each time this runs.
    if (existsSync(out.url) && readFileSync(out.url, "utf8") === out.body) { unchanged++; continue; }
    writeFileSync(out.url, out.body);
    written++;
    console.log(`  wrote  ${out.rel}`);
  }
}

console.log(`\n${BOARDS.length} boards, ${files} files — ${written} written, ${unchanged} already current\n`);

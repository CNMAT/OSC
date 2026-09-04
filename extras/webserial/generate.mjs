/* Writes every generated file: examples/<Sketch>/<Sketch>.{ino,html} for
   Arduino boards (only the .html when the sketch is hand-written),
   extras/python/<Id>Oscuino/{main.py|code.py,boot.py,<Id>Oscuino.html} for
   python firmwares, and the board-less extras/webserial/oscuino.html.
   Run: node generate.mjs   (or `make generate`). */

import { mkdirSync, writeFileSync, readFileSync, existsSync } from "node:fs";
import { BOARDS, allOutputs } from "./render.mjs";

let written = 0, unchanged = 0, files = 0;

for (const out of allOutputs()) {
  files++;
  mkdirSync(out.dir, { recursive: true });
  // Skip identical writes so mtimes stay put and the Arduino IDE does not
  // decide every example changed each time this runs.
  if (existsSync(out.url) && readFileSync(out.url, "utf8") === out.body) { unchanged++; continue; }
  writeFileSync(out.url, out.body);
  written++;
  console.log(`  wrote  ${out.rel}`);
}

console.log(`\n${BOARDS.length} boards + the universal page, ${files} files — ${written} written, ${unchanged} already current\n`);

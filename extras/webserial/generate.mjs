/* Writes examples/<Id>Oscuino/<Id>Oscuino.{ino,html} for every board in
   boards.json. Run: node generate.mjs   (or `make generate`). */

import { mkdirSync, writeFileSync, readFileSync, existsSync } from "node:fs";
import { BOARDS, EXAMPLES_DIR, render, sketchName } from "./render.mjs";

let written = 0, unchanged = 0;

for (const board of BOARDS) {
  const name = sketchName(board);
  const dir = new URL(`${name}/`, EXAMPLES_DIR);
  mkdirSync(dir, { recursive: true });

  const { html, ino } = render(board);
  for (const [file, body] of [[`${name}.ino`, ino], [`${name}.html`, html]]) {
    const path = new URL(file, dir);
    // Skip identical writes so mtimes stay put and the Arduino IDE does not
    // decide every example changed each time this runs.
    if (existsSync(path) && readFileSync(path, "utf8") === body) { unchanged++; continue; }
    writeFileSync(path, body);
    written++;
    console.log(`  wrote  examples/${name}/${file}`);
  }
}

console.log(`\n${BOARDS.length} boards — ${written} file(s) written, ${unchanged} already current\n`);

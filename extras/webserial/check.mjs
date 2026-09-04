/* Drift check: re-renders every generated file in memory and compares it to
   what is on disk — every board's page (and .ino, unless the sketch is
   hand-written), the python firmwares, and the board-less oscuino.html. Fails
   if any generated file has been hand-edited or if a template change was
   never regenerated. Run: node check.mjs   (or `make check`).

   This is the safeguard that makes N self-contained copies of a 250-line codec
   an acceptable trade rather than a maintenance trap. */

import { readFileSync, existsSync } from "node:fs";
import { allOutputs } from "./render.mjs";

let ok = 0;
const problems = [];

for (const out of allOutputs()) {
  if (!existsSync(out.url)) { problems.push([out.rel, "missing — run `make generate`"]); continue; }

  const got = readFileSync(out.url, "utf8");
  if (got === out.body) { ok++; console.log(`  ok    ${out.rel}`); continue; }

  // Point at the first differing line; "they differ" alone is useless when the
  // file is 500 lines of generated HTML.
  const g = got.split("\n"), w = out.body.split("\n");
  let i = 0;
  while (i < g.length && i < w.length && g[i] === w[i]) i++;
  problems.push([out.rel, `drifted at line ${i + 1}\n         on disk : ${JSON.stringify(g[i] ?? "<eof>")}\n         expected: ${JSON.stringify(w[i] ?? "<eof>")}`]);
}

console.log("");
for (const [rel, why] of problems) console.log(`  DRIFT ${rel}\n         ${why}`);
console.log(`\n${ok} current, ${problems.length} drifted\n`);

if (problems.length) {
  console.log("Generated files are not editable. Change extras/webserial/template.*,");
  console.log("template-*.py or boards.json, then run `make generate`.\n");
}
process.exit(problems.length ? 1 : 0);

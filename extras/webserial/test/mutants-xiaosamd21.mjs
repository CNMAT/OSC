/* Grades test-xiaosamd21-contract.mjs by mutation: each mutant breaks one
   thing the test claims to guard, in a scratch copy of the files it reads, and
   the test must fail on every one. The unmutated copy must pass, so a failure
   below is the mutant's and not the copy's.

   Run: node test/mutants-xiaosamd21.mjs   No dependencies. */

import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, copyFileSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { tmpdir } from "node:os";
import { join } from "node:path";

const ROOT = new URL("../../../", import.meta.url).pathname;
const FILES = ["ADDRESSES.md",
               "examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino",
               "examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.html",
               "extras/webserial/test/test-xiaosamd21-contract.mjs",
               "extras/webserial/test/xiaosamd21-2026-10-28.slip"];
const INO = FILES[1], HTML = FILES[2], SLIP = FILES[4];

const MUTANTS = [
  ["page PARAMS loses hop", HTML, '"freq", "hop", "compcap"', '"freq", "compcap"'],
  ["sketch renames intcap", INO, '"/cap/intcap"', '"/cap/intcapx"'],
  ["ADDRESSES.md reorders compcap and intcap", "ADDRESSES.md",
   "| `compcap` | 0..16383 | compensation capacitor code |\n| `intcap` | 0..63 | integration capacitor code |",
   "| `intcap` | 0..63 | integration capacitor code |\n| `compcap` | 0..16383 | compensation capacitor code |"],
  ["sketch header drops the hop row", INO, " *   hop         1..16", " *   (hop)       1..16"],
  ["page DEFAULTS compcap drifts", HTML, "compcap: 8192, intcap: 63 }", "compcap: 8000, intcap: 63 }"],
  ["sketch dispatches /cap by exact match", INO,
   'bundleIN.route("/cap",          routeCap);', 'bundleIN.dispatch("/cap",       askOnly);'],
  ["page loses D9's fader", HTML, '["D9",    9, "R", 332.5, 385.0, 3]', '["D9",    9, "R", 332.5, 385.0, null]'],
  ["page encodes ints as floats", HTML, 'ostr("," + "i".repeat(ints.length))', 'ostr("," + "f".repeat(ints.length))'],
  ["capture truncated", SLIP, null, null],
];

function copyTree(dir) {
  for (const f of FILES) {
    mkdirSync(join(dir, f, ".."), { recursive: true });
    copyFileSync(join(ROOT, f), join(dir, f));
  }
}
const run = dir => spawnSync(process.execPath, [join(dir, FILES[3])], { encoding: "utf8" });

let bad = 0;
const control = mkdtempSync(join(tmpdir(), "xs21-control-"));
copyTree(control);
const c = run(control);
console.log(`${c.status === 0 ? "ok  " : "FAIL"}  unmutated copy passes`);
if (c.status !== 0) bad++;
rmSync(control, { recursive: true });

for (const [name, file, from, to] of MUTANTS) {
  const dir = mkdtempSync(join(tmpdir(), "xs21-mutant-"));
  copyTree(dir);
  const p = join(dir, file);
  if (from === null) {
    const b = readFileSync(p);
    writeFileSync(p, b.subarray(0, b.length - 300));
  } else {
    const s = readFileSync(p, "utf8");
    const n = s.split(from).length - 1;
    if (n !== 1) { console.log(`FAIL  ${name}: mutation site found ${n} times, not once`); bad++; continue; }
    writeFileSync(p, s.replace(from, to));
  }
  const r = run(dir);
  const caught = r.status !== 0;
  const first = (r.stdout.match(/^  FAIL .*$/m) || [""])[0].trim();
  console.log(`${caught ? "ok  " : "FAIL"}  ${name}${caught ? "  <- " + first : "  NOT CAUGHT"}`);
  if (!caught) bad++;
  rmSync(dir, { recursive: true });
}
console.log(`\n${MUTANTS.length} mutants; ${bad ? bad + " problem(s): FAIL" : "all caught, control passes"}`);
if (bad) process.exit(1);

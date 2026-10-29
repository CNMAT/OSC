/* Grades test-xiaosamd21-contract.mjs by mutation: each mutant breaks one
   thing the test claims to guard, in a scratch copy of the files it reads, and
   the test must fail on every one. The unmutated copy must pass, so a failure
   below is the mutant's and not the copy's. A mutant may make further edits
   (its fifth element, [file, from, to] each), for a break that has to be made
   in two files at once to get past the check that holds them equal.

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
               "extras/webserial/test/xiaosamd21-2026-10-29.slip",
               "examples/XiaoSamd21Oscuino/WiiExtension.h"];
const INO = FILES[1], HTML = FILES[2], SLIP = FILES[4], WII = FILES[5];

const MUTANTS = [
  // 1. the libraries
  ["page LIBS renames a library", HTML,
   'const LIBS = ["FreeTouch", "FastTouch", "CapacitiveSensor"];',
   'const LIBS = ["FreeTouch", "Fasttouch", "CapacitiveSensor"];'],
  ["ADDRESSES.md stops naming FastTouch", "ADDRESSES.md", "`FastTouch` (the pin's", "FastTouch (the pin's"],
  // 2. the parameters
  ["page PARAMS loses hop", HTML, '"freq", "hop", "compcap"', '"freq", "compcap"'],
  ["sketch renames intcap", INO, '"/cap/intcap"', '"/cap/intcapx"'],
  ["sketch gives timeout to FreeTouch", INO,
   "  LIB_CAPSENSE, LIB_CAPSENSE\n};", "  LIB_CAPSENSE, LIB_FREETOUCH\n};"],
  ["ADDRESSES.md reorders compcap and intcap", "ADDRESSES.md",
   "| `compcap` | FreeTouch | 0..16383 | compensation capacitor code |\n| `intcap` | FreeTouch | 0..63 | integration capacitor code |",
   "| `intcap` | FreeTouch | 0..63 | integration capacitor code |\n| `compcap` | FreeTouch | 0..16383 | compensation capacitor code |"],
  ["sketch header drops the hop row", INO, " *   hop         FreeTouch", " *   (hop)       FreeTouch"],
  ["sketch dispatches /cap by exact match", INO,
   'bundleIN.route("/cap",          routeCap);', 'bundleIN.dispatch("/cap",       askOnly);'],
  // 3. the capture
  ["capture truncated", SLIP, null, null],
  // 4. the defaults
  ["page DEFAULTS compcap drifts", HTML, "compcap: 8192, intcap: 63 }", "compcap: 8000, intcap: 63 }"],
  ["page DEFAULTS samples drifts", HTML, "{ samples: 30, timeout: 2000 }", "{ samples: 20, timeout: 2000 }"],
  // 5. the drawing
  ["page loses D9's fader", HTML, '["D9",    9, "R", 332.5, 385.0, 3]', '["D9",    9, "R", 332.5, 385.0, null]'],
  // 6. outbound bytes
  ["page encodes ints as floats", HTML, '"s" : "i").join("")', '"s" : "f").join("")'],
  ["page tags strings as ints", HTML, 'typeof v === "string" ? "s" : "i"', 'typeof v === "string" ? "i" : "i"'],
  // 7. what the page asks
  ["page asks /cap/pins after a hello", HTML,
   '    send("/cap/config", []);\n    send("/i2c", []);',
   '    send("/cap/pins", []);\n    send("/i2c", []);'],
  ["page asks again on every /cap/lib", HTML,
   "  foreignLib = null;\n  setLib(name);\n}", '  foreignLib = null;\n  setLib(name);\n  if (writer) send("/cap/config", []);\n}'],
  ["page takes an unknown library for the one showing", HTML,
   "  if (!LIBS.includes(name)) {\n    if (name !== foreignLib)", "  if (false) {\n    if (name !== foreignLib)"],
  ["page draws a /cap of any length", HTML,
   "  if (!pins || pins.length !== values.length) {", "  if (!pins) {"],
  ["page holds a port that would not open", HTML,
   "  const chosen = await navigator.serial.requestPort(opts);",
   "  const chosen = port = await navigator.serial.requestPort(opts);"],
  ["page lets go on any port's disconnect", HTML,
   "if (port && e.target === port) disconnect();", "if (port) disconnect();"],
  ["page forgets to ask what is plugged in when the bus comes on", HTML,
   '    send("/i2c/scan", []);\n    send("/wii/id", []);', '    send("/i2c/scan", []);'],
  // 8. the Wii binding
  ["header reads a Classic button through the guitar's view", WII,
   "WII_GET(wiiClassic, buttonPlus, 0, 1),", "WII_GET(wiiGuitar, buttonPlus, 0, 1),"],
  ["page WII_KINDS loses the Drawsome", HTML,
   '  { name: "Drawsome", lib: "DrawsomeTablet", fields: [\n      ["penX", 0, 65535], ["penY", 0, 65535], ["penPressure", 0, 4095], ["penDetected", 0, 1],\n  ] },\n', ""],
  ["ADDRESSES.md renames the guitar's whammyBar", "ADDRESSES.md", "`whammyBar` 0..31", "`whammy` 0..31"],
  ["page's guitar drawing reads a field the kind lacks", HTML,
   'f.strumUp ? "translate(0,-6)"', 'f.strumup ? "translate(0,-6)"'],
  ["header, page and ADDRESSES.md all swap the Nunchuk's joyX and joyY", WII,
   '  WII_GET(wiiNunchuk, joyX, 0, 255),       // "8 bits, 0-255"\n  WII_GET(wiiNunchuk, joyY, 0, 255),',
   '  WII_GET(wiiNunchuk, joyY, 0, 255),       // "8 bits, 0-255"\n  WII_GET(wiiNunchuk, joyX, 0, 255),',
   [[HTML, '["joyX", 0, 255], ["joyY", 0, 255], ["accelX"', '["joyY", 0, 255], ["joyX", 0, 255], ["accelX"'],
    ["ADDRESSES.md", "`joyX` 0..255, `joyY` 0..255, `accelX`", "`joyY` 0..255, `joyX` 0..255, `accelX`"]]],
  ["header, page and ADDRESSES.md all drop the Classic's buttonSelect", WII,
   "  WII_GET(wiiClassic, buttonSelect, 0, 1),\n", "",
   [[HTML, '["buttonSelect", 0, 1], ', ""], ["ADDRESSES.md", "`buttonSelect` 0..1, ", ""]]],
  ["header, page and ADDRESSES.md all give the Nunchuk's accelX 0..255", WII,
   "WII_GET(wiiNunchuk, accelX, 0, 1023),", "WII_GET(wiiNunchuk, accelX, 0, 255),",
   [[HTML, '["accelX", 0, 1023]', '["accelX", 0, 255]'], ["ADDRESSES.md", "`accelX` 0..1023", "`accelX` 0..255"]]],
  ["header, page and ADDRESSES.md all send rollAngle as an int", WII,
   "WII_GETF(wiiNunchuk, rollAngle, -180, 180),", "WII_GET(wiiNunchuk, rollAngle, -180, 180),",
   [[HTML, '["rollAngle", -180, 180, "f"]', '["rollAngle", -180, 180]'],
    ["ADDRESSES.md", "`rollAngle` -180..180 float", "`rollAngle` -180..180"]]],
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

for (const [name, file, from, to, more = []] of MUTANTS) {
  const dir = mkdtempSync(join(tmpdir(), "xs21-mutant-"));
  copyTree(dir);
  let missing = null;
  for (const [f, a, b] of [[file, from, to], ...more]) {
    const p = join(dir, f);
    if (a === null) {
      const buf = readFileSync(p);
      writeFileSync(p, buf.subarray(0, Math.floor(buf.length / 2)));
      continue;
    }
    const s = readFileSync(p, "utf8");
    const n = s.split(a).length - 1;
    if (n !== 1) { missing = `mutation site in ${f} found ${n} times, not once`; break; }
    writeFileSync(p, s.replace(a, b));
  }
  if (missing) { console.log(`FAIL  ${name}: ${missing}`); bad++; rmSync(dir, { recursive: true }); continue; }
  const r = run(dir);
  const caught = r.status !== 0;
  const first = (r.stdout.match(/^  FAIL .*$/m) || [""])[0].trim();
  console.log(`${caught ? "ok  " : "FAIL"}  ${name}${caught ? "  <- " + first : "  NOT CAUGHT"}`);
  if (!caught) bad++;
  rmSync(dir, { recursive: true });
}
console.log(`\n${MUTANTS.length} mutants; ${bad ? bad + " problem(s): FAIL" : "all caught, control passes"}`);
if (bad) process.exit(1);

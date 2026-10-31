/* Grades test-kb2040-contract.mjs by mutation: each mutant breaks one thing
   the test claims to guard, in a scratch copy of the files it reads, and the
   test must fail a check on every one: a crash fails none, so a copy whose
   test throws before checking anything must not count as caught. The
   unmutated copy must pass, so a failure below is the mutant's and not the
   copy's. A mutant may make further edits
   (its fifth element, [file, from, to] each), for a break that has to be made
   in two places at once to get past the check that holds them equal.

   Two checks need something from this machine, and their mutants are skipped,
   and said to be, without it: arduino-pico installed (RP2040_CORE, or the
   Arduino folders' own), and the two board files named by KB2040_BRD and
   ADAPTER_BRD, which the test passes to brdgeom.py.

   Run: node test/mutants-kb2040.mjs   No dependencies. */

import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, copyFileSync, rmSync, existsSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { tmpdir, homedir } from "node:os";
import { join } from "node:path";

const ROOT = new URL("../../../", import.meta.url).pathname;
const FILES = ["ADDRESSES.md",
               "examples/KB2040Oscuino/KB2040Oscuino.ino",
               "examples/KB2040Oscuino/KB2040Oscuino.html",
               "extras/webserial/test/test-kb2040-contract.mjs",
               "extras/webserial/test/kb2040-boards.json",
               "examples/KB2040Oscuino/WiiExtension.h",
               "examples/KB2040Oscuino/brdgeom.py",
               "examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino",
               "examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.html",
               "examples/XiaoSamd21Oscuino/WiiExtension.h",
               "examples/KB2040Oscuino/CREDITS.md"];
const [DOC, INO, HTML, , BOARDS, WII, TOOL, XINO, XHTML, , CREDITS] = FILES;

const CORE = [process.env.RP2040_CORE,
              join(homedir(), "Library", "Arduino15", "packages", "rp2040", "hardware", "rp2040"),
              join(homedir(), ".arduino15", "packages", "rp2040", "hardware", "rp2040")].some(d => d && existsSync(d))
  ? null : "no arduino-pico installed";
const BRD = process.env.KB2040_BRD && process.env.ADAPTER_BRD ? null : "KB2040_BRD and ADAPTER_BRD not set";

const MUTANTS = [
  // 1. WiiExtension.h
  ["the KB2040's WiiExtension.h drifts", WII,
   "WII_GET(wiiClassic, buttonPlus, 0, 1),", "WII_GET(wiiClassic, buttonPlus, 0, 2),"],
  // 2. the sketch's shared functions
  ["the KB2040's routeWii() probes with the bus off", INO,
   "  if (i2cOn && !wiiPresent) changed = wiiProbe();", "  if (!wiiPresent) changed = wiiProbe();"],
  ["XiaoSamd21Oscuino's argInt() truncates and the KB2040's does not", XINO,
   "  if (m.isFloat(i)) { v = lroundf(m.getFloat(i)); return true; }",
   "  if (m.isFloat(i)) { v = (int32_t) m.getFloat(i); return true; }"],
  ["the header stops naming wiiRead()", INO,
   " * sketch's file, byte for byte, and wiiForget(), wiiProbe(), wiiRead(),",
   " * sketch's file, byte for byte, and wiiForget(), wiiProbe(),"],
  // 3. the page's shared blocks
  ["the page's codec tags ints as floats", HTML, '"s" : "i").join("")', '"s" : "f").join("")'],
  ["the page's readLoop() swallows a read error", HTML,
   '    logLine("warn", "read error: " + e.message);\n', ""],
  ["the page's send() pads every frame", HTML,
   "  if (frame.length % USB_ENDPOINT !== 0) return frame;", "  if (frame.length % USB_ENDPOINT === 1) return frame;"],
  ["the page's engrave() spaces its letters wider", HTML, '"letter-spacing": ".04em"', '"letter-spacing": ".05em"'],
  ["the page's WII_KINDS lose the Drawsome", HTML,
   '  { name: "Drawsome", lib: "DrawsomeTablet", fields: [\n      ["penX", 0, 65535], ["penY", 0, 65535], ["penPressure", 0, 4095], ["penDetected", 0, 1],\n  ] },\n', ""],
  ["the page's readout wraps at 50 characters", HTML, "> 44) { lines.push(line)", "> 50) { lines.push(line)"],
  ["WII_ART's head drifts", HTML, "const WII_ART = {};\n", "const WII_ART = Object.create(null);\n"],
  ["the page's Nunchuk rolls the other way", HTML, "const NUNCHUK_ROLL_SIGN = 1;", "const NUNCHUK_ROLL_SIGN = -1;"],
  // 4. one set of addresses
  ["the page asks /wii/ident for the identity", HTML,
   '$("wiiId").onclick = () => send("/wii/id", []);', '$("wiiId").onclick = () => send("/wii/ident", []);'],
  ["the sketch routes /i2c/scans where its header says /i2c/scan", INO,
   'if (m.fullMatch("/scan", off)) addScan();', 'if (m.fullMatch("/scans", off)) addScan();'],
  ["the page no longer handles /rate by name", HTML, '    } else if (a === "/rate") {', '    } else if (a === "/rates") {'],
  ["ADDRESSES.md stops naming KB2040Oscuino", DOC,
   "`XiaoSamd21Oscuino` and `KB2040Oscuino`, NintendoExtensionCtrl", "`XiaoSamd21Oscuino` and KB2040Oscuino, NintendoExtensionCtrl",
   [[DOC, "On `KB2040Oscuino` the bus", "On KB2040Oscuino the bus"]]],
  ["ADDRESSES.md wants two ints after every /i2c/pull", DOC,
   "sends `/i2c/pull` with nothing after the address", "sends `/i2c/pull` with its two ints"],
  // 5. the board the page asks for
  ["the page asks for another PID", HTML, "usbProductId: 0x8105", "usbProductId: 0x8106"],
  ["the page and the header both name a PID the core does not build", INO, "VID 0x239A PID 0x8105", "VID 0x239A PID 0x8106",
   [[HTML, "usbProductId: 0x8105", "usbProductId: 0x8106"]], CORE],
  // 6. what the page asks
  ["a hello asks for the scan as well", HTML,
   '  if (writer) send("/i2c", []);\n}', '  if (writer) { send("/i2c", []); send("/i2c/scan", []); }\n}'],
  ["/i2c 1 forgets the identity", HTML,
   '    send("/i2c/scan", []);\n    send("/wii/id", []);\n  }', '    send("/i2c/scan", []);\n  }'],
  ["/i2c 0 asks again", HTML, "  if (busOn && writer) {", "  if (writer) {"],
  ["a bare /i2c/pull is logged", HTML,
   '    else if (a === "/i2c/pull") continue;', '    else if (a === "/i2c/pull") { logLine("", a); continue; }'],
  ["both pages throw on a /wii before /wii/id", HTML,
   'const names = kind ? kind.fields.map(f => f[0]) : args.map((_, i) => "b" + i);',
   "const names = kind.fields.map(f => f[0]);",
   [[XHTML, 'const names = kind ? kind.fields.map(f => f[0]) : args.map((_, i) => "b" + i);',
     "const names = kind.fields.map(f => f[0]);"]]],
  ["connecting sends /rate first", HTML,
   '  send("/enq", []);\n  send("/rate", [+$("rate").value]);', '  send("/rate", [+$("rate").value]);\n  send("/enq", []);'],
  ["the grid shows the last scan with the bus off", HTML,
   "const found = new Set(busOn ? bus.scan || [] : []);", "const found = new Set(bus.scan || []);"],
  ["the grid loses 0x77's lamp", HTML, "const reserved = a < 0x08 || a > 0x77;", "const reserved = a < 0x08 || a > 0x76;"],
  // 7. the cable
  ["the cable does not turn over", HTML, "const o = off * Math.cos(Math.PI * t);", "const o = off;"],
  ["the KB2040's plug takes the wires upside down", HTML,
   "const kbPads = KB.qtPads.slice().reverse();", "const kbPads = KB.qtPads.slice();"],
  ["the adapter drawn face down", HTML, "AD_AT.y + (x - AD.x0) * MM]", "AD_AT.y - (x - AD.x0) * MM]"],
  ["the KB2040 drawn face down", HTML, "KB_AT.y + (KB.h - y) * MM]", "KB_AT.y + y * MM]"],
  // 8. the board files
  ["KB2040: the RP2040's box drifts", HTML, "rp2040: [15.87, 5.68, 22.87, 12.68,", "rp2040: [15.97, 5.68, 22.87, 12.68,"],
  ["KB2040: D1's box is read from tDocu", HTML, '"D1 · NSR0320", 21],', '"D1 · NSR0320"],'],
  ["KB2040: the castellations' pitch drifts", HTML, "pitch: 2.54,", "pitch: 2.55,"],
  ["KB2040: two castellated pins swap nets", HTML, '"A1", "A0", "SCLK"', '"A0", "A1", "SCLK"'],
  ["KB2040: the long pads' holes sit on the edge", HTML, "out: 0.254,", "out: 0,"],
  ["KB2040: CONN1's pads drawn 0.6 tall", HTML, "qtPadSize: [1.55, 0.5]", "qtPadSize: [1.55, 0.6]"],
  ["KB2040: a third fiducial would show", BOARDS, '"U$34": {"package": "FIDUCIAL_1MM", "value": "FIDUCIAL_1MM", "at": [30.797, 13.64]',
   '"U$34": {"package": "FIDUCIAL_1MM", "value": "FIDUCIAL_1MM", "at": [20.0, 2.0]'],
  ["adapter: a corner rounds the other way", HTML, "[12.7, -10.033, 0], [12.7, 2.667, 90]", "[12.7, -10.033, 0], [12.7, 2.667, -90]"],
  ["adapter: the outline's extent drifts", HTML, "x0: -12.7, x1: 12.7, y0: -12.573, y1: 5.207,", "x0: -12.7, x1: 12.7, y0: -12.573, y1: 5.307,"],
  ["adapter: the tongue's contacts swap nets", HTML,
   '"GND", "1"], [[-2.65, -3.8, -1.35, 3.8], "SCL_3V", "3"]', '"SCL_3V", "1"], [[-2.65, -3.8, -1.35, 3.8], "GND", "3"]'],
  ["adapter: CONN4's SDA and SCL swap", HTML, '[-4.183, "SDA"], [-5.183, "SCL"]] },\n    far:', '[-4.183, "SCL"], [-5.183, "SDA"]] },\n    far:'],
  ["adapter: the header's 3V is named VIN", HTML, '[-2.794, "3.3V", "3V"]', '[-2.794, "3.3V", "VIN"]'],
  ["adapter: the holes' rings drawn radius 2.2", HTML, "ring: 2, line: 0.2032", "ring: 2.2, line: 0.2032"],
  ["adapter: a fiducial moves", HTML, "fiducials: [[-7.303, 4.064], [7.303, 4.128]]", "fiducials: [[-7.303, 4.064], [7.303, 4.064]]"],
  ["adapter: Notch Up is not turned", HTML, '[0, 0, "Notch Up", true, true]', '[0, 0, "Notch Up", true, false]'],
  ["adapter: the silkscreen drawn 1.2 high", HTML, "silkSize: 1.016,", "silkSize: 1.2,"],
  ["adapter: Q2's box drifts", HTML, "fet: [1.49, -7.86, 3.59, -5.86,", "fet: [1.49, -7.86, 3.59, -5.76,"],
  ["kb2040-boards.json drifts from the files where the page does not look", BOARDS,
   '"package": "SOLDERJUMPER_ARROW_NOPASTE"', '"package": "SOLDERJUMPER_ARROW"', [], BRD],
  ["brdgeom.py rounds to two places", TOOL, "    return round(v + 0.0, 3)", "    return round(v + 0.0, 2)", [], BRD],
  ["brdgeom.py leaves a bottom part's smd pads on the top", TOOL,
   'copper = {"1": "16", "16": "1"} if', 'copper = {"1": "1", "16": "16"} if', [], BRD],
  // 9. the board files' terms
  ["CREDITS.md cites another commit of the adapter's files", CREDITS,
   "commit d3f2e25c4f9211553e32b36ffce03212f414ead3", "commit d3f2e25c4f9211553e32b36ffce03212f414ead4"],
  ["the page's Sources cite another commit of the KB2040's files", HTML,
   "Adafruit-KB2040-PCB</a> at b4160dc,", "Adafruit-KB2040-PCB</a> at b4160dd,"],
  ["CREDITS.md stops naming kb2040-boards.json", CREDITS,
   "`extras/webserial/test/kb2040-boards.json` holds", "The test fixture holds"],
  ["CREDITS.md stops naming the designer", CREDITS,
   "**Limor Fried/Ladyada for Adafruit Industries**", "**Adafruit Industries**"],
  ["the page's credit line drops the licence's version", HTML,
   "board files, CC BY-SA 3.0</text>", "board files, CC BY-SA</text>"],
  ["the page stops pointing at CREDITS.md", HTML, "see CREDITS.md beside this page", "see the board files"],
];

function copyTree(dir) {
  for (const f of FILES) {
    mkdirSync(join(dir, f, ".."), { recursive: true });
    copyFileSync(join(ROOT, f), join(dir, f));
  }
}
const run = dir => spawnSync(process.execPath, [join(dir, FILES[3])], { encoding: "utf8" });
// Caught: the test exits non-zero having failed a check. A crash fails none.
const verdict = r => {
  const first = (r.stdout.match(/^  FAIL .*$/m) || [""])[0].trim();
  return { caught: r.status !== 0 && first !== "", first };
};

let bad = 0, skipped = 0;
const control = mkdtempSync(join(tmpdir(), "kb2040-control-"));
copyTree(control);
const c = run(control);
console.log(`${c.status === 0 ? "ok  " : "FAIL"}  unmutated copy passes${c.status === 0 ? "" : "\n" + c.stdout}`);
if (c.status !== 0) bad++;
rmSync(control, { recursive: true });

// A copy whose test throws before it checks anything: a crash, which must not
// count as a catch.
const crash = mkdtempSync(join(tmpdir(), "kb2040-crash-"));
copyTree(crash);
writeFileSync(join(crash, FILES[3]), 'throw new Error("a crash, not a check");\n' + readFileSync(join(crash, FILES[3]), "utf8"));
const k = run(crash), kOk = k.status !== 0 && !verdict(k).caught;
console.log(`${kOk ? "ok  " : "FAIL"}  a copy whose test crashes (exit ${k.status}) is not counted as caught`);
if (!kOk) bad++;
rmSync(crash, { recursive: true });

for (const [name, file, from, to, more = [], needs = null] of MUTANTS) {
  if (needs) { console.log(`skip  ${name}: ${needs}`); skipped++; continue; }
  const dir = mkdtempSync(join(tmpdir(), "kb2040-mutant-"));
  copyTree(dir);
  let missing = null;
  for (const [f, a, b] of [[file, from, to], ...more]) {
    const p = join(dir, f);
    const s = readFileSync(p, "utf8");
    const n = s.split(a).length - 1;
    if (n !== 1) { missing = `mutation site in ${f} found ${n} times, not once`; break; }
    writeFileSync(p, s.replace(a, b));
  }
  if (missing) { console.log(`FAIL  ${name}: ${missing}`); bad++; rmSync(dir, { recursive: true }); continue; }
  const r = run(dir), { caught, first } = verdict(r);
  console.log(`${caught ? "ok  " : "FAIL"}  ${name}` +
              (caught ? "  <- " + first : r.status !== 0 ? "  CRASHED: no check failed" : "  NOT CAUGHT"));
  if (!caught) bad++;
  rmSync(dir, { recursive: true });
}
console.log(`\n${MUTANTS.length} mutants, ${skipped} skipped; ` +
            `${bad ? bad + " problem(s): FAIL" : "every one run was caught, control passes"}`);
if (bad) process.exit(1);

/* The hand-written KB2040Oscuino pair, held to XiaoSamd21Oscuino's Wii work,
   to ADDRESSES.md and to the two boards' own files.

   KB2040Oscuino is XiaoSamd21Oscuino's I2C mode on a board of its own: the
   same WiiExtension.h, the same Wii functions in the sketch, the same codec,
   Wii lists and Wii drawings in the page. Nothing generates either copy, so a
   fix made in one and not the other is silent, as is a page that drifts from
   the board files it says it was drawn from. `make check` cannot see this
   pair, because it is not generated from boards.json, so this checks, with no
   board and no browser:

     1. WiiExtension.h beside each sketch is the same file, byte for byte
     2. each function the KB2040 sketch's header names as XiaoSamd21Oscuino's
        is that sketch's, line for line
     3. the page's codec, read loop, send, drawing helpers, Wii lists and Wii
        drawings are XiaoSamd21Oscuino's page's, character for character
     4. one set of addresses: the sketch's header lists what it routes; every
        address the page sends, the sketch routes; every address the sketch
        sends, the page handles by name, but for the two it only logs;
        ADDRESSES.md names the sketch in its wii section and allows the bare
        /i2c/pull it sends
     5. the page's USB filter is the VID and PID the sketch's header gives,
        and those are arduino-pico's build ids for adafruit_kb2040 wherever
        that core is installed (RP2040_CORE, or the Arduino folders' own)
     6. what the page asks the board, run against a stub document: connecting
        sends /enq then /rate; a hello asks for /i2c alone; /i2c 1 asks for
        the scan and the identity, /i2c 0 for nothing; a bare /i2c/pull asks
        for nothing and is not logged; a /wii before any /wii/id does not
        throw; and, against a document that keeps what is drawn, the grid has
        a lamp for each address a scan can name, and a scan lights the ones
        it names while the bus is on and none once it is off
     7. the STEMMA QT cable as drawn: each wire leaves the KB2040's plug level
        with its own pad and comes into the adapter's plug in line with the
        pad of the same number, so the cable turns over between them; and
        both boards are drawn face up, turned but not mirrored
     8. the drawing's numbers are the board files': kb2040-boards.json, what
        examples/KB2040Oscuino/brdgeom.py reads from the two files, gives the
        outlines, the part boxes, the pads with their nets, sizes and holes,
        the mounting holes with their rings, the fiducials and the silkscreen
        texts the page draws, each within 0.006 mm (the page's two decimals
        on the tool's three); where KB2040_BRD and ADAPTER_BRD name the two
        files, the tool is run on them and must give kb2040-boards.json,
        SHA-256 and all
     9. the board files' terms: CREDITS.md beside the sketch names what here
        is theirs (the page's KB and AD, kb2040-boards.json), CC BY-SA 3.0
        and its URL, both repositories at the one commit each that the
        page's Sources and brdgeom.py cite, and the designer the page
        credits; the page credits the boards CC BY-SA 3.0 and points at
        CREDITS.md

   Not checked here: the fiducials' clearing in the mask (from the package's
   tStop, which brdgeom.py does not read), the colours, and what the page says
   is drawn by eye. The transport (a port held only once open, let go only on
   its own disconnect, a close that fails reported) is test-serial-close.mjs's.

   Graded by test/mutants-kb2040.mjs: one mutant per claim above, and each
   must fail this test.

   Run: node test/test-kb2040-contract.mjs   (or `make test`). No dependencies. */

import { readFileSync, readdirSync, existsSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { homedir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = new URL("../../../", import.meta.url);
const read = p => readFileSync(new URL(p, ROOT), "utf8");
const KB_DIR = "examples/KB2040Oscuino/", XIAO_DIR = "examples/XiaoSamd21Oscuino/";
const ino = read(KB_DIR + "KB2040Oscuino.ino"), xIno = read(XIAO_DIR + "XiaoSamd21Oscuino.ino");
const html = read(KB_DIR + "KB2040Oscuino.html"), xHtml = read(XIAO_DIR + "XiaoSamd21Oscuino.html");
const contract = read("ADDRESSES.md");
const BOARDS = new URL("kb2040-boards.json", import.meta.url);
const boards = JSON.parse(readFileSync(BOARDS, "utf8"));

let pass = 0, fail = 0, skip = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};
const eq = (label, a, b) => ok(label, JSON.stringify(a) === JSON.stringify(b),
                               `${JSON.stringify(a)}\n       != ${JSON.stringify(b)}`);

const scriptOf = h => h.split(/<script>/)[1].split(/<\/script>/)[0];
const script = scriptOf(html), xScript = scriptOf(xHtml);
const SEP = "/* ============================================================\n";
const page = new Function(script.split(SEP + "   3. Web Serial")[0] +
  "return { decodePacket, messagesOf, SlipDecoder, encodeMessage };")();

/* ---------- 1. WiiExtension.h */
console.log("\nWiiExtension.h");
const wiiH = readFileSync(new URL(KB_DIR + "WiiExtension.h", ROOT));
const xWiiH = readFileSync(new URL(XIAO_DIR + "WiiExtension.h", ROOT));
ok("the KB2040's WiiExtension.h is XiaoSamd21Oscuino's, byte for byte",
   wiiH.length > 1000 && wiiH.equals(xWiiH), `${wiiH.length} and ${xWiiH.length} bytes`);

/* ---------- 2. the sketch's functions that are XiaoSamd21Oscuino's */
console.log("\nThe sketch's functions shared with XiaoSamd21Oscuino");
// A C++ function: its signature line through the "}" that closes it in
// column 0. A JS one likewise.
const cFn = (src, name) => (src.match(new RegExp(
  `^(?:static )?[\\w:<>*& ]+?\\b${name}\\([^)]*\\) \\{\\n[\\s\\S]*?\\n\\}\\n`, "m")) || [""])[0];
const jsFn = (src, name) => (src.match(new RegExp(
  `^(?:async )?function ${name}\\([^)]*\\) \\{\\n[\\s\\S]*?\\n\\}\\n`, "m")) || [""])[0];
const header = ino.slice(0, ino.indexOf("*/")), code = ino.slice(ino.indexOf("*/"));
const prose = header.replace(/\n \* ?/g, " ");
const sharedSentence = (prose.match(/WiiExtension\.h is that sketch's file, byte for byte, and ([^.]*)\./) || [])[1] || "";
const shared = [...sharedSentence.matchAll(/(\w+)\(\)/g)].map(m => m[1]);
ok(`the header names ${shared.length} of them`, shared.length === 15, shared.join(" "));
for (const name of shared) {
  const a = cFn(code, name), b = cFn(xIno, name);
  ok(`${name}() is XiaoSamd21Oscuino's, line for line`, a.length > 50 && a === b,
     `${a.length} and ${b.length} characters`);
}

/* ---------- 3. the page's blocks that are XiaoSamd21Oscuino's page's */
console.log("\nThe page's blocks shared with XiaoSamd21Oscuino's page");
// From `start` through the "}" or "};" in column 0 that closes the first
// function or object begun at `opener`, at or after `start`.
function toClose(src, start, opener) {
  const i = src.indexOf(start), j = i < 0 ? -1 : src.indexOf(opener, i);
  if (j < 0) return "";
  const ends = ["\n}\n", "\n};\n"].map(e => [src.indexOf(e, j), e]).filter(([k]) => k >= 0).sort((a, b) => a[0] - b[0]);
  return ends.length ? src.slice(i, ends[0][0] + ends[0][1].length) : "";
}
const between = (src, start, stop) => {
  const i = src.indexOf(start), j = i < 0 ? -1 : src.indexOf(stop, i);
  return j < 0 ? "" : src.slice(i, j);
};
const through = (src, start, last) => {
  const i = src.indexOf(start), j = i < 0 ? -1 : src.indexOf(last, i);
  return j < 0 ? "" : src.slice(i, j + last.length);
};
const BLOCKS = {
  "the OSC codec and SLIP": s => between(s, SEP + "   1. OSC", SEP + "   3. Web Serial"),
  "readLoop()": s => toClose(s, "async function readLoop() {", "async function readLoop() {"),
  "send() and its guard against a zero-length packet":
    s => toClose(s, "// One extra END keeps a write off an exact multiple of the 64-byte USB", "async function send("),
  "the drawing helpers, $() to engrave()": s => toClose(s, "const $ = id => document.getElementById(id);", "function engrave("),
  "WII_KINDS": s => through(s, "const WII_KINDS = [", "const wiiRange = f => [f[1], f[2]];\n"),
  "drawWiiSlot() to markShelf()": s => toClose(s, "// The connected extension, drawn by its kind's art", "function markShelf() {"),
  "WII_ART's head": s => through(s, "/* ---- the extensions' own drawings", "const WII_ART = {};\n"),
  "the extensions' drawings, wiiKey() to WII_ART.Unknown":
    s => toClose(s, "// A press button: shape drawn at rest, a lit copy shown while pressed.", "WII_ART.Unknown = slot => {"),
};
for (const [name, cut] of Object.entries(BLOCKS)) {
  const a = cut(script), b = cut(xScript);
  ok(`${name}: XiaoSamd21Oscuino's page's, character for character`, a.length > 100 && a === b,
     `${a.length} and ${b.length} characters`);
}

/* ---------- 4. one set of addresses */
console.log("\nOne set of addresses");
const sent = new Set([...code.matchAll(/bundleOUT\.add\("(\/[\w/]*)"\)/g)].map(m => m[1]));
// What the sketch routes: each dispatch()ed address; and each route()d
// prefix with the sub-addresses its handler fullMatch()es, and the address
// itself where the handler matches it whole.
const routed = new Set();
for (const [, a] of code.matchAll(/bundleIN\.dispatch\("(\/[\w/]+)",\s*\w+\)/g)) routed.add(a);
for (const [, prefix, fn] of code.matchAll(/bundleIN\.route\("(\/[\w/]+)",\s*(\w+)\)/g)) {
  const body = cFn(code, fn);
  for (const [, s] of body.matchAll(/fullMatch\("(\/[\w/]+)", off\)/g)) routed.add(prefix + s);
  for (const [, whole] of body.matchAll(/fullMatch\("(\/[\w/]+)"\)/g)) routed.add(whole);
}
const listed = new Set([...header.matchAll(/^ \*   (\/[\w/]+)/gm)].map(m => m[1]));
ok(`the sketch routes ${routed.size} addresses and sends ${sent.size}`, routed.size >= 10 && sent.size >= 12,
   `${[...routed].join(" ")}; ${[...sent].join(" ")}`);
eq("the sketch's header lists the addresses it routes", [...listed].sort(), [...routed].sort());
const pageSends = new Set([...script.matchAll(/send\("(\/[\w/]*)"/g)].map(m => m[1]));
const unrouted = [...pageSends].filter(a => !routed.has(a));
ok(`every address the page sends (${[...pageSends].join(" ")}) the sketch routes`,
   pageSends.size >= 5 && !unrouted.length, unrouted.join(" "));
const handlers = jsFn(script, "onFrame") + jsFn(script, "onHello");
const named = new Set([...handlers.matchAll(/"(\/[\w/]*)"/g)].map(m => m[1]));
const prefixes = [...handlers.matchAll(/startsWith\("(\/[\w/]*\/)"\)/g)].map(m => m[1]);
const handled = a => named.has(a) || prefixes.some(p => a.startsWith(p));
eq("every address the sketch sends the page handles by name, but /s/m and /wii/raw, which it logs",
   [...sent].filter(a => !handled(a)).sort(), ["/s/m", "/wii/raw"]);
const wiiAt = contract.indexOf("### wii fields");
const wiiBlock = wiiAt < 0 ? "" : contract.slice(wiiAt, contract.indexOf("\n## ", wiiAt));
ok("ADDRESSES.md's wii fields name KB2040Oscuino", wiiBlock.includes("`KB2040Oscuino`"));
const i2cRow = (contract.match(/^\| \*\*i2c\*\* .*$/m) || [""])[0];
ok("ADDRESSES.md's i2c row allows a /i2c/pull with nothing after the address",
   i2cRow.includes("`/i2c/pull` with nothing after the address"), i2cRow.slice(0, 60));

/* ---------- 5. the board the page asks for */
console.log("\nThe board the page asks for");
const filter = script.match(/const KB2040_FILTERS = \[\{ usbVendorId: (0x[0-9A-Fa-f]+), usbProductId: (0x[0-9A-Fa-f]+) \}\];/);
const ids = header.match(/VID (0x[0-9A-Fa-f]+) PID (0x[0-9A-Fa-f]+)/);
ok("the page's USB filter is the VID and PID the sketch's header gives",
   !!filter && !!ids && +filter[1] === +ids[1] && +filter[2] === +ids[2],
   `${filter && filter.slice(1)} and ${ids && ids.slice(1)}`);
const coreDirs = [process.env.RP2040_CORE,
                  join(homedir(), "Library", "Arduino15", "packages", "rp2040", "hardware", "rp2040"),
                  join(homedir(), ".arduino15", "packages", "rp2040", "hardware", "rp2040")]
  .filter(d => d && existsSync(d));
const boardsTxts = coreDirs.flatMap(d => readdirSync(d).map(v => join(d, v, "boards.txt")).filter(f => existsSync(f)));
for (const f of boardsTxts) {
  const t = readFileSync(f, "utf8");
  const vid = (t.match(/^adafruit_kb2040\.build\.usbvid=-DUSBD_VID=(0x[0-9A-Fa-f]+)\r?$/m) || [])[1];
  const pid = (t.match(/^adafruit_kb2040\.build\.usbpid=-DUSBD_PID=(0x[0-9A-Fa-f]+)\r?$/m) || [])[1];
  ok(`... and arduino-pico's build ids for adafruit_kb2040 (${f})`,
     !!vid && !!pid && !!ids && +vid === +ids[1] && +pid === +ids[2], `${vid} ${pid}`);
}
if (!boardsTxts.length) {
  skip++;
  console.log("  skip arduino-pico's build ids: no rp2040 core found (RP2040_CORE names its hardware/rp2040 folder)");
}

/* ---------- 6. what the page asks the board, given what the board says */
// The whole script, UI half included, against a stub document: every element
// is a Proxy that takes any property, call or iteration and does nothing.
// "serial" is not in the stub navigator, so the page runs as if no Web
// Serial; plug() then hands it a writer, and what it writes is what is
// checked.
console.log("\nThe page's answers");
const stub = () => new Proxy(function () {}, {
  get: (_, k) => k === Symbol.iterator ? function* () {}
               : k === Symbol.toPrimitive ? () => ""
               : k === "then" ? undefined : stub(),
  set: () => true,
  apply: () => stub(),
  construct: () => stub(),
});
const writerInto = list => ({ write: async bytes => new page.SlipDecoder().transform(bytes, {
  enqueue: f => list.push(...page.messagesOf(page.decodePacket(f)).map(m => m.address)) }) });
const ui = new Function("document", "navigator", "location", "setInterval",
  script + "\nreturn { onFrame, plug: w => { writer = w; }, spy: f => { logLine = f; } };")(
  stub(), stub(), { search: "" }, () => 0);
const sentByPage = [], logged = [];
ui.plug(writerInto(sentByPage));
ui.spy((cls, text) => logged.push(text));
const feed = (address, args) => ui.onFrame(page.encodeMessage(address, args));
const asked = () => sentByPage.splice(0);   // what the page sent since the last look

feed("/enq", ["KB2040Oscuino"]);
eq("a hello asks for /i2c alone", asked(), ["/i2c"]);
feed("/i2c", [1]);
eq("/i2c 1 asks for the scan and the identity", asked(), ["/i2c/scan", "/wii/id"]);
feed("/i2c", [0]);
eq("/i2c 0 asks for nothing", asked(), []);
logged.length = 0;
feed("/i2c/pull", []);
ok("a bare /i2c/pull asks for nothing and is not logged",
   asked().length === 0 && !logged.some(t => t.includes("/i2c/pull")), logged.join(" | "));
let threw = null;
try { feed("/wii", [1, 2, 3, 4, 5, 6]); } catch (e) { threw = e.message; }
ok("a /wii with no /wii/id yet does not throw", threw === null, threw);

// Connecting, through a fake Web Serial whose port opens and then says nothing.
const onOpen = [];
const quietPort = { open: async () => {}, writable: { getWriter: () => writerInto(onOpen) },
                    readable: { pipeTo: () => new Promise(() => {}) } };
const net = new Function("document", "navigator", "location", "setInterval", script + "\nreturn { connect };")(
  stub(), { serial: { addEventListener: () => {}, requestPort: async () => quietPort } }, { search: "" }, () => 0);
await net.connect();
eq("connecting sends /enq, then /rate", onOpen, ["/enq", "/rate"]);

// Against a document that keeps what is drawn: one element per id, and each
// element made keeps its attributes and children.
const kept = tag => new Proxy({ tag, attrs: {}, kids: [] }, {
  get: (t, k) => k === "setAttribute" ? (a, v) => { t.attrs[a] = String(v); }
               : k === "appendChild" ? c => { t.kids.push(c); return c; }
               : k in t ? t[k] : stub(),
  set: (t, k, v) => { t[k] = v; return true; },
});
const byId = {};
const keeping = new Proxy({}, { get: (_, k) =>
    k === "createElementNS" ? (ns, tag) => kept(tag)
  : k === "getElementById" ? id => (byId[id] = byId[id] || kept("#" + id))
  : stub() });
const drawn = new Function("document", "navigator", "location", "setInterval",
  script + "\nreturn { onFrame, busLeds: () => busLeds, KB, AD, kb, ad, QT_WIRE };")(
  keeping, stub(), { search: "" }, () => 0);
const lit = () => Object.entries(drawn.busLeds()).filter(([, l]) => l.lit.attrs.opacity === "1").map(([a]) => +a);
ok("the grid has a lamp for each of the 112 addresses from 0x08 to 0x77",
   Object.keys(drawn.busLeds()).map(Number).join() === Array.from({ length: 112 }, (_, i) => i + 8).join());
drawn.onFrame(page.encodeMessage("/i2c", [1]));
drawn.onFrame(page.encodeMessage("/i2c/scan", [0x29, 0x52]));
eq("a scan lights the addresses it names while the bus is on", lit(), [0x29, 0x52]);
drawn.onFrame(page.encodeMessage("/i2c", [0]));
eq("... and none once the bus is off", lit(), []);

/* ---------- 7. the cable as drawn */
console.log("\nThe STEMMA QT cable as drawn");
const { KB, AD, kb, ad, QT_WIRE } = drawn;
const everything = [];
(function walk(n) { everything.push(n); (n.kids || []).forEach(walk); })(byId.boards || kept("none"));
const wires = everything.filter(n => n.tag === "g" &&
  n.kids.some(c => c.tag === "title" && / · (yellow|blue|red|black)$/.test(c.textContent || "")));
const nearSock = AD.sockets.near;
eq("four wires, one for each of the KB2040's socket's pads",
   wires.map(w => w.kids.find(c => c.tag === "title").textContent.split(" · ")[0]).sort(),
   KB.qtPads.map(p => p[1]).sort());
for (const w of wires) {
  const net = w.kids.find(c => c.tag === "title").textContent.split(" · ")[0];
  const path = w.kids.find(c => c.tag === "path" && c.attrs.stroke === QT_WIRE[net]);
  const pts = path ? [...path.attrs.d.matchAll(/(-?[\d.]+),(-?[\d.]+)/g)].map(m => [+m[1], +m[2]]) : [];
  const [start, end] = [pts[0] || [NaN, NaN], pts[pts.length - 1] || [NaN, NaN]];
  const k = KB.qtPads.findIndex(p => p[1] === net);
  const kbY = kb(KB.qtPadX, KB.qtPads[k][0])[1], adX = ad(nearSock.x, nearSock.pads[k][0])[0];
  ok(`${net}: leaves the KB2040's plug level with its pad ${k + 1}, off the board's edge`,
     Math.abs(start[1] - kbY) <= 0.06 && start[0] > kb(KB.w, 0)[0], `${start} vs y ${kbY}`);
  ok(`${net}: comes into the adapter's plug in line with ${nearSock.name}'s pad ${k + 1} (${nearSock.pads[k][1]}), off its edge`,
     Math.abs(end[0] - adX) <= 0.06 && end[1] < ad(AD.x0, 0)[1], `${end} vs x ${adX}`);
}
// Face up: Eagle's y runs up and the drawing's down, so a board drawn as it
// lies, or turned, takes Eagle's axes to the drawing's with a negative
// determinant, and a board mirrored, face down, with a positive one.
const det = f => { const [o, i, j] = [f(0, 0), f(1, 0), f(0, 1)];
  return (i[0] - o[0]) * (j[1] - o[1]) - (i[1] - o[1]) * (j[0] - o[0]); };
ok("both boards are drawn face up: kb() and ad() turn Eagle's axes and mirror neither",
   det(kb) < 0 && det(ad) < 0, `determinants ${det(kb)} and ${det(ad)}`);

/* ---------- 8. the drawing's numbers are the board files' */
console.log("\nThe drawing against the board files");
const TOL = 0.006;   // mm: the page's two decimals on the tool's three
const near = (a, b, tol = TOL) => Math.abs(a - b) <= tol;
const nearAll = (a, b) => !!b && a.length === b.length && a.every((v, i) => near(v, b[i]));
const K = boards.kb2040, A = boards.adapter;
ok("kb2040-boards.json holds both boards, each with its file's SHA-256",
   /^[0-9a-f]{64}$/.test(K.sha256) && /^[0-9a-f]{64}$/.test(A.sha256) && K.sha256 !== A.sha256);

// The KB2040
const kx = K.outline.flatMap(w => [w[0], w[2]]), ky = K.outline.flatMap(w => [w[1], w[3]]);
ok("KB2040: the outline is a plain rectangle, KB.w x KB.h from the origin",
   K.outline.length === 4 && K.outline.every(w => w[4] === 0 && (w[0] === w[2] || w[1] === w[3])) &&
   near(Math.min(...kx), 0) && near(Math.min(...ky), 0) && near(Math.max(...kx), KB.w) && near(Math.max(...ky), KB.h),
   JSON.stringify(K.outline));
const C = KB.castel;
for (const [jp, nets, yPad, yEdge] of [["JP1", KB.top, KB.h - C.inset, KB.h + C.out], ["JP2", KB.bottom, C.inset, -C.out]]) {
  const pads = (K.elements[jp] || { pads: [] }).pads;
  const round = pads.filter(p => !p.name.endsWith("C")).sort((a, b) => a.x - b.x);
  const long = pads.filter(p => p.name.endsWith("C")).sort((a, b) => a.x - b.x);
  ok(`KB2040 ${jp}: ${nets.length} round pads, left to right by net, at x0 + k pitch, inset from the edge, dia across, drill`,
     round.length === nets.length && round.every((p, k) => p.net === nets[k] && near(p.x, C.x0 + C.pitch * k) &&
       near(p.y, yPad) && p.diameter === C.dia && p.drill === C.drill && p.shape === "round"),
     round.map(p => `${p.net}@${p.x},${p.y}`).join(" "));
  ok(`KB2040 ${jp}: beside each a long pad, dia wide, its hole edgeDrill across and out past the edge`,
     long.length === nets.length && long.every((p, k) => p.name === round[k].name + "C" && p.net === nets[k] &&
       near(p.x, C.x0 + C.pitch * k) && near(p.y, yEdge) && p.diameter === C.dia && p.drill === C.edgeDrill &&
       p.shape === "long"),
     long.map(p => `${p.name} ${p.net}@${p.x},${p.y}`).join(" "));
}
function partBoxes(board, label, parts) {
  for (const [key, [x0, y0, x1, y1, what, layer = 51]] of Object.entries(parts)) {
    const name = what.split(" · ")[0], e = board.elements[name], box = e && e.boxes[String(layer)];
    ok(`${label} ${key}: ${name}'s box in layer ${layer}`, nearAll([x0, y0, x1, y1], box),
       `${JSON.stringify([x0, y0, x1, y1])} vs ${JSON.stringify(box)}`);
  }
}
partBoxes(K, "KB2040", KB.parts);
const tPlace = Object.values(KB.parts).filter(p => p[5] === 21).map(p => p[4].split(" · ")[0]);
ok(`KB2040: a box is tPlace's (${tPlace.join(" ")}) only where tDocu draws nothing, or only leads 2.4 x 0.4`,
   tPlace.length > 0 && tPlace.every(n => {
     const b = K.elements[n] && K.elements[n].boxes["51"];
     return K.elements[n] && (!b || (near(b[2] - b[0], 2.4) && near(b[3] - b[1], 0.4)));
   }));
const conn1 = (K.elements.CONN1 || { pads: [] }).pads;
ok("KB2040 CONN1: pads 1 to 4 at qtPadX, each qtPadSize, by y and net",
   KB.qtPads.length === 4 && KB.qtPads.every(([y, net], i) => {
     const p = conn1.find(q => q.name === String(i + 1));
     return p && p.net === net && near(p.x, KB.qtPadX) && near(p.y, y) &&
       near(p.box[2] - p.box[0], KB.qtPadSize[0]) && near(p.box[3] - p.box[1], KB.qtPadSize[1]);
   }), JSON.stringify(conn1.slice(0, 4)));
const kbFids = Object.entries(K.elements).filter(([, e]) => e.package === "FIDUCIAL_1MM");
const inBox = ([x, y], [x0, y0, x1, y1]) => x >= x0 && x <= x1 && y >= y0 && y <= y1;
ok(`KB2040: its fiducials (${kbFids.map(([n]) => n).join(" ")}) lie under parts as drawn, so are left out`,
   kbFids.length === 2 && kbFids.every(([, e]) => Object.values(KB.parts).some(p => inBox(e.at, p))));

// The adapter
const pageWires = AD.outline.map(([x, y, c], i) => [x, y, ...AD.outline[(i + 1) % AD.outline.length].slice(0, 2), c]);
const badWire = pageWires.findIndex((w, i) => !nearAll(w, A.outline[i]));
ok(`adapter: the outline, ${A.outline.length} wires in the file's order, each with its curve`,
   pageWires.length === A.outline.length && badWire < 0,
   badWire < 0 ? `${pageWires.length} wires` : `wire ${badWire}: ${pageWires[badWire]} vs ${A.outline[badWire]}`);
const ax = A.outline.flatMap(w => [w[0], w[2]]), ay = A.outline.flatMap(w => [w[1], w[3]]);
ok("adapter: x0..x1 and y0..y1 are the outline's extent",
   near(Math.min(...ax), AD.x0) && near(Math.max(...ax), AD.x1) && near(Math.min(...ay), AD.y0) && near(Math.max(...ay), AD.y1));
const u1 = (A.elements.U1 || { pads: [] }).pads;
ok("adapter: the tongue's contacts are U1's top-side pads, box and net",
   AD.contacts.length === u1.filter(p => p.layer === "1").length && AD.contacts.every(([box, net, pad]) => {
     const p = u1.find(q => q.name === pad);
     return p && p.layer === "1" && p.net === net && nearAll(box, p.box);
   }), JSON.stringify(u1));
for (const s of Object.values(AD.sockets)) {
  const e = A.elements[s.name];
  ok(`adapter ${s.name}: its box, and pads 1 to 4 at x, each socketPad, by y and net`,
     !!e && nearAll(s.box, e.boxes["51"]) && s.pads.length === 4 && s.pads.every(([y, net], i) => {
       const p = e.pads.find(q => q.name === String(i + 1));
       return p && p.net === net && near(p.x, s.x) && near(p.y, y) &&
         near(p.box[2] - p.box[0], AD.socketPad[0]) && near(p.box[3] - p.box[1], AD.socketPad[1]);
     }), e ? JSON.stringify(e.pads.slice(0, 4)) : "no such element");
}
const jp2 = (A.elements.JP2 || { pads: [] }).pads;
const labels = A.texts.filter(t => t.y < AD.headerY).sort((a, b) => a.x - b.x).map(t => t.text);
ok("adapter JP2: pads 1 to 5 at headerY by x and net, headerPad's size, each named by the text below it",
   AD.header.length === 5 && AD.header.every(([x, net, name], i) => {
     const p = jp2.find(q => q.name === String(i + 1));
     return p && p.net === net && near(p.x, x) && near(p.y, AD.headerY) && p.diameter === AD.headerPad.dia &&
       p.drill === AD.headerPad.drill && labels[i] === name;
   }), `${labels.join(" ")}; ${JSON.stringify(jp2)}`);
const H = AD.hole, holes = Object.values(A.elements).filter(e => e.package === "MOUNTINGHOLE_2.5_PLATED");
ok("adapter: the mounting holes where the page puts them, drill, pad and silkscreen ring",
   holes.length === AD.holes.length && AD.holes.every(([x, y]) => holes.some(e => {
     const p = e.pads[0], ring = e.circles.find(c => c[4] === "21");
     return p && near(p.x, x) && near(p.y, y) && p.drill === H.drill && p.diameter === H.pad &&
       !!ring && near(ring[0], x) && near(ring[1], y) && ring[2] === H.ring && ring[3] === H.line;
   })), JSON.stringify(holes.map(e => [e.pads, e.circles])));
const adFids = Object.values(A.elements).filter(e => e.package === "FIDUCIAL_1MM");
ok("adapter: the fiducials where the page puts them, round copper fiducial.dia across",
   adFids.length === AD.fiducials.length && AD.fiducials.every(([x, y]) => adFids.some(e => {
     const p = e.pads[0];
     return p && near(p.x, x) && near(p.y, y) && p.roundness === 100 &&
       near(p.box[2] - p.box[0], AD.fiducial.dia) && near(p.box[3] - p.box[1], AD.fiducial.dia);
   })), JSON.stringify(adFids.map(e => e.pads)));
ok(`adapter: the ${A.texts.length} silkscreen texts, each where the file puts it, centred and turned as it is, silkSize high`,
   AD.silk.length === A.texts.length && A.texts.every(t => t.size === AD.silkSize) &&
   AD.silk.every(([x, y, s, centred, turned]) => A.texts.some(t => t.text === s && near(t.x, x) && near(t.y, y) &&
     (t.align === "center") === centred && (t.rot === "R90") === turned)),
   JSON.stringify(A.texts));
partBoxes(A, "adapter", AD.parts);

const kbBrd = process.env.KB2040_BRD, adBrd = process.env.ADAPTER_BRD;
if (kbBrd && adBrd) {
  const tool = fileURLToPath(new URL(KB_DIR + "brdgeom.py", ROOT));
  const r = spawnSync("python3", ["-I", tool, "--json", kbBrd, adBrd], { encoding: "utf8" });
  let fresh = null;
  try { fresh = JSON.parse(r.stdout); } catch {}
  ok("brdgeom.py run on KB2040_BRD and ADAPTER_BRD gives kb2040-boards.json",
     r.status === 0 && !!fresh && JSON.stringify(fresh) === JSON.stringify(boards),
     r.status !== 0 ? `exit ${r.status}: ${r.stderr.trim().split("\n").pop()}`
                    : fresh ? `sha256 ${fresh.kb2040.sha256.slice(0, 12)} ${fresh.adapter.sha256.slice(0, 12)}` : "no JSON");
} else {
  skip++;
  console.log("  skip running brdgeom.py on the board files: KB2040_BRD and ADAPTER_BRD name them");
}

/* ---------- 9. the board files' terms */
console.log("\nThe board files' terms");
const creditsAt = new URL(KB_DIR + "CREDITS.md", ROOT);
const credits = existsSync(creditsAt) ? readFileSync(creditsAt, "utf8") : "";
const toolSrc = read(KB_DIR + "brdgeom.py");
// Each board repository the page's Sources cite, with the short commit given.
const cited = [...html.matchAll(/href="(https:\/\/github\.com\/adafruit\/[\w-]+-PCB)">[^<]*<\/a>\s+at ([0-9a-f]{7})\b/g)]
  .map(m => [m[1].replace("https://", ""), m[2]]);
const fullAt = (src, repo) =>
  (src.match(new RegExp(`${repo.replace(/\./g, "\\.")} at\\s+(?:commit\\s+)?([0-9a-f]{40})\\b`)) || [])[1];
const noteAt = html.indexOf('<div class="note">');
const note = noteAt < 0 ? "" : html.slice(noteAt, html.indexOf("</div>", noteAt));
const designer = (note.match(/by ([^,(<]+?) for Adafruit\s+Industries/) || [])[1] || "";
const TERMS = "CC BY-SA 3.0", TERMS_URL = "https://creativecommons.org/licenses/by-sa/3.0/";
ok(`CREDITS.md names what is the board files', ${TERMS}, both repositories at the commits the page and brdgeom.py cite, and ${designer || "the designer the page credits"}`,
   cited.length === 2 && cited.every(([repo, short]) => {
     const t = fullAt(toolSrc, repo);
     return !!t && t.startsWith(short) && fullAt(credits, repo) === t;
   }) && designer.length > 3 && credits.includes(designer) && credits.includes(TERMS) && credits.includes(TERMS_URL) &&
   ["KB2040Oscuino.html", "`KB`", "`AD`", "kb2040-boards.json"].every(s => credits.includes(s)),
   credits ? JSON.stringify(cited.map(([repo, short]) => [short, fullAt(toolSrc, repo), fullAt(credits, repo)]))
           : "no CREDITS.md beside the sketch");
ok(`the page credits the boards ${TERMS} and points at CREDITS.md`,
   (html.match(/board files, CC BY-SA 3\.0<\/text>/g) || []).length === 1 && /see CREDITS\.md/.test(note));

console.log(`\n${pass} passed, ${fail} failed, ${skip} skipped\n`);
if (fail) process.exit(1);

/* The hand-written XiaoSamd21Oscuino pair, held to one list of libraries and
   parameters.

   The sketch measures with one of three libraries, chosen by /cap/lib, and
   routes each library's /cap/<p> set-up addresses; the page has a tab per
   library and sends and tabulates the same parameters; ADDRESSES.md documents
   them under "cap set-up". A name that moves in one place and not the others
   is silent: the board answers nothing for an address it does not route, and
   the page shows "–" for a parameter it never hears. `make check` cannot see
   this pair, because it is not generated from boards.json, so this checks,
   with no board and no browser:

     1. the libraries: the sketch's LIB_NAME, the page's LIBS and the names
        ADDRESSES.md's "cap set-up" gives agree, in order
     2. the parameters, each with its library: the sketch's PARAM_ADDR and
        PARAM_LIB, the sketch header's set-up table, the page's PARAMS and
        ADDRESSES.md's table agree, in order, and the sketch reaches them
        through route("/cap"), not an exact-match dispatch
     3. the page's codec on bytes captured from the board on 2026-10-29 by
        test/hardware/xiaosamd21capture.py, phase by phase:
          FreeTouch: the hello names the sketch with /enq/cap 7; /cap/config
            answers /cap/lib, every FreeTouch parameter for every pad, then
            /cap/pins; the /cap/3/intcap 20 echo; a stream of /state and seven
            /cap ints with consecutive sequence numbers
          FastTouch: /cap/lib echoed and the hello re-sent with /enq/cap 11;
            /cap/config is /cap/lib and the eleven pins; its stream carries
            eleven ints, each within 0..23 (fastTouchMax() on SAMD21)
          CapacitiveSensor: the hello carries no /enq/cap until pins are
            chosen; /cap/send 10 and /cap/recv 9 8 are echoed and the hello
            then says 2; /cap/config carries its parameters, pins and pads;
            /cap/defaults takes the pins away again
     4. the page's DEFAULTS equal what the board reported at its defaults; the
        page's orange marks compare against them
     5. the page's drawing has a fader for every pin each library reports, and
        as many FreeTouch faders as /enq/cap says
     6. the page encodes a set-up request and a /cap/lib choice to the same
        bytes as test/hardware/oscprobe.py, whose bytes the board accepted
     7. what the page asks the board back, run against a stub document: one
        /cap/config per library switch, another when a /cap's length is not
        /cap/pins', none for a library it has no tab for; and its port is
        held only once open, and let go only on its own disconnect

   Graded by test/mutants-xiaosamd21.mjs: one mutant per claim above, and each
   must fail this test.

   Run: node test/test-xiaosamd21-contract.mjs   (or `make test`). No dependencies. */

import { readFileSync } from "node:fs";

const ROOT = new URL("../../../", import.meta.url);
const read = p => readFileSync(new URL(p, ROOT), "utf8");
const ino = read("examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino");
const html = read("examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.html");
const contract = read("ADDRESSES.md");
const capture = new Uint8Array(readFileSync(new URL("xiaosamd21-2026-10-29.slip", import.meta.url)));

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};
const eq = (label, a, b) => ok(label, JSON.stringify(a) === JSON.stringify(b),
                               `${JSON.stringify(a)}\n       != ${JSON.stringify(b)}`);

const script = html.split(/<script>/)[1].split(/<\/script>/)[0];
const page = new Function(
  script.split("/* ============================================================\n   3. Web Serial")[0] +
  "return { decodePacket, messagesOf, SlipDecoder, encodeMessage };")();
const constOf = name => {
  const m = script.match(new RegExp(`const ${name} = ([\\[{][\\s\\S]*?[\\]}]);\\n`));
  return m ? new Function(`return ${m[1]}`)() : undefined;
};
const LIBS = constOf("LIBS"), PARAMS = constOf("PARAMS"), DEFAULTS = constOf("DEFAULTS"), PINS = constOf("PINS");
const block = (src, re) => ((src.match(re) || [])[1] || "");

/* ---------- 1. the libraries */
console.log("\nThe libraries");
const sketchLibs = [...block(ino, /LIB_NAME\[LIB_COUNT\]\s*=\s*\{([^}]*)\}/).matchAll(/"([A-Za-z]+)"/g)].map(m => m[1]);
const libEnum = block(ino, /enum Lib\s*\{([^}]*)\}/).split(",").map(s => s.trim()).filter(s => s && s !== "LIB_COUNT");
const setupAt = contract.indexOf("### cap set-up");
const setupBlock = setupAt < 0 ? "" : contract.slice(setupAt, contract.indexOf("\n## ", setupAt));
ok("sketch LIB_NAME parsed, one name per enum Lib entry",
   sketchLibs.length === 3 && libEnum.length === sketchLibs.length, sketchLibs.join(" "));
eq("the page's LIBS are the sketch's", LIBS, sketchLibs);
ok("ADDRESSES.md's cap set-up names every library",
   sketchLibs.every(l => setupBlock.includes("`" + l + "`")), sketchLibs.join(" "));

/* ---------- 2. one parameter list, with libraries, in four places */
console.log("\nThe set-up parameters");
const libOfEnum = e => sketchLibs[libEnum.indexOf(e)];
const sketchNames = [...block(ino, /PARAM_ADDR\[P_COUNT\]\s*=\s*\{([^}]*)\}/).matchAll(/"\/cap\/([a-z]+)"/g)].map(m => m[1]);
const sketchLibOf = block(ino, /PARAM_LIB\[P_COUNT\]\s*=\s*\{([^}]*)\}/).split(",").map(s => s.trim()).filter(Boolean).map(libOfEnum);
const sketchParams = sketchNames.map((n, i) => `${n}@${sketchLibOf[i]}`);
const headerAt = ino.indexOf(" *   p           library");
const headerBlock = headerAt < 0 ? "" : ino.slice(headerAt, ino.indexOf("\n *\n", headerAt));
const headerParams = [...headerBlock.matchAll(/^ \*   ([a-z]+)\s{2,}([A-Za-z]+)\s{2,}/gm)]
  .filter(m => m[1] !== "p").map(m => `${m[1]}@${m[2]}`);
const pageParams = LIBS.flatMap(l => (PARAMS[l] || []).map(p => `${p}@${l}`));
const contractParams = [...setupBlock.matchAll(/^\| `([a-z]+)` \| ([A-Za-z]+) \|/gm)].map(m => `${m[1]}@${m[2]}`);

ok("sketch PARAM_ADDR and PARAM_LIB parsed, same length",
   sketchNames.length === 8 && sketchLibOf.length === sketchNames.length && sketchLibOf.every(Boolean),
   sketchParams.join(" "));
eq("the sketch header's table lists the routed parameters", headerParams, sketchParams);
eq("the page's PARAMS are the routed parameters", pageParams, sketchParams);
eq("ADDRESSES.md's cap set-up table is the routed parameters", contractParams, sketchParams);
ok("the sketch routes /cap with route(), so sub-addresses reach it",
   /\.route\(\s*"\/cap"\s*,\s*routeCap\)/.test(ino) && !/\.dispatch\(\s*"\/cap"/.test(ino));

/* ---------- 3. the page's codec on bytes from the board */
console.log("\nBytes captured from the board, through the page's codec");
const frames = [];
new page.SlipDecoder().transform(capture, { enqueue: f => frames.push(f) });
const decoded = [];
for (const f of frames) {
  try { decoded.push(page.messagesOf(page.decodePacket(f))); }
  catch (e) { decoded.push(null); }
}
ok("every frame decodes", frames.length > 0 && decoded.every(Boolean), `${frames.length} frames`);

// Phases follow the board's own /cap/lib echoes, so each bundle is judged
// against the library the board said was measuring when it sent it.
let phase = "FreeTouch";
const by = {};
for (const ms of decoded.filter(Boolean)) {
  const libMsg = ms.find(m => m.address === "/cap/lib");
  if (libMsg && ms.length === 1) phase = libMsg.args[0];
  (by[phase] = by[phase] || []).push(ms);
}
const ofAddr = (list, a) => (list || []).filter(ms => ms.some(m => m.address === a));
const argsOf = (ms, a) => (ms.find(m => m.address === a) || { args: null }).args;
const hellos = l => ofAddr(by[l], "/enq");
const configs = l => (by[l] || []).filter(ms => ms[0] && ms[0].address === "/cap/lib" && ms.length > 1);
const reports = l => ofAddr(by[l], "/state");
const consecutive = rs => rs.map(ms => argsOf(ms, "/state")[0]).every((s, i, a) => i === 0 || s === a[i - 1] + 1);

// FreeTouch
const ftHello = hellos("FreeTouch")[0] || [];
eq("FreeTouch: the hello names the sketch", argsOf(ftHello, "/enq"), ["XiaoSamd21Oscuino"]);
eq("FreeTouch: the hello announces seven pads", argsOf(ftHello, "/enq/cap"), [7]);
ok("FreeTouch: the hello announces /diag and carries it",
   !!argsOf(ftHello, "/enq/diag") && !!argsOf(ftHello, "/diag"));
const ftConfig = configs("FreeTouch")[0] || [];
const ftNames = sketchParams.filter(p => p.endsWith("@FreeTouch")).map(p => p.split("@")[0]);
eq("FreeTouch: /cap/config is /cap/lib, every FreeTouch parameter, then /cap/pins",
   ftConfig.map(m => m.address), ["/cap/lib", ...ftNames.map(p => "/cap/" + p), "/cap/pins"]);
ok("FreeTouch: one value per pad in every /cap/<p>",
   ftConfig.filter(m => ftNames.includes(m.address.slice(5))).every(m => m.tags === "iiiiiii"));
const echo = (by.FreeTouch || []).find(ms => ms.length === 1 && ms[0].address === "/cap/intcap");
eq("FreeTouch: /cap/3/intcap 20 was echoed on pad 3 only", echo && echo[0].args, [63, 63, 63, 20, 63, 63, 63]);
const ftRep = reports("FreeTouch");
ok("FreeTouch: a stream of /state <i> <i> and seven /cap ints",
   ftRep.length >= 20 && ftRep.every(ms => (ms.find(m => m.address === "/state") || {}).tags === "ii"
                                        && (ms.find(m => m.address === "/cap") || {}).tags === "iiiiiii"),
   `${ftRep.length} bundles`);
ok("FreeTouch: sequence numbers are consecutive", consecutive(ftRep));

// FastTouch
const fastHello = hellos("FastTouch")[0] || [];
ok("FastTouch: /cap/lib echoed alone, then the hello", (by.FastTouch || [])[0]?.length === 1 && fastHello.length > 0);
eq("FastTouch: the hello announces eleven pads", argsOf(fastHello, "/enq/cap"), [11]);
const fastConfig = configs("FastTouch")[0] || [];
eq("FastTouch: /cap/config is /cap/lib and the pins", fastConfig.map(m => m.address), ["/cap/lib", "/cap/pins"]);
eq("FastTouch: the pins are D0..D10", argsOf(fastConfig, "/cap/pins"), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
const fastRep = reports("FastTouch");
ok("FastTouch: a stream of eleven /cap ints, each within 0..23",
   fastRep.length >= 5 && fastRep.every(ms => {
     const c = ms.find(m => m.address === "/cap");
     return c && c.tags === "i".repeat(11) && c.args.every(v => v >= 0 && v <= 23);
   }), `${fastRep.length} bundles`);
ok("FastTouch: sequence numbers are consecutive", consecutive(fastRep));

// CapacitiveSensor
const csHellos = hellos("CapacitiveSensor");
ok("CapacitiveSensor: with no pins the hello carries no /enq/cap",
   csHellos.length >= 1 && argsOf(csHellos[0], "/enq/cap") === null);
const csConfigs = configs("CapacitiveSensor");
eq("CapacitiveSensor: /cap/config with no pins",
   (csConfigs[0] || []).map(m => [m.address, m.args]),
   [["/cap/lib", ["CapacitiveSensor"]], ["/cap/samples", []], ["/cap/timeout", []],
    ["/cap/send", []], ["/cap/recv", []], ["/cap/pins", []]]);
ok("CapacitiveSensor: /cap/send 10 and /cap/recv 9 8 echoed",
   (by.CapacitiveSensor || []).some(ms => ms.length === 1 && JSON.stringify(ms[0].args) === "[10]" && ms[0].address === "/cap/send")
   && (by.CapacitiveSensor || []).some(ms => ms.length === 1 && JSON.stringify(ms[0].args) === "[9,8]" && ms[0].address === "/cap/recv"));
ok("CapacitiveSensor: with pins the hello says 2",
   csHellos.some(ms => JSON.stringify(argsOf(ms, "/enq/cap")) === "[2]"));
const csPinned = csConfigs.find(ms => JSON.stringify(argsOf(ms, "/cap/recv")) === "[9,8]") || [];
eq("CapacitiveSensor: /cap/config with pins",
   csPinned.map(m => [m.address, m.args]).filter(([a]) => !["/cap/samples", "/cap/timeout"].includes(a)),
   [["/cap/lib", ["CapacitiveSensor"]], ["/cap/send", [10]], ["/cap/recv", [9, 8]], ["/cap/pins", [9, 8]]]);
ok("CapacitiveSensor: /cap/defaults takes the pins away",
   csConfigs.length >= 3 && JSON.stringify(argsOf(csConfigs[csConfigs.length - 1], "/cap/pins")) === "[]");

/* ---------- 4. the page's defaults are the board's */
console.log("\nDefaults");
for (const p of ftNames)
  eq(`DEFAULTS.FreeTouch.${p} is what the board reported`, Array(7).fill(DEFAULTS.FreeTouch[p]), argsOf(ftConfig, "/cap/" + p));
for (const p of sketchParams.filter(q => q.endsWith("@CapacitiveSensor")).map(q => q.split("@")[0]))
  eq(`DEFAULTS.CapacitiveSensor.${p} is what the board reported`, Array(2).fill(DEFAULTS.CapacitiveSensor[p]), argsOf(csPinned, "/cap/" + p));
eq("DEFAULTS.FastTouch is empty, as it has no parameters", DEFAULTS.FastTouch, {});

/* ---------- 5. the drawing has a fader for every pad */
console.log("\nThe drawing");
const drawn = new Set(PINS.filter(r => r[1] !== null).map(r => r[1]));
const ptcDrawn = PINS.filter(r => r[5] !== null);
eq("the page draws as many FreeTouch faders as /enq/cap announces", ptcDrawn.length, argsOf(ftHello, "/enq/cap")[0]);
for (const pin of argsOf(ftConfig, "/cap/pins") || [])
  ok(`FreeTouch pin ${pin} from /cap/pins has a fader with a Y line`, ptcDrawn.some(r => r[1] === pin));
ok("every FastTouch pin from /cap/pins has a row", (argsOf(fastConfig, "/cap/pins") || []).every(p => drawn.has(p)));

/* ---------- 6. outbound bytes */
console.log("\nOutbound");
const hex = a => [...a].map(b => b.toString(16).padStart(2, "0")).join("");
// test/hardware/oscprobe.py msg(...): the bytes capsetupprobe.py and the capture sent
eq("/cap/3/compcap 4000 encodes as oscprobe.py encodes it",
   hex(page.encodeMessage("/cap/3/compcap", [4000])),
   "2f6361702f332f636f6d7063617000002c69000000000fa0");
eq("/cap/lib FastTouch encodes as oscprobe.py encodes it",
   hex(page.encodeMessage("/cap/lib", ["FastTouch"])),
   "2f6361702f6c6962000000002c73000046617374546f756368000000");
eq("/cap/recv 9 8 encodes as oscprobe.py encodes it",
   hex(page.encodeMessage("/cap/recv", [9, 8])),
   "2f6361702f726563760000002c6969000000000900000008");
eq("an ask carries an empty type tag string",
   hex(page.encodeMessage("/cap/config", [])), "2f6361702f636f6e666967002c000000");

/* ---------- 7. what the page asks the board, given what the board says */
// The whole script, UI half included, against a stub document: every element
// is a Proxy that takes any property, call or iteration and does nothing.
// "serial" is not in the stub navigator, so the page runs as if no Web Serial;
// plug() then hands it a writer, and what it writes is what is checked.
console.log("\nThe page's answers");
const stub = () => new Proxy(function () {}, {
  get: (_, k) => k === Symbol.iterator ? function* () {}
               : k === Symbol.toPrimitive ? () => ""
               : k === "then" ? undefined : stub(),
  set: () => true,
  apply: () => stub(),
  construct: () => stub(),
});
const ui = new Function("document", "navigator", "location", "setInterval",
  script + "\nreturn { onFrame, plug: w => { writer = w; }, now: () => ({ lib, pins }) };")(
  stub(), stub(), { search: "" }, () => 0);
let sent = [];
ui.plug({ write: async bytes => new page.SlipDecoder().transform(bytes, {
  enqueue: f => sent.push(...page.messagesOf(page.decodePacket(f)).map(m => m.address)) }) });
const feed = (address, args) => ui.onFrame(page.encodeMessage(address, args));
const asked = () => sent.splice(0);   // what the page sent since the last look

feed("/cap/lib", ["FreeTouch"]);       // at rest on FreeTouch, as after boot
asked();
feed("/cap/lib", ["FastTouch"]);       // a switch: the echo, then the hello
feed("/enq", ["XiaoSamd21Oscuino"]);
eq("a library switch, its echo then its hello, asks for /cap/config once", asked(), ["/cap/config"]);
eq("the echo moved the page to that library", ui.now().lib, "FastTouch");
feed("/cap/pins", [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
feed("/cap", Array(11).fill(0));
eq("a /cap as long as /cap/pins asks nothing", asked(), []);
feed("/cap", [5, 6, 7]);
eq("a /cap of another length asks for /cap/config", asked(), ["/cap/config"]);
for (let i = 0; i < 3; i++) feed("/cap/lib", ["Mystery"]);
feed("/cap", [1, 2, 3]);
eq("a /cap/lib this page has no tab for asks nothing, however often it comes", asked(), []);
eq("... and leaves no pins to draw counts on", ui.now().pins, null);

// The transport, with a fake Web Serial: a port that will not open, and
// disconnect events from this board's port and from another one.
const listeners = {};
const net = new Function("document", "navigator", "location", "setInterval",
  script + "\nreturn { connect, holds: () => port, hold: p => { port = p; } };")(
  stub(), { serial: { addEventListener: (t, f) => { listeners[t] = f; },
                      requestPort: async () => ({ open: async () => { throw new Error("busy"); } }) } },
  { search: "" }, () => 0);
const tick = () => new Promise(r => setTimeout(r, 0));
await net.connect().catch(() => {});
eq("a port that would not open is not held", net.holds(), null);
const board = { close: async () => {} };
net.hold(board);
listeners.disconnect({ target: {} });
await tick();
ok("another port's disconnect leaves this one held", net.holds() === board);
listeners.disconnect({ target: board });
await tick();
eq("this port's disconnect lets it go", net.holds(), null);

console.log(`\n${pass} passed, ${fail} failed\n`);
if (fail) process.exit(1);

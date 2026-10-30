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
        bytes as test/hardware/oscprobe.py, whose bytes the board accepted,
        and frames them as it does, END before and after; through the OSC
        library's own SLIP decoder (test/host/slipframes) two such frames sent
        back to back arrive as two packets, where with the closing END alone
        the second was lost
     7. what the page asks the board back, run against a stub document: one
        /cap/config per library switch, another when a /cap's length is not
        /cap/pins', none for a library it has no tab for; its port is held
        only once open, and let go only on its own disconnect; /i2c moves it
        between touch and the I2C tab
     8. the Wii binding: WiiExtension.h, the page's WII_KINDS and
        ADDRESSES.md's "wii fields" list the same kinds and fields, with the
        same ranges; each kind reads through the view of its own library
        class; each drawing on the page reads only fields its kind has; and,
        where NintendoExtensionCtrl is installed (NXC_SRC, or the Library
        Manager's folder on this machine), each kind's fields are the
        getters its class declares that are public, const, take no argument
        and return a number or a boolean (the platters' from their base,
        TurntableExpansion), in order, with the float ones marked as floats,
        booleans 0..1, and each range the library's comment states
     9. the Nunchuk drawing, run against a stub document that keeps what is
        drawn: flat; rolled and pitched 30 degrees, each the way it was
        checked by hand on the board; both at once, against the shortest
        rotation onto up worked out here another way; upside down; rolled
        120 degrees; and rolled across 90

   Graded by test/mutants-xiaosamd21.mjs: one mutant per claim above, and each
   must fail this test.

   Run: node test/test-xiaosamd21-contract.mjs   (or `make test`). No dependencies. */

import { readFileSync, readdirSync, existsSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";

const ROOT = new URL("../../../", import.meta.url);
const read = p => readFileSync(new URL(p, ROOT), "utf8");
const ino = read("examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino");
const html = read("examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.html");
const contract = read("ADDRESSES.md");
const capture = new Uint8Array(readFileSync(new URL("xiaosamd21-2026-10-29.slip", import.meta.url)));
const slipframes = read("test/host/slipframes.cpp");

let pass = 0, fail = 0, skip = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};
const eq = (label, a, b) => ok(label, JSON.stringify(a) === JSON.stringify(b),
                               `${JSON.stringify(a)}\n       != ${JSON.stringify(b)}`);

const script = html.split(/<script>/)[1].split(/<\/script>/)[0];
const page = new Function(
  script.split("/* ============================================================\n   3. Web Serial")[0] +
  "return { decodePacket, messagesOf, SlipDecoder, encodeMessage, slipEncode };")();
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
// oscprobe.py slip_encode(msg("/wii/id", [])): END, the message, END. A byte
// to escape is in the second check, as END and ESC inside an int argument.
eq("/wii/id is framed as oscprobe.py frames it, END before and after",
   hex(page.slipEncode(page.encodeMessage("/wii/id", []))), "c02f7769692f6964002c000000c0");
eq("END and ESC inside a message are escaped, the frame still END before and after",
   hex(page.slipEncode(new Uint8Array([0x01, 0xC0, 0xDB, 0x02]))), "c001dbdcdbdd02c0");
// test/host/slipframes.cpp runs a copy of the sketch's pollOSC() on such frames.
const pollOf = src => (src.match(/static bool pollOSC\(\) \{[\s\S]*?\n\}\n/) || [""])[0];
ok("test/host/slipframes.cpp's pollOSC() is the sketch's, line for line",
   pollOf(ino).length > 100 && pollOf(ino) === pollOf(slipframes), `${pollOf(ino).length} and ${pollOf(slipframes).length} characters`);

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
eq("a library switch, its echo then its hello, asks for /cap/config once, and /i2c once",
   asked(), ["/cap/config", "/i2c"]);
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

// The I2C mode: /i2c 1 moves the page to it and asks what is on the bus;
// /i2c 0 moves it back. A /wii before any /wii/id is drawn as unknown bytes
// without throwing.
const ui2 = new Function("document", "navigator", "location", "setInterval",
  script + "\nreturn { onFrame, plug: w => { writer = w; }, mode: () => mode };")(
  stub(), stub(), { search: "" }, () => 0);
ui2.plug({ write: async bytes => new page.SlipDecoder().transform(bytes, {
  enqueue: f => sent.push(...page.messagesOf(page.decodePacket(f)).map(m => m.address)) }) });
asked();
ui2.onFrame(page.encodeMessage("/i2c", [1]));
eq("/i2c 1 moves the page to the I2C tab", ui2.mode(), "i2c");
eq("... and asks for the scan and the identity", asked(), ["/i2c/scan", "/wii/id"]);
let threw = null;
try { ui2.onFrame(page.encodeMessage("/wii", [1, 2, 3, 4, 5, 6])); } catch (e) { threw = e.message; }
ok("a /wii with no /wii/id yet does not throw", threw === null, threw);
ui2.onFrame(page.encodeMessage("/i2c", [0]));
eq("/i2c 0 moves it back to touch", ui2.mode(), "touch");

/* ---------- 8. the Wii binding: one list in three places, true to the library */
console.log("\nThe Wii binding");
const wiiH = read("examples/XiaoSamd21Oscuino/WiiExtension.h");
const views = Object.fromEntries([...wiiH.matchAll(/static (\w+)::Shared (\w+)\(wiiPort\);/g)].map(([, cls, v]) => [v, cls]));
const hRows = {};
for (const [, arr, body] of wiiH.matchAll(/static const WiiField (\w+)\[\] = \{([\s\S]*?)\n\};/g))
  hRows[arr] = [...body.matchAll(/WII_GET(F?)\((\w+), ([\w.]+), (-?\d+), (-?\d+)\)/g)]
    .map(([, f, view, getter, lo, hi]) => ({ view, field: f ? [getter, +lo, +hi, "f"] : [getter, +lo, +hi] }));
const hKinds = [...wiiH.matchAll(/\{ "(\w+)", ExtensionType::(\w+), (\w+), WII_COUNT_OF\(\3\) \}/g)]
  .map(([, name, lib, arr]) => ({ name, lib, rows: hRows[arr] || [] }));
const getLines = (wiiH.match(/^\s*WII_GETF?\(/gm) || []).length;
const hCount = hKinds.reduce((n, k) => n + k.rows.length, 0);
ok(`WiiExtension.h parsed: seven kinds, all ${getLines} WII_GET lines read into them`,
   hKinds.length === 7 && hKinds.every(k => k.rows.length) && hCount === getLines && getLines > 0,
   `${hKinds.map(k => `${k.name} ${k.rows.length}`).join(", ")}; ${getLines} lines`);
ok("each kind reads every field through the view of its own library class",
   hKinds.every(k => k.rows.every(r => views[r.view] === k.lib)),
   hKinds.flatMap(k => k.rows.filter(r => views[r.view] !== k.lib).map(r => `${k.name}.${r.field[0]} via ${r.view}`)).join(" "));
ok("every range runs low to high", hKinds.every(k => k.rows.every(r => r.field[1] < r.field[2])));

const kindsLit = (script.match(/const WII_KINDS = (\[[\s\S]*?\n\]);/) || [])[1];
const pKinds = kindsLit ? new Function("return " + kindsLit)() : [];
eq("the page's WII_KINDS are the header's, kind by kind and field by field",
   pKinds.map(k => [k.name, k.lib, k.fields]), hKinds.map(k => [k.name, k.lib, k.rows.map(r => r.field)]));
const wiiAt = contract.indexOf("### wii fields");
const wiiBlock = wiiAt < 0 ? "" : contract.slice(wiiAt, contract.indexOf("\n## ", wiiAt));
const docKinds = [...wiiBlock.matchAll(/^\| `(\w+)` \| `(\w+)` \| (.+) \|$/gm)].map(([, name, lib, fields]) =>
  [name, lib, [...fields.matchAll(/`([\w.]+)` (-?\d+)\.\.(-?\d+)( float)?/g)]
    .map(([, n, lo, hi, f]) => f ? [n, +lo, +hi, "f"] : [n, +lo, +hi])]);
eq("ADDRESSES.md's wii fields: the header's kinds, classes, fields and ranges, in order",
   docKinds, hKinds.map(k => [k.name, k.lib, k.rows.map(r => r.field)]));

// Each drawing, run against a stand-in for the fields that records every name
// asked of it, reads only fields its kind has.
const art = new Function("document", "navigator", "location", "setInterval",
  script + "\nreturn { WII_ART, WII_KINDS };")(stub(), stub(), { search: "" }, () => 0);
for (const k of art.WII_KINDS) {
  const names = new Set(k.fields.map(x => x[0])), asked = new Set();
  const f = new Proxy({}, { get: (_, n) => { if (typeof n === "string") asked.add(n); return 1; } });
  let threw = null;
  try { art.WII_ART[k.name](stub()).update(f, k); } catch (e) { threw = e.message; }
  const strays = [...asked].filter(n => !names.has(n));
  ok(`${k.name}: its drawing reads ${asked.size} fields, every one the kind's own`,
     !threw && asked.size > 0 && !strays.length, threw || strays.join(" "));
}

// The library's own headers, where they are installed: each class's public,
// const, no-argument getters that return a number or a boolean (GETTER's
// types: not getExpectedType(), nor what ExtensionController gives them all),
// in the order declared, with their types and the
// "a-b" range of any comment beside them; a member object (the turntable's
// left and right) contributes its class's base's getters under its name.
const nxcDir = process.env.NXC_SRC ||
  join(homedir(), "Documents", "Arduino", "libraries", "Nintendo_Extension_Ctrl", "src", "controllers");
const GETTER = /^\s*(?:virtual\s+)?(uint8_t|uint16_t|int8_t|int16_t|boolean|bool|float|VelocityID)\s+(\w+)\(\)\s*const\s*(?:=\s*0\s*)?;\s*(?:\/\/\s*(.*))?$/;
function classesOf(text) {
  const classes = {}, stack = [];
  let depth = 0;
  for (const line of text.split("\n")) {
    const head = line.match(/^\s*class\s+(\w+)(?:\s*:\s*(?:public|private|protected)?\s*([\w:]+))?\s*\{/);
    const top = stack[stack.length - 1];
    if (head) {
      classes[head[1]] = { base: head[2] ? head[2].split("::").pop() : null, getters: [], members: [] };
      stack.push({ name: head[1], depth: depth + 1, access: "private" });
    } else if (top) {
      const acc = line.match(/^\s*(public|private|protected)\s*:/);
      if (acc && depth === top.depth) top.access = acc[1];
      const g = line.match(GETTER);
      if (g && depth === top.depth && top.access === "public")
        classes[top.name].getters.push({ name: g[2], type: g[1], comment: g[3] || "" });
    }
    for (const ch of line) depth += ch === "{" ? 1 : ch === "}" ? -1 : 0;
    const open = stack[stack.length - 1];
    if (open && depth < open.depth) {
      stack.pop();
      const inst = line.match(/\}\s*(\w+)\s*;/), parent = stack[stack.length - 1];
      if (inst && parent) classes[parent.name].members.push({ name: inst[1], cls: open.name });
    }
  }
  return classes;
}
// Where a range differs from the library's comment on purpose, and why.
const RANGE_NOT_COMMENT = {
  "DJTurntableController.turntable": "left's plus right's turntable(), each -32..31 by the code",
};
if (existsSync(nxcDir)) {
  const headers = readdirSync(nxcDir).filter(f => f.endsWith(".h")).map(f => readFileSync(join(nxcDir, f), "utf8"));
  const classes = Object.assign({}, ...headers.map(classesOf));
  const alias = {};
  for (const h of headers)
    for (const [, a, base] of h.matchAll(/using (\w+) = NintendoExtensionCtrl::BuildControllerClass\s*<NintendoExtensionCtrl::(\w+)>/g))
      alias[a] = base;
  const bound = cls => {
    const c = classes[cls];
    if (!c) return [];
    const out = [...c.getters];
    for (const m of c.members) {
      const mc = classes[m.cls], via = mc && mc.base && classes[mc.base] ? classes[mc.base] : mc;
      if (via) out.push(...via.getters.map(g => ({ ...g, name: `${m.name}.${g.name}` })));
    }
    return out;
  };
  console.log(`  (the library's headers: ${nxcDir})`);
  for (const k of hKinds) {
    const lib = bound(alias[k.lib]);
    eq(`${k.name}: every public, const, no-argument getter of ${k.lib} that returns a number or a boolean, in the header's order`,
       k.rows.map(r => r.field[0]), lib.map(g => g.name));
    const byName = Object.fromEntries(lib.map(g => [g.name, g]));
    const wrong = k.rows.filter(r => {
      const g = byName[r.field[0]];
      if (!g) return false;
      const isFloat = g.type === "float", isBool = g.type === "boolean" || g.type === "bool";
      if (isFloat !== (r.field[3] === "f")) return true;
      if (isBool && (r.field[1] !== 0 || r.field[2] !== 1)) return true;
      const c = g.comment.match(/(-?\d+)(?:\.0)?(?:-| to )(-?\d+)(?:\.0)?/);
      return !!c && !RANGE_NOT_COMMENT[`${k.lib}.${g.name}`] && (r.field[1] !== +c[1] || r.field[2] !== +c[2]);
    });
    ok(`${k.name}: floats marked float, booleans 0..1, each range the comment beside its getter states`,
       lib.length > 0 && !wrong.length, wrong.map(r => r.field.join(" ")).join("; "));
  }
} else {
  skip++;
  console.log(`  skip the binding against the library's own headers: none at ${nxcDir}`);
}

console.log("\nThe Nunchuk drawing");
// Against a document that keeps what is drawn, given accelerometer values
// built from the library's angle formulas (Nunchuk.cpp: rollAngle()
// atan2(accelX - 511, accelZ - 511), pitchAngle() -atan2(accelY - 511, accelZ
// - 511)). The face nearer the eye moves toward the side that dips and
// narrows across a roll, and moves back and shortens along a pitch: the two
// directions checked by hand on the board. Upside down it is the plain
// underside, and a roll past 90 degrees keeps the head up the screen.
const kept = tag => new Proxy({ tag, attrs: {}, kids: [] }, {
  get: (t, k) => k === "setAttribute" ? (a, v) => { t.attrs[a] = String(v); }
               : k === "appendChild" ? c => { t.kids.push(c); return c; }
               : k in t ? t[k] : stub(),
  set: (t, k, v) => { t[k] = v; return true; },
});
const nunchukPage = new Function("document", "navigator", "location", "setInterval",
  script + "\nreturn { WII_ART, WII_KINDS };")(
  new Proxy({}, { get: (_, k) => k === "createElementNS" ? (ns, tag) => kept(tag) : stub() }),
  stub(), { search: "" }, () => 0);
const THICK = +(script.match(/const NUNCHUK_THICK = (\d+);/) || [])[1];
const G = 200;   // gravity's length in counts: any will do, only its direction is drawn
const deg = Math.PI / 180;
const rolled = a => [511 + G * Math.sin(a * deg), 511, 511 + G * Math.cos(a * deg)];   // right side down
const pitched = a => [511, 511 - G * Math.sin(a * deg), 511 + G * Math.cos(a * deg)];  // head up
const both = (r, p) => {   // rollAngle() r and pitchAngle() p at once, each under 90
  const tx = Math.tan(r * deg), ty = Math.tan(p * deg), z = G / Math.hypot(1, tx, ty);
  return [511 + z * tx, 511 - z * ty, 511 + z];
};
function nunchukAt([accelX, accelY, accelZ]) {
  const slot = kept("g");
  nunchukPage.WII_ART.Nunchuk(slot).update({ joyX: 128, joyY: 128, buttonC: false, buttonZ: false,
    accelX, accelY, accelZ,
    rollAngle: Math.atan2(accelX - 511, accelZ - 511) / deg,
    pitchAngle: -Math.atan2(accelY - 511, accelZ - 511) / deg },
    nunchukPage.WII_KINDS.find(k => k.name === "Nunchuk"));
  const all = [];
  (function walk(n) { all.push(n); n.kids.forEach(walk); })(slot);
  const face = all.find(n => n.tag === "g" && n.kids.some(c => c.attrs.fill === "url(#wiiWhite)"));
  const top = face && face.kids.find(n => n.tag === "g" && "opacity" in n.attrs);
  const m = ((face && face.attrs.transform) || "").match(
    /^translate\((-?[\d.]+),(-?[\d.]+)\) matrix\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+),(-?[\d.]+),/);
  const [x, y, a, b, c, d] = m ? m.slice(1).map(Number) : Array(6).fill(NaN);
  return { x, y, a, b, c, d, top: top ? top.attrs.opacity !== "0" : null };
}
// The shortest rotation taking up (x right, y toward the head, z out of the
// top face) onto the eye's direction, worked as a turn by its angle about
// up x eye (Rodrigues' formula applied to each axis of the body), not by the
// page's closed form; then seen from the eye, with the screen's y down.
function shortestTurn([ux, uy, uz]) {
  const k = [uy, -ux, 0].map(v => v / Math.hypot(uy, ux)), th = Math.acos(uz);
  const turn = v => {
    const kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
    const kxv = [k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]];
    return v.map((vi, i) => vi * Math.cos(th) + kxv[i] * Math.sin(th) + k[i] * kv * (1 - Math.cos(th)));
  };
  const [ex, ey, ez] = [[1, 0, 0], [0, 1, 0], [0, 0, 1]].map(turn);
  return { a: ex[0], b: -ex[1], c: -ey[0], d: ey[1], x: ez[0] * THICK / 2, y: -ez[1] * THICK / 2 };
}
const near = (v, w, tol = 0.02) => Math.abs(v - w) <= tol;
const flat = nunchukAt([511, 511, 511 + G]);
ok("Nunchuk drawing, flat: the top face where it lies, full size",
   near(flat.x, 0, 0.1) && near(flat.y, 0, 0.1) && near(flat.a, 1) && near(flat.d, 1) &&
   near(flat.b, 0) && near(flat.c, 0) && flat.top === true, JSON.stringify(flat));
const r30 = nunchukAt(rolled(30));
ok("... rolled 30 degrees right side down: the face moves right and narrows across",
   r30.x > 1 && near(r30.y, 0, 0.1) && r30.a > 0.8 && r30.a < 0.9 && near(r30.d, 1) && r30.top === true,
   JSON.stringify(r30));
const p30 = nunchukAt(pitched(30));
ok("... pitched 30 degrees head up: the face moves back, toward the cable, and shortens",
   near(p30.x, 0, 0.1) && p30.y > 1 && p30.d > 0.8 && p30.d < 0.9 && near(p30.a, 1) && p30.top === true,
   JSON.stringify(p30));
const rp = both(30, 20), gRp = Math.hypot(rp[0] - 511, rp[1] - 511, rp[2] - 511);
const want = shortestTurn([(511 - rp[0]) / gRp, (511 - rp[1]) / gRp, (rp[2] - 511) / gRp]);
const got = nunchukAt(rp);
ok("... rolled 30 and pitched 20 degrees at once: turned, sheared and moved as the shortest rotation onto up",
   ["a", "b", "c", "d"].every(e => near(got[e], want[e])) && near(got.x, want.x, 0.1) && near(got.y, want.y, 0.1) &&
   Math.abs(want.b) > 0.05 && got.top === true, JSON.stringify({ got, want }));
const over = nunchukAt([511, 511, 511 - G]);
ok("... upside down: the plain underside, not the top", over.top === false, JSON.stringify(over));
const r120 = nunchukAt(rolled(120));
ok("... rolled 120 degrees: the underside, its head still up the screen",
   r120.top === false && r120.d > 0.5, JSON.stringify(r120));
const r89 = nunchukAt(rolled(89)), r91 = nunchukAt(rolled(91));
ok("... rolled 89 then 91 degrees: it does not turn end for end",
   r89.d > 0.5 && r91.d > 0.5, `d ${r89.d} then ${r91.d}`);

console.log(`\n${pass} passed, ${fail} failed, ${skip} skipped\n`);
if (fail) process.exit(1);

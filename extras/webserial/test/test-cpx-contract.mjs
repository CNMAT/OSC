/* The one thing that can quietly break the hand-written CircuitPlaygroundSensors
   pair: the sketch and its page disagreeing about the shape of a report.

   The sketch streams one bundle per report — /state plus one message per
   capability — and the page unpacks each message by address and by position.
   Adding an argument to a message in one file and not the other turns a
   reading into a different reading; renaming an address in one file and not
   the other makes a reading vanish. Both are silent, because the bytes still
   parse. The page guards arity at runtime, which catches an added argument
   but not a reordered or retyped one, and only once a board is plugged in.

   So this checks, without hardware and without a browser, that four
   independent statements of the same layout agree:

     1. the STREAM table in the sketch's header comment   (what a reader is told)
     2. the add() chains in addReadings() and send()      (what actually goes out)
     3. the STREAM table in the page                      (what gets unpacked)
     4. ADDRESSES.md                                      (the contract)

   and then that the page's codec really does decode a report bundle and a
   enq bundle in that layout, and really does encode the control messages
   the sketch routes — /rgb/<n>, /rgb/pixels, /rgb/bright, /s/l, /buzz.

   Two contract rules are pinned here as well, because both fail silently and
   neither is visible to test-namespace.mjs, which only reads address literals:

     * nothing rides in the hello that the hello did not announce. /diag is a
       capability like any other — ADDRESSES.md: "Announced by /enq/diag like
       anything else" — and an unannounced line leaves a page that reads /enq
       unable to expect it.
     * /rate 0 stops the stream. A change-driven sketch is free to treat a
       non-zero /rate as a floor rather than a period, but if 0 means "no
       floor" then a generic page asking for silence gets the fastest stream
       the board can produce, which is the opposite of what it asked for.

   These two files are hand-written, so `make check` — which only ever looks at
   the boards.json-generated pairs — cannot see them at all.

   Run: node test/test-cpx-contract.mjs   (or `make test`). No dependencies. */

import { readFileSync } from "node:fs";

const DIR = new URL("../../../examples/CircuitPlaygroundSensors/", import.meta.url);
const ino = readFileSync(new URL("CircuitPlaygroundSensors.ino", DIR), "utf8");
const html = readFileSync(new URL("CircuitPlaygroundSensors.html", DIR), "utf8");
const contract = readFileSync(new URL("../../../ADDRESSES.md", import.meta.url), "utf8");

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};
const eq = (label, a, b) => ok(label, JSON.stringify(a) === JSON.stringify(b),
                               `${JSON.stringify(a)}\n       != ${JSON.stringify(b)}`);

/* ---------- 1. the header's STREAM table: lines like " *     /btn    iii   ..." */
const streamAt = ino.indexOf("STREAM");
const streamEnd = ino.indexOf("\n *\n", streamAt);
const streamBlock = streamAt >= 0 && streamEnd > streamAt ? ino.slice(streamAt, streamEnd) : "";
const table = [...streamBlock.matchAll(/^ \*\s+(\/[a-z]+)\s+([ifs]+)\s{2,}/gm)]
  .map(m => ({ address: m[1], tags: m[2] }));

ok("header STREAM table found", table.length > 0);
ok("the report starts with /state", table[0] && table[0].address === "/state");
eq("/state is sequence then millis", table[0] && table[0].tags, "ii");
ok("header table has no duplicate address",
   new Set(table.map(r => r.address)).size === table.length);

const touchCount = +(ino.match(/TOUCH_COUNT\s*=\s*(\d+)/) || [])[1];
const buttonCount = +(ino.match(/BUTTON_COUNT\s*=\s*(\d+)/) || [])[1];
const pixelsIno = +(ino.match(/#define NEOPIXEL_NUM (\d+)/) || [])[1];
ok("TOUCH_COUNT parsed", touchCount > 0);
ok("BUTTON_COUNT parsed", buttonCount > 0);
ok("sketch declares a NeoPixel count", pixelsIno > 0);

/* ---------- 2. what the sketch actually adds, per address, in order.

   send() opens the bundle with /state; addReadings() adds the rest. A float
   argument is one whose expression carries a float: the thermistor conversion
   or the milli-g scaling. Everything else is an int32. /cap is a loop, one int
   per touch pad. */
const outAt = ino.indexOf("static void addReadings(");
const outEnd = ino.indexOf("/* ---", outAt);
const outbound = ino.slice(outAt, outEnd);
ok("addReadings() and send() found", outAt >= 0 && outEnd > outAt);

// Each add("/x") followed by whatever its statement adds, up to the ';'.
const actual = [];
for (const m of outbound.matchAll(/add\("(\/[a-z]+)"\)([^;]*);/g)) {
  const address = m[1], chain = m[2];
  let tags = [...chain.matchAll(/\.add\(([^)]*(?:\([^)]*\))?[^)]*)\)/g)]
    .map(a => /thermistorC|\d\.\d+f/.test(a[1]) ? "f" : "i").join("");
  // OSCMessage &m = b.add("/cap"); for (...) m.add(s.touch[i]);
  if (!tags) {
    const after = outbound.slice(m.index + m[0].length, m.index + m[0].length + 120);
    if (/for\s*\([^)]*TOUCH_COUNT[^)]*\)\s*m\.add\(s\.touch\[i\]\)/.test(after))
      tags = "i".repeat(touchCount);
  }
  actual.push({ address, tags });
}
// send() is below addReadings() in the file; /state must lead the report.
const state = actual.find(r => r.address === "/state");
const readings = actual.filter(r => r.address !== "/state");
eq("the sketch's add() chains match the header table, address for address, tag for tag",
   [state, ...readings], table);

// The header says /mic and /imu are conditional; the code must agree. The
// guard is the `if (...)` on the line or two before the add().
const guard = addr => outbound.slice(Math.max(0, outbound.indexOf(`add("${addr}")`) - 120),
                                     outbound.indexOf(`add("${addr}")`));
ok("/mic is sent only when the PDM driver came up", /micReady/.test(guard("/mic")), guard("/mic"));
ok("/imu is sent only when the accelerometer answered", /haveAccel/.test(guard("/imu")), guard("/imu"));
ok("/btn carries BUTTON_COUNT values",
   (table.find(r => r.address === "/btn") || {}).tags === "i".repeat(buttonCount));
ok("/mic is scaled onto the contract's full scale", /MIC_SCALE/.test(outbound) &&
   /MIC_SCALE\s*=\s*32\b/.test(ino));

/* ---------- 3. the hello: one /enq per capability the stream carries */
const helloAt = ino.indexOf("static void sayHello(");
const helloBody = ino.slice(helloAt, ino.indexOf("\n}", helloAt));
ok("sayHello() found", helloAt >= 0);
ok("hello leads with /enq and the sketch name",
   /add\("\/enq"\)\.add\("CircuitPlaygroundSensors"\)/.test(helloBody));
// The parameter chain is `.add((int32_t) PIXEL_COUNT)`: one level of nested
// parentheses, which a plain [^)]* would stop inside.
const enq = [...helloBody.matchAll(/add\("\/enq\/([a-z]+)"\)((?:\.add\((?:[^()]|\([^()]*\))*\))*)/g)]
  .map(m => ({ name: m[1], params: [...m[2].matchAll(/\.add\(\(int32_t\)\s*(\w+)\)/g)].map(p => p[1]) }));
const enqNames = enq.map(e => e.name);
for (const r of table) if (r.address !== "/state")
  ok(`the stream's ${r.address} is announced by /enq/${r.address.slice(1)}`,
     enqNames.includes(r.address.slice(1)));
ok("the pixels are announced by /enq/rgb with PIXEL_COUNT",
   JSON.stringify((enq.find(e => e.name === "rgb") || {}).params) === '["PIXEL_COUNT"]');
ok("the pads are announced by /enq/cap with TOUCH_COUNT",
   JSON.stringify((enq.find(e => e.name === "cap") || {}).params) === '["TOUCH_COUNT"]');
ok("the buttons are announced by /enq/btn with BUTTON_COUNT",
   JSON.stringify((enq.find(e => e.name === "btn") || {}).params) === '["BUTTON_COUNT"]');
ok("the speaker is announced by /enq/buzz", enqNames.includes("buzz"));
// ADDRESSES.md's diag row: free text, "Announced by /enq/diag like anything
// else". So nothing may ride in the hello that the hello did not announce.
// This is the general rule, not a /diag special case: any future line added to
// sayHello() without its /enq is caught here. (add("/enq/rgb") does not match —
// the pattern wants the closing quote straight after the letters.)
const helloExtras = [...new Set([...helloBody.matchAll(/add\("\/([a-z]+)"\)/g)].map(m => m[1]))]
  .filter(a => a !== "enq");     // the bare /enq is the board's name, not a capability
for (const a of helloExtras)
  ok(`the hello's /${a} is announced by /enq/${a}`, enqNames.includes(a),
     `announced: ${enqNames.join(" ")}`);
ok("the hello carries the /diag free text", helloExtras.includes("diag"));
ok("/enq/diag carries no parameters",
   JSON.stringify((enq.find(e => e.name === "diag") || {}).params) === "[]");
ok("/enq/imu says three axes", /add\("\/enq\/imu"\)\.add\(\(int32_t\)\s*3\)/.test(helloBody));
ok("/enq/imu is conditional on the accelerometer", /haveAccel\)\s*bundleOUT\.add\("\/enq\/imu"/.test(helloBody));
ok("/enq/mic is conditional on the microphone", /micReady\)\s*bundleOUT\.add\("\/enq\/mic"/.test(helloBody));
ok("no boolean rides in /enq", !/add\("\/enq"\)[^;]*\.add\((?:true|false|haveAccel|micReady)/.test(helloBody));

/* ---------- 4. the page's STREAM table */
const streamSrc = html.match(/const STREAM = \{([\s\S]*?)\n\};/);
ok("page declares STREAM", !!streamSrc);
const pageStream = streamSrc ? [...streamSrc[1].matchAll(/^\s*"(\/[a-z]+)":/gm)].map(m => m[1]) : [];
eq("page STREAM matches the sketch's header table, address for address, in order",
   pageStream, table.map(r => r.address));

const aritySrc = html.match(/const STREAM_ARITY = \{([^}]*)\}/);
ok("page declares STREAM_ARITY", !!aritySrc);
const pageArity = aritySrc
  ? Object.fromEntries([...aritySrc[1].matchAll(/"(\/[a-z]+)":\s*(\d+)/g)].map(m => [m[1], +m[2]])) : {};
for (const [addr, n] of Object.entries(pageArity)) {
  const row = table.find(r => r.address === addr);
  ok(`page arity for ${addr} matches the header (${n})`, row && row.tags.length === n,
     row ? `${row.tags.length} in the header` : "not in the header");
}

/* ---------- 5. every address the page sends is one the sketch handles.

   dispatch() is an exact match; route() takes a root and the handler looks at
   the rest: nothing (/rgb), a literal it fullMatch()es (/rgb/pixels, /s/l), or
   a digit it parses (/rgb/<n>, which the page spells "/rgb/" + i). */
const dispatched = new Set([...ino.matchAll(/dispatch\("([^"]+)"/g)].map(m => m[1]));
const routed = new Set([...ino.matchAll(/route\("([^"]+)"/g)].map(m => m[1]));
const fragments = new Set([...ino.matchAll(/fullMatch\("([^"]+)"/g)].map(m => m[1]));
const sent = new Set([...html.matchAll(/send\("(\/[^"]+)"/g)].map(m => m[1]));
ok("the page sends something", sent.size > 0);
for (const a of sent) {
  if (dispatched.has(a)) { ok(`sketch dispatches ${a}`, true); continue; }
  const root = a.match(/^\/[a-z]+/)[0], rest = a.slice(root.length);
  const handled = routed.has(root) && (
    rest === "" ||
    (rest === "/" && /strtol\(rest \+ 1/.test(ino)) ||       // "/rgb/" + i
    fragments.has(rest));
  ok(`sketch routes ${a}${rest === "/" ? "<n>" : ""}`, handled,
     `sketch dispatches ${[...dispatched].join(" ")}; routes ${[...routed].join(" ")}`);
}
ok("the page asks for /enq on connect", sent.has("/enq"));

/* /rate is a core address, so it has to mean what the core table means by it:
   "streaming period in ms; 0 stops". This sketch is change-driven, so a
   non-zero value is a floor on the gap between reports rather than a fixed
   period — but 0 must stop the stream, not remove the floor, or a generic
   contract-aware page asking for silence gets the fastest stream instead. */
ok("/rate 0 stops the stream in the sketch", /reportInterval == 0\)\s*return;/.test(ino));
ok("the sketch says so where the guard is", /0 stops the stream/.test(ino));
{
  const sel = html.slice(html.indexOf('id="rate"'));
  ok("the page's /rate 0 is labelled stop", /<option value="0">stop<\/option>/.test(sel),
     (sel.match(/<option value="0">[^<]*/) || [])[0]);
  ok("the page still offers the old no-floor behaviour as a 1 ms floor",
     /<option value="1">/.test(sel.slice(0, sel.indexOf("</select>"))));
}

/* ---------- 6. every address root either file uses is in ADDRESSES.md */
const contractRoots = new Set([...contract.matchAll(/`\/([a-z]+)/g)].map(m => m[1]));
ok("ADDRESSES.md parsed", contractRoots.has("enq") && contractRoots.has("state"));
const stripFragments = s => s.replace(/\b(?:fullMatch|match)\(\s*"\/[^"]*"/g, "");
const rootsIn = s => new Set([...stripFragments(s).matchAll(/["']\/([A-Za-z][A-Za-z0-9_]*)(?:\/[^"'\s]*)?["']/g)]
  .map(m => m[1]));
for (const [name, text] of [["sketch", ino], ["page", html]]) {
  const outside = [...rootsIn(text)].filter(r => !contractRoots.has(r));
  ok(`every address the ${name} quotes is in ADDRESSES.md`, outside.length === 0, outside.join(" "));
}
// The addresses this pair retired must not come back as literals. A
// fullMatch("/pixels", off) fragment under the /rgb root is not one — it is
// the tail of /rgb/pixels — so those call sites are stripped first, exactly
// as test-namespace.mjs strips them.
const retired = /["']\/(cpx|pix|pixels|bright|led|tone)\b/;
for (const [name, text] of [["sketch", stripFragments(ino)], ["page", stripFragments(html)]])
  ok(`no retired address is quoted in the ${name}`, !retired.test(text), (text.match(retired) || [])[0]);

/* ---------- 7. the page's codec, evaluated out of the page itself */
const script = html.split(/<script>/)[1].split(/<\/script>/)[0];

// The whole script must parse, UI half included — new Function compiles without
// running, so document and navigator being absent here does not matter. Without
// this, a syntax error below the codec would only show up in a browser.
{
  let why = "";
  try { new Function(script); } catch (e) { why = e.message; }
  ok("the page's whole script parses", !why, why);
}

// Every element the script reaches for must exist in the markup. A typo'd id is
// otherwise silent until the moment a board is connected.
{
  const ids = new Set([...html.matchAll(/id="([^"]+)"/g)].map(m => m[1]));
  // ids the script creates at runtime, however it spells the assignment
  const built = new Set();
  for (const m of script.matchAll(/(?:setAttribute\("id",|\bid:)\s*"(\w+)"\s*\+/g)) built.add(m[1]);
  const missing = [...new Set([...script.matchAll(/\$\("([A-Za-z_]\w*)"\)/g)].map(m => m[1]))]
    .filter(id => !ids.has(id));
  ok("every $(...) id exists in the markup", missing.length === 0, missing.join(" "));
  for (const p of ["pix", "pad"])
    ok(`ids beginning "${p}" are created by the script`, built.has(p));

  // the geometry tables and the counts derived from them must stay in step
  const pixRows = (script.match(/const PIXEL_XY = \[([\s\S]*?)\];/) || ["", ""])[1];
  const padRows = (script.match(/const PAD_XY = \[([\s\S]*?)\];/) || ["", ""])[1];
  const nPix = (pixRows.match(/\[\s*\d+\s*,\s*\d+\s*\]/g) || []).length;
  const nPad = (padRows.match(/\[\s*\d+\s*,\s*\d+\s*\]/g) || []).length;
  ok("PIXEL_XY has one coordinate per NeoPixel", nPix === pixelsIno, `${nPix} != ${pixelsIno}`);
  ok("PAD_XY has one coordinate per touch pad", nPad === touchCount, `${nPad} != ${touchCount}`);
  ok("the page derives its counts from those tables rather than repeating them",
     /PIXELS = PIXEL_XY\.length/.test(script) && /PADS = PAD_XY\.length/.test(script));
}

// Sections 1–5 are DOM-free by design: the codec, SLIP, the baselines and the
// stream unpacking. Everything from "6. State display" on touches the document.
const codec = script.split("/* ============================================================\n   6. State display")[0];
ok("the DOM-free half of the script was found", codec.length > 0 && codec.includes("function unpackStream"));
const { encodeMessage, decodePacket, messagesOf, unpackStream, unpackHello,
        slipEncode, SlipDecoder, PadBaseline, angleGap, STREAM } = new Function(
  codec + "\nreturn { encodeMessage, decodePacket, messagesOf, unpackStream, unpackHello, " +
          "slipEncode, SlipDecoder, PadBaseline, angleGap, STREAM };")();
eq("the evaluated STREAM has the same keys as the source", Object.keys(STREAM), pageStream);

/* angleGap: the shortest way round between two bearings. The version this
   replaced took a remainder of a negative number and so returned more than pi
   for the far side of the ring, lighting the wrong pixel in the tilt demo. */
{
  const P = Math.PI;
  const close = (a, b) => Math.abs(a - b) < 1e-9;
  ok("angleGap: identical bearings", close(angleGap(1, 1), 0));
  ok("angleGap: never exceeds pi", [...Array(400).keys()]
      .every(k => { const g = angleGap(-8 + k * 0.05, 0.7); return g >= 0 && g <= P + 1e-9; }));
  ok("angleGap: wraps the short way", close(angleGap(0.1, 2 * P - 0.1), 0.2));
  ok("angleGap: works for the negative bearings the ring actually uses",
     close(angleGap(-7.5, -7.5 + 0.3), 0.3));
}

/* ---------- 8. a report bundle, built by hand to the header's layout */
const enc = new TextEncoder();
const ostr = s => { const b = enc.encode(s + "\0"); const p = (4 - b.length % 4) % 4;
                    return [...b, ...new Uint8Array(p)]; };
const be32 = (v, f) => { const b = new Uint8Array(4), dv = new DataView(b.buffer);
                         if (f) dv.setFloat32(0, v); else dv.setInt32(0, v); return [...b]; };
// A typed message: tags is the OSC type tag string, values in order.
function message(address, tags, values) {
  const out = [...ostr(address), ...ostr("," + tags)];
  values.forEach((v, i) => {
    if (tags[i] === "s") out.push(...ostr(v)); else out.push(...be32(v, tags[i] === "f"));
  });
  return out;
}
function bundle(messages) {
  const out = [...ostr("#bundle"), 0, 0, 0, 0, 0, 0, 0, 1];
  for (const m of messages) out.push(...be32(m.length), ...m);
  return Uint8Array.from(out);
}

// Which field of the page's sample each argument of each message lands in.
const FIELD = {
  "/state": ["seq", "millis"], "/btn": ["buttonA", "buttonB", "slide"],
  "/light": ["light"], "/temp": ["tempC"], "/mic": ["sound", "peak"],
  "/imu": ["accelX", "accelY", "accelZ"], "/cap": ["touch"],
};
eq("the test's own field map covers the header's addresses",
   Object.keys(FIELD).sort(), table.map(r => r.address).sort());

// distinct values so a transposition cannot pass by coincidence
let n = 0;
const values = table.map(r => [...r.tags].map(t => t === "f" ? (++n) * 0.5 : (++n) * 7));
const report = bundle(table.map((r, i) => message(r.address, r.tags, values[i])));

const full = unpackStream(messagesOf(decodePacket(report)));
ok("a hand-built report unpacks", !!full);
if (full) {
  ok("nothing in it had the wrong arity", full.bad.length === 0, full.bad.join(" "));
  table.forEach((r, i) => {
    const want = values[i];
    if (r.address === "/cap") eq("/cap lands in touch[], every pad", full.touch, want);
    else FIELD[r.address].forEach((f, k) =>
      ok(`${r.address} argument ${k} lands in ${f}`, full[f] === want[k], `${full[f]} != ${want[k]}`));
  });
  ok("the raw table sees every message, with its tags",
     full.messages.length === table.length && full.messages.every((m, i) => m.tags === table[i].tags));
}

// The optional capabilities are absent, not zero.
const bare = bundle(table.filter(r => r.address !== "/mic" && r.address !== "/imu")
  .map(r => message(r.address, r.tags, [...r.tags].map(() => 1))));
const partial = unpackStream(messagesOf(decodePacket(bare)));
ok("a report without /mic and /imu unpacks with null for both",
   partial && partial.sound === null && partial.accelX === null && partial.touch.length === touchCount);
ok("a packet with no /state is not a report",
   unpackStream(messagesOf(decodePacket(Uint8Array.from(message("/light", "i", [5]))))) === null);
ok("a message with the wrong arity is flagged, not unpacked",
   (() => { const s = unpackStream(messagesOf(decodePacket(bundle([
      message("/state", "ii", [1, 2]), message("/btn", "ii", [1, 1])]))));
      return s && s.bad.includes("/btn") && s.buttonA === undefined; })());

// The wire size of a report is what the page's buffer note claims.
const withoutMic = bundle(table.filter(r => r.address !== "/mic")
  .map(r => message(r.address, r.tags, [...r.tags].map(() => 0)))).length;
const withMic = report.length;
eq("a report without the microphone is 196 bytes on the wire", withoutMic, 196);
eq("a report with the microphone is 220 bytes on the wire", withMic, 220);
ok("the page's buffer note states those two numbers",
   /one report bundle is 196\b[\s\S]{0,80}220 with the microphone/.test(html));

/* ---------- 9. the enq bundle, built from what sayHello() announces */
const helloMsgs = [message("/enq", "s", ["CircuitPlaygroundSensors"])];
const counts = { PIXEL_COUNT: pixelsIno, BUTTON_COUNT: buttonCount, TOUCH_COUNT: touchCount };
for (const e of enq) {
  const params = e.name === "imu" ? [3] : e.params.map(p => counts[p]);
  helloMsgs.push(message("/enq/" + e.name, "i".repeat(params.length), params));
}
// The free text goes in only because /enq/diag announced it. Building it
// unconditionally is what let an unannounced /diag pass here before.
ok("this bundle carries /diag only because sayHello() announced it",
   enqNames.includes("diag"));
if (enqNames.includes("diag")) helloMsgs.push(message("/diag", "s", ["LIS3DH at 0x19"]));
const hello = unpackHello(messagesOf(decodePacket(bundle(helloMsgs))));
ok("the enq bundle unpacks", !!hello);
if (hello) {
  eq("hello carries the sketch name", hello.name, "CircuitPlaygroundSensors");
  eq("hello lists every /enq the sketch announces", Object.keys(hello.enq).sort(), enqNames.slice().sort());
  eq("/enq/rgb carries the pixel count", hello.enq.rgb, [pixelsIno]);
  eq("/enq/cap carries the pad count", hello.enq.cap, [touchCount]);
  eq("/enq/btn carries the button count", hello.enq.btn, [buttonCount]);
  eq("/enq/light has no parameters", hello.enq.light, []);
  eq("/enq/diag announces the free text and has no parameters", hello.enq.diag, []);
  eq("/diag comes through as text", hello.diag, ["LIS3DH at 0x19"]);
}
ok("a report is not mistaken for a hello", unpackHello(messagesOf(decodePacket(report))) === null);

/* ---------- 10. what the page says to the board */
const one = encodeMessage("/rgb/3", [255, 0, 128]);
eq("/rgb/<n> encodes to the OSC 1.0 bytes",
   [...one],
   [0x2f, 0x72, 0x67, 0x62, 0x2f, 0x33, 0x00, 0x00,      // "/rgb/3" + pad to 8
    0x2c, 0x69, 0x69, 0x69, 0x00, 0x00, 0x00, 0x00,      // ",iii" + pad to 8
    0, 0, 0, 255,  0, 0, 0, 0,  0, 0, 0, 128]);

// /rgb/pixels is the animation path: thirty ints in one packet. Eleven
// characters plus the terminator pad to 12; ",iii...i" (31) plus its
// terminator pads to 32; then 30 × 4 bytes of payload.
const all = encodeMessage("/rgb/pixels", new Array(30).fill(1));
ok("/rgb/pixels carries all ten pixels in one message",
   all.length === 12 + 32 + 120, `${all.length} bytes, expected ${12 + 32 + 120}`);
eq("/s/l 1 encodes as one int", [...encodeMessage("/s/l", [1])],
   [0x2f, 0x73, 0x2f, 0x6c, 0, 0, 0, 0,  0x2c, 0x69, 0, 0,  0, 0, 0, 1]);
eq("/buzz hz ms encodes as two ints", [...encodeMessage("/buzz", [440, 200])].length, 8 + 4 + 8);
eq("a control message round-trips through the page's own decoder",
   decodePacket(encodeMessage("/rgb/bright", [40])), { address: "/rgb/bright", tags: "i", args: [40] });

/* ---------- 11. the touch baseline, against numbers measured on a real board.

   An undisturbed Circuit Playground Express reads 196..275 on its pads and
   holds that to within a count. Across 45 seconds of being handled, touches
   reached 686..1014 and two pads dipped to 56 and 58. The first version of this
   tracker kept a running minimum, so one dip pinned the floor and left the pad
   reading fully touched for the rest of the session. These cases pin the fix. */
{
  const REST = 230, TOUCH = 1014, DIP = 56;
  const pads = new PadBaseline(1);
  const feed = (v, k = 1) => { let e = 0; for (let i = 0; i < k; i++) e = pads.update(0, v); return e; };

  feed(REST, 60);
  ok("a resting pad reads as untouched", Math.abs(feed(REST)) < 0.02);

  feed(DIP);                       // the glitch that broke the running minimum
  const afterDip = feed(REST, 3);
  ok("a one-sample dip does not disturb the baseline", Math.abs(afterDip) < 0.02,
     `excursion ${afterDip.toFixed(3)} after a dip to ${DIP}`);

  const touched = feed(TOUCH, 4);
  ok("a finger reads as touched", touched > 0.5, `excursion ${touched.toFixed(3)}`);

  feed(REST, 40);
  ok("releasing returns to untouched", Math.abs(feed(REST)) < 0.15,
     `excursion ${feed(REST).toFixed(3)}`);

  // A sustained lower level is a real change and must be adopted, unlike a dip.
  const lower = new PadBaseline(1);
  for (let k = 0; k < 30; k++) lower.update(0, REST);
  for (let k = 0; k < 30; k++) lower.update(0, 200);
  ok("a sustained lower resting level is adopted",
     Math.abs(lower.update(0, 200)) < 0.05, `excursion ${lower.update(0, 200).toFixed(3)}`);
}

// SLIP: a frame containing an END byte must survive the round trip.
const framed = slipEncode(Uint8Array.from([0x01, 0xC0, 0xDB, 0x02]));
const sd = new SlipDecoder();
const got = [];
sd.transform(framed, { enqueue: v => got.push([...v]) });
eq("SLIP round-trips a payload containing END and ESC", got, [[0x01, 0xC0, 0xDB, 0x02]]);

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);

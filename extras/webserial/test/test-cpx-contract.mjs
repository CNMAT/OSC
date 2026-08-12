/* The one thing that can quietly break the hand-written CircuitPlaygroundSensors
   pair: the sketch and its page disagreeing about /cpx.

   The sketch builds one positional message and the page unpacks it by index, so
   inserting an argument in one file and not the other turns every reading after
   that point into a different reading — silently, because the bytes still parse.
   The page guards the count at runtime, which catches an added argument but not
   a reordered or retyped one, and only once a board is plugged in.

   So this checks, without hardware and without a browser, that four independent
   statements of the same layout agree:

     1. the table in the sketch's header comment      (what a reader is told)
     2. the type tag string in that header            (what a receiver is told)
     3. the add() chain in send()                     (what actually goes out)
     4. the FIELDS array in the page                  (what gets unpacked)

   and then that the page's codec really does decode a packet in that layout,
   and really does encode the control messages the sketch dispatches on.

   These two files are hand-written, so `make check` — which only ever looks at
   the boards.json-generated pairs — cannot see them at all.

   Run: node test/test-cpx-contract.mjs   (or `make test`). No dependencies. */

import { readFileSync } from "node:fs";

const DIR = new URL("../../../examples/CircuitPlaygroundSensors/", import.meta.url);
const ino = readFileSync(new URL("CircuitPlaygroundSensors.ino", DIR), "utf8");
const html = readFileSync(new URL("CircuitPlaygroundSensors.html", DIR), "utf8");

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};
const eq = (label, a, b) => ok(label, JSON.stringify(a) === JSON.stringify(b),
                               `${JSON.stringify(a)}\n       != ${JSON.stringify(b)}`);

/* ---------- 1. the header table: lines like " *    7  accelX    f  g, ..." */
const table = [...ino.matchAll(/^ \*\s+(\d+)\s+(\w+)\s+([ifs])\s+\S/gm)]
  .map(m => ({ index: +m[1], name: m[2], type: m[3] }));

ok("header table found", table.length > 0);
eq("header table is indexed 0..n-1", table.map(r => r.index),
   table.map((_, i) => i));

/* ---------- 2. the type tag string in the header */
const tagLine = ino.match(/\/cpx\s+,([ifs]+)\s+<(\d+) args>/);
ok("header states a tag string and an argument count", !!tagLine);
const declaredTags = tagLine ? tagLine[1] : "";
const declaredCount = tagLine ? +tagLine[2] : -1;

eq("header tag string matches the header table", declaredTags,
   table.map(r => r.type).join(""));
ok("header argument count matches the table", declaredCount === table.length,
   `${declaredCount} != ${table.length}`);

const argCount = +(ino.match(/ARG_COUNT\s*=\s*(\d+)/) || [])[1];
ok("ARG_COUNT constant matches the table", argCount === table.length,
   `${argCount} != ${table.length}`);

/* ---------- 3. what send() actually adds, in order.

   A float argument is one whose expression carries a float: the thermistor
   conversion, or the milli-g scaling. Everything else is an int32. The trailing
   loop adds one int per touch pad. */
const sendAt = ino.indexOf("static void send(const Sample &s)");
const body = ino.slice(sendAt, ino.indexOf("SLIPSerial.beginPacket();", sendAt));
// The straight-line chain stops where the touch loop begins; counting the
// loop's own .add() as well as the pads it stands for would double one of them.
const loopAt = body.indexOf("for (");
const chain = loopAt < 0 ? body : body.slice(0, loopAt);
const actual = [...chain.matchAll(/\.add\(([^)]*(?:\([^)]*\))?[^)]*)\)/g)]
  .map(m => /thermistorC|f\b|\d\.\d/.test(m[1]) ? "f" : "i");

const touchCount = +(ino.match(/TOUCH_COUNT\s*=\s*(\d+)/) || [])[1];
ok("TOUCH_COUNT parsed", touchCount > 0);
const loopAdds = /for\s*\([^)]*\)\s*msgOut\.add\(s\.touch\[i\]\)/.test(body);
ok("send() adds one argument per touch pad in a loop", loopAdds);
const actualTags = actual.join("") + "i".repeat(loopAdds ? touchCount : 0);

eq("the add() chain matches the declared tag string", actualTags, declaredTags);

/* ---------- 4. the page's FIELDS array */
const fieldsSrc = html.match(/const FIELDS = \[([\s\S]*?)\];/);
ok("page declares FIELDS", !!fieldsSrc);
const FIELDS = fieldsSrc
  ? [...fieldsSrc[1].matchAll(/"([^"]+)"/g)].map(m => m[1]) : [];

eq("page FIELDS matches the sketch's header table, name for name",
   FIELDS, table.map(r => r.name));

// The page's counts come from its geometry tables, which section 8 checks
// against the sketch; here we only need the sketch's own number.
const pixelsIno = +(ino.match(/#define NEOPIXEL_NUM (\d+)/) || [])[1];
ok("sketch declares a NeoPixel count", pixelsIno > 0);

/* ---------- 5. every address the page sends is one the sketch dispatches */
const dispatched = new Set([...ino.matchAll(/dispatch\("([^"]+)"/g)].map(m => m[1]));
const sent = new Set([...html.matchAll(/send\("(\/[^"]+)"/g)].map(m => m[1]));
for (const a of sent)
  ok(`sketch dispatches ${a}`, dispatched.has(a),
     `page sends ${a}; sketch handles ${[...dispatched].join(" ")}`);

/* ---------- 6. the page's codec, evaluated out of the page itself */
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
  const count = (re) => (script.match(re) || []).length;
  const pixRows = (script.match(/const PIXEL_XY = \[([\s\S]*?)\];/) || ["", ""])[1];
  const padRows = (script.match(/const PAD_XY = \[([\s\S]*?)\];/) || ["", ""])[1];
  const nPix = (pixRows.match(/\[\s*\d+\s*,\s*\d+\s*\]/g) || []).length;
  const nPad = (padRows.match(/\[\s*\d+\s*,\s*\d+\s*\]/g) || []).length;
  ok("PIXEL_XY has one coordinate per NeoPixel", nPix === pixelsIno, `${nPix} != ${pixelsIno}`);
  ok("PAD_XY has one coordinate per touch pad", nPad === touchCount, `${nPad} != ${touchCount}`);
  ok("the page derives its counts from those tables rather than repeating them",
     /PIXELS = PIXEL_XY\.length/.test(script) && /PADS = PAD_XY\.length/.test(script));
}
const codec = script.split("/* ============================================================\n   5. State display")[0];
const { encodeMessage, decodeMessage, slipEncode, SlipDecoder, PadBaseline, angleGap } = new Function(
  codec + "\nreturn { encodeMessage, decodeMessage, slipEncode, SlipDecoder, PadBaseline, angleGap };")();

// The page formats the raw table by declared type, so its copy of the tag
// string has to be the sketch's.
const pageTypes = (html.match(/const FIELD_TYPES = "([if]+)"/) || [])[1];
eq("page FIELD_TYPES matches the sketch's tag string", pageTypes, declaredTags);

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

// Build a /cpx packet by hand, to the layout the sketch documents, and check the
// page pulls the right value out of every slot.
function buildCpx(values) {
  const enc = new TextEncoder();
  const ostr = s => { const b = enc.encode(s + "\0"); const p = (4 - b.length % 4) % 4;
                      return [...b, ...new Uint8Array(p)]; };
  const out = [...ostr("/cpx"), ...ostr("," + declaredTags)];
  values.forEach((v, i) => {
    const b = new Uint8Array(4), dv = new DataView(b.buffer);
    if (table[i].type === "f") dv.setFloat32(0, v); else dv.setInt32(0, v);
    out.push(...b);
  });
  return Uint8Array.from(out);
}

// distinct values so a transposition cannot pass by coincidence
const sample = table.map((r, i) => r.type === "f" ? (i + 1) * 0.5 : (i + 1) * 7);
const decoded = decodeMessage(buildCpx(sample));

eq("a hand-built /cpx decodes to the right address", decoded.address, "/cpx");
ok("all arguments come back", decoded.args.length === table.length,
   `${decoded.args.length} != ${table.length}`);
eq("every slot decodes to the value that was put in it", decoded.args, sample);

// The page must be able to say what the sketch is listening for.
const pix = encodeMessage("/pix", [3, 255, 0, 128]);
eq("/pix encodes to the OSC 1.0 bytes",
   [...pix],
   [0x2f, 0x70, 0x69, 0x78, 0x00, 0x00, 0x00, 0x00,      // "/pix" + pad to 8
    0x2c, 0x69, 0x69, 0x69, 0x69, 0x00, 0x00, 0x00,      // ",iiii" + pad to 8
    0, 0, 0, 3,  0, 0, 0, 255,  0, 0, 0, 0,  0, 0, 0, 128]);

// /pixels is the animation path: thirty ints in one packet. "/pixels" is seven
// characters, so its terminator lands exactly on the four-byte boundary and the
// address costs 8 bytes rather than 12.
const all = encodeMessage("/pixels", new Array(30).fill(1));
ok("/pixels carries all ten pixels in one message",
   all.length === 8 + 32 + 120, `${all.length} bytes, expected ${8 + 32 + 120}`);

/* ---------- 7. the touch baseline, against numbers measured on a real board.

   An undisturbed Circuit Playground Express reads 196..275 on its pads and
   holds that to within a count. Across 45 seconds of being handled, touches
   reached 686..1014 and two pads dipped to 56 and 58. The first version of this
   tracker kept a running minimum, so one dip pinned the floor and left the pad
   reading fully touched for the rest of the session. These cases pin the fix. */
{
  const REST = 230, TOUCH = 1014, DIP = 56;
  const pads = new PadBaseline(1);
  const feed = (v, n = 1) => { let e = 0; for (let k = 0; k < n; k++) e = pads.update(0, v); return e; };

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

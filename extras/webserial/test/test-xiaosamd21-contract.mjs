/* The hand-written XiaoSamd21Oscuino pair, held to one parameter list.

   The sketch routes six /cap/<p> set-up addresses, the page sends and tabulates
   the same six, and ADDRESSES.md documents them under "cap set-up". A name that
   moves in one place and not the others is silent: the board answers nothing
   for an address it does not route, and the page shows "–" for a parameter it
   never hears. `make check` cannot see this pair, because it is not generated
   from boards.json, so this checks, with no board and no browser:

     1. the parameter list: the sketch's PARAM_ADDR table, the sketch header's
        set-up table, the page's PARAMS and ADDRESSES.md's "cap set-up" table
        name the same parameters in the same order, and the sketch reaches
        them through route("/cap"), not an exact-match dispatch
     2. the page's codec on bytes captured from the board on 2026-10-28
        (xiaosamd21-2026-10-28.slip: /enq, /cap/config, one /cap/3/intcap 20
        echo, /rate 20, then a second of stream): every frame decodes, the
        hello names the sketch with /enq/cap 7, every stream bundle carries
        /state and seven /cap ints with consecutive sequence numbers, and the
        /cap/config reply carries every parameter for every pad, then /cap/pins
     3. the page's DEFAULTS equal what that board reported from /cap/config at
        its defaults; the page's orange marks compare against them
     4. every pin in /cap/pins has a fader in the page's PINS table (a row with
        a Y line), and the page draws exactly as many faders as /enq/cap says
     5. the page encodes a set-up request to the same bytes as
        test/hardware/oscprobe.py, whose bytes the board accepted

   Graded by test/mutants-xiaosamd21.mjs: nine mutants, one per claim above,
   and each must fail this test.

   Run: node test/test-xiaosamd21-contract.mjs   (or `make test`). No dependencies. */

import { readFileSync } from "node:fs";

const ROOT = new URL("../../../", import.meta.url);
const read = p => readFileSync(new URL(p, ROOT), "utf8");
const ino = read("examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino");
const html = read("examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.html");
const contract = read("ADDRESSES.md");
const capture = new Uint8Array(readFileSync(new URL("xiaosamd21-2026-10-28.slip", import.meta.url)));

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
const PARAMS = constOf("PARAMS"), DEFAULTS = constOf("DEFAULTS"), PINS = constOf("PINS");

/* ---------- 1. one parameter list in four places */
console.log("\nThe set-up parameters");
const sketchParams = [...((ino.match(/PARAM_ADDR\[P_COUNT\]\s*=\s*\{([^}]*)\}/) || [])[1] || "")
  .matchAll(/"\/cap\/([a-z]+)"/g)].map(m => m[1]);
const headerAt = ino.indexOf(" *   p           values");
const headerBlock = headerAt < 0 ? "" : ino.slice(headerAt, ino.indexOf("\n *\n", headerAt));
const headerParams = [...headerBlock.matchAll(/^ \*   ([a-z]+)\s{2,}/gm)].map(m => m[1]).filter(p => p !== "p");
const setupAt = contract.indexOf("### cap set-up");
const setupBlock = setupAt < 0 ? "" : contract.slice(setupAt, contract.indexOf("\n## ", setupAt));
const contractParams = [...setupBlock.matchAll(/^\| `([a-z]+)` \|/gm)].map(m => m[1]);

ok("sketch PARAM_ADDR parsed", sketchParams.length === 6, sketchParams.join(" "));
eq("the sketch header's table lists the routed parameters", headerParams, sketchParams);
eq("the page's PARAMS are the routed parameters", PARAMS, sketchParams);
eq("ADDRESSES.md's cap set-up table is the routed parameters", contractParams, sketchParams);
ok("the sketch routes /cap with route(), so sub-addresses reach it",
   /\.route\(\s*"\/cap"\s*,\s*routeCap\)/.test(ino) && !/\.dispatch\(\s*"\/cap"/.test(ino));

/* ---------- 2. the page's codec on bytes from the board */
console.log("\nBytes captured from the board, through the page's codec");
const frames = [];
new page.SlipDecoder().transform(capture, { enqueue: f => frames.push(f) });
const decoded = [];
for (const f of frames) {
  try { decoded.push(page.messagesOf(page.decodePacket(f))); }
  catch (e) { decoded.push(null); }
}
ok("54 frames, as captured", frames.length === 54, String(frames.length));
ok("every frame decodes", decoded.every(Boolean));

const hello = decoded.find(ms => ms && ms.some(m => m.address === "/enq")) || [];
const enqArgs = a => (hello.find(m => m.address === a) || { args: [] }).args;
eq("the hello names the sketch", enqArgs("/enq"), ["XiaoSamd21Oscuino"]);
eq("the hello announces seven pads", enqArgs("/enq/cap"), [7]);
ok("the hello announces /diag and carries it",
   hello.some(m => m.address === "/enq/diag") && hello.some(m => m.address === "/diag"));

const reports = decoded.filter(ms => ms && ms.some(m => m.address === "/state"));
const seqs = reports.map(ms => ms.find(m => m.address === "/state").args[0]);
ok("50 stream bundles", reports.length === 50, String(reports.length));
ok("each carries /state <i> <i> and /cap with seven ints",
   reports.every(ms => {
     const st = ms.find(m => m.address === "/state"), cap = ms.find(m => m.address === "/cap");
     return st && st.tags === "ii" && cap && cap.tags === "iiiiiii";
   }));
ok("sequence numbers are consecutive", seqs.every((s, i) => i === 0 || s === seqs[i - 1] + 1),
   `${seqs[0]}..${seqs[seqs.length - 1]}`);

const config = decoded.find(ms => ms && ms.some(m => m.address === "/cap/pins")) || [];
eq("/cap/config answers every parameter, then /cap/pins",
   config.map(m => m.address), [...sketchParams.map(p => "/cap/" + p), "/cap/pins"]);
ok("one value per pad in every /cap/<p>",
   config.every(m => m.args.length === 7 && m.tags === "iiiiiii"));

/* ---------- 3. the page's defaults are the board's */
console.log("\nDefaults");
for (const p of sketchParams) {
  const m = config.find(x => x.address === "/cap/" + p);
  eq(`DEFAULTS.${p} is what the board reported`, Array(7).fill(DEFAULTS[p]), m ? m.args : null);
}
const echo = decoded.find(ms => ms && ms.length === 1 && ms[0].address === "/cap/intcap");
eq("/cap/3/intcap 20 was echoed on pad 3 only",
   echo && echo[0].args, [63, 63, 63, 20, 63, 63, 63]);

/* ---------- 4. the drawing has a fader for every pad */
console.log("\nThe drawing");
const pins = (config.find(m => m.address === "/cap/pins") || { args: [] }).args;
const faders = PINS.filter(r => r[5] !== null);
eq("the page draws as many faders as /enq/cap announces", faders.length, enqArgs("/enq/cap")[0]);
for (const pin of pins)
  ok(`pin ${pin} from /cap/pins has a fader`, faders.some(r => r[1] === pin));

/* ---------- 5. outbound bytes */
console.log("\nOutbound");
const hex = a => [...a].map(b => b.toString(16).padStart(2, "0")).join("");
// test/hardware/oscprobe.py msg('/cap/3/compcap', [4000]), the bytes capsetupprobe.py sent
eq("/cap/3/compcap 4000 encodes as oscprobe.py encodes it",
   hex(page.encodeMessage("/cap/3/compcap", [4000])),
   "2f6361702f332f636f6d7063617000002c69000000000fa0");
eq("an ask carries an empty type tag string",
   hex(page.encodeMessage("/cap/config", [])), "2f6361702f636f6e666967002c000000");

console.log(`\n${pass} passed, ${fail} failed\n`);
if (fail) process.exit(1);

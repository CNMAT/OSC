/* Checks every generated page as a page rather than as a codec:
     - its <script> parses (catches a board note or label that broke out of a
       string or an attribute during substitution)
     - the board identity block matches boards.json
     - the chips match boards.json
     - the port-filter logic picks the right requestPort() argument

   test-codec.mjs proves the OSC bytes are right; this proves the generated
   wrapper around them is right, for every board rather than one. The python
   firmware pairs get the same page checks; their .py side is only greppable
   from here — extras/python/test_host.py is what actually imports and runs it.

   Run: node test/test-pages.mjs   (or `make test`). No dependencies. */

import { readFileSync, existsSync } from "node:fs";
import { BOARDS, EXAMPLES_DIR, sketchName, outputs } from "../render.mjs";

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};

for (const board of BOARDS) {
  const name = sketchName(board);
  console.log(`\n${name}${board.firmware ? ` (${board.firmware})` : ""}`);

  const outs = outputs(board);
  const missing = outs.filter(o => !existsSync(o.url));
  if (missing.length) {
    ok("all generated files exist", false,
      "run `make generate` — missing: " + missing.map(o => o.rel).join(", "));
    continue;
  }
  ok(`all generated files exist (${outs.length})`, true);

  const html = readFileSync(outs.find(o => o.file.endsWith(".html")).url, "utf8");
  const codeOut = outs.find(o =>
    o.file.endsWith(".ino") || o.file === "main.py" || o.file === "code.py");
  const code = readFileSync(codeOut.url, "utf8");
  const script = html.split(/<script>/)[1].split(/<\/script>/)[0];

  // 1. The whole script must parse, UI section included. new Function compiles
  //    without running, so document/navigator being absent here does not matter.
  let parsed = true, why = "";
  try { new Function(script); } catch (e) { parsed = false; why = e.message; }
  ok("script parses", parsed, why);

  // 2. No placeholder survived substitution, in any generated file.
  for (const o of outs) {
    const body = readFileSync(o.url, "utf8");
    ok(`no unsubstituted {{...}} in ${o.file}`, !/\{\{\w+\}\}/.test(body),
      (body.match(/\{\{\w+\}\}/g) || []).join(" "));
  }

  // 3. Board identity — evaluate just section 0 and compare against boards.json.
  const identity = new Function(
    script.split("/* ============================================================\n   1. OSC")[0] +
    "return { BOARD_NAME, BOARD_FQBN, USB_FILTERS, NATIVE_USB };")();

  ok("BOARD_NAME matches", identity.BOARD_NAME === board.name,
    `${identity.BOARD_NAME} != ${board.name}`);
  ok("BOARD_FQBN matches", identity.BOARD_FQBN === board.fqbn,
    `${identity.BOARD_FQBN} != ${board.fqbn}`);
  ok("NATIVE_USB matches", identity.NATIVE_USB === !!board.nativeUSB);

  // usbProductId is optional — a vendor-only filter matches the whole
  // catalogue, which is how one CircuitPython entry covers every Adafruit id.
  const wantFilters = (board.usbFilters || []).map(f => {
    const o = { usbVendorId: Number(f.usbVendorId) };
    if (f.usbProductId !== undefined) o.usbProductId = Number(f.usbProductId);
    return o;
  });
  ok(`USB filters (${wantFilters.length})`,
    JSON.stringify(identity.USB_FILTERS) === JSON.stringify(wantFilters),
    `${JSON.stringify(identity.USB_FILTERS)} != ${JSON.stringify(wantFilters)}`);

  // Hex in boards.json must have survived as a number, not a string — a quoted
  // id silently matches nothing and the board never appears in the chooser.
  ok("filter ids are numbers",
    identity.USB_FILTERS.every(f =>
      typeof f.usbVendorId === "number" &&
      (f.usbProductId === undefined || typeof f.usbProductId === "number")));

  // 4. requestPort() argument: filtered normally, unfiltered when "show all
  //    ports" is ticked. Mirrors the expression in connect().
  const opts = (allPorts, F) => allPorts || !F.length ? {} : { filters: F };
  ok("filtered when allPorts off",
    JSON.stringify(opts(false, identity.USB_FILTERS)) ===
    JSON.stringify({ filters: wantFilters }));
  ok("unfiltered when allPorts on",
    JSON.stringify(opts(true, identity.USB_FILTERS)) === "{}");

  // 5. Chips carry the addresses boards.json asked for, in order.
  const chips = [...html.matchAll(/<button data-a="([^"]*)" data-g="([^"]*)">/g)]
    .map(m => ({ addr: m[1], args: m[2] }));
  ok(`chips (${board.chips.length})`,
    JSON.stringify(chips) === JSON.stringify(board.chips.map(c => ({ addr: c.addr, args: c.args }))),
    JSON.stringify(chips));

  // 6. The firmware names its own pair and reports the right hello address.
  //    The V1 deployment strips comments to fit the on-board compiler, taking
  //    the header's page reference with them — identity must still survive in
  //    the title line and the /hello payload.
  ok(`${codeOut.file} references its own page`, board.firmware === "microbit"
    ? code.includes(name)
    : code.includes(`${name}.html`));
  ok("hello identifies the board", board.firmware
    ? code.includes(`'/hello', '${name}'`)
    : code.includes(`.add("${name}")`));

  // 7. Pin clamp present exactly when boards.json asks for one (Arduino only:
  //    the python firmwares resolve pins by name at runtime, nothing to clamp).
  if (!board.firmware) {
    const clamped = code.includes("#define NUM_DIGITAL_PINS");
    ok(`pin clamp ${board.pinClamp ? "present" : "absent"}`, clamped === !!board.pinClamp);
  }
}

/* ------------------------------------------------------------------------
   Hand-written pages. Everything above is bounded by boards.json, which left
   the hand-written demos -- more of them than the generated ones by now --
   with no automated check at all: a page whose blob decoder was missing, or
   whose script had stopped parsing, passed `make test` in silence. This pass
   applies the checks that need no knowledge of the board:
     - <meta charset> is declared (em dashes render as mojibake without it)
     - every <script> parses
     - every id the script asks for exists in the markup
     - the OSC decoder handles blobs, whose absence once cost a waveform
       display that had never drawn
   ------------------------------------------------------------------------ */
import { readdirSync, statSync } from "node:fs";

const generated = new Set(BOARDS.map(sketchName));
const examplesRoot = new URL(".", EXAMPLES_DIR);
const handwritten = readdirSync(examplesRoot).filter(d => {
  if (generated.has(d)) return false;
  try { return existsSync(new URL(`${d}/${d}.html`, examplesRoot)); }
  catch { return false; }
});

for (const name of handwritten) {
  console.log(`\n${name} (hand-written)`);
  const html = readFileSync(new URL(`${name}/${name}.html`, examplesRoot), "utf8");

  ok("declares a charset", /<meta[^>]+charset/i.test(html));

  const scripts = [...html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)].map(m => m[1]);
  let parsed = true, why = "";
  for (const src of scripts) {
    try { new Function(src); } catch (e) { parsed = false; why = String(e); }
  }
  ok("scripts parse", parsed, why);

  // ids: every literal getElementById / $("...") target must exist
  const script = scripts.join("\n");
  const wanted = new Set(
    [...script.matchAll(/getElementById\(["']([\w-]+)["']\)/g),
     ...script.matchAll(/\$\(["']([\w-]+)["']\)/g)].map(m => m[1]));
  const have = new Set([...html.matchAll(/id=["']([\w-]+)["']/g)].map(m => m[1]));
  const missing = [...wanted].filter(id => !have.has(id));
  ok("every requested id exists", missing.length === 0, missing.join(", "));

  // decoder handles blobs, if the page decodes OSC at all
  if (/decodeMessage|function decode/.test(script))
    ok("decoder has a blob case", /['"]b['"]/.test(script));

  // a page that opens a serial port must cancel its reader on disconnect:
  // a pending read() holds the stream lock, port.close() throws on a locked
  // stream, and the browser then keeps the port open forever -- which blocks
  // every later flash of the board. Found the hard way on TDisplayS3Oscuino,
  // the one page that had dropped the cancel from the shared boilerplate.
  if (script.includes("requestPort"))
    ok("disconnect cancels the reader", script.includes(".cancel()"));
}

console.log(`\n${pass} passed, ${fail} failed\n`);
process.exit(fail ? 1 : 0);

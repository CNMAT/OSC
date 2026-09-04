/* Checks every generated page as a page rather than as a codec:
     - its <script> parses (catches a board note or label that broke out of a
       string or an attribute during substitution)
     - every id the script asks for exists in the markup
     - the board identity block matches boards.json: name, FQBN, USB filters,
       the default transport, and the bundle box's default
     - the chips match boards.json
     - the port-filter logic picks the right requestPort() argument
     - a hand-written sketch is left alone by the generator and still names
       its page and itself in /enq
     - the board-less oscuino.html is generated, unfiltered and chip-less

   test-codec.mjs proves the OSC bytes are right and test-panels.mjs the
   capability model; this proves the generated wrapper around them is right,
   for every board rather than one. The python firmware pairs get the same
   page checks; their .py side is only greppable from here —
   extras/python/test_host.py is what actually imports and runs it.

   Run: node test/test-pages.mjs   (or `make test`). No dependencies. */

import { readFileSync, existsSync, readdirSync } from "node:fs";
import { BOARDS, EXAMPLES_DIR, TRANSPORTS, sketchName, outputs, universalOutput } from "../render.mjs";

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};

const scriptOf = html => html.split(/<script>/)[1].split(/<\/script>/)[0];

// Section 0 of the page, evaluated on its own: the generated constants.
function identityOf(script) {
  return new Function(
    script.split("/* ============================================================\n   1. OSC")[0] +
    "return { BOARD_NAME, BOARD_FQBN, USB_FILTERS, NATIVE_USB, DEFAULT_TRANSPORT };")();
}

// Checks that need no knowledge of the board, applied to every page.
function pageChecks(html) {
  ok("declares a charset", /<meta[^>]+charset/i.test(html));
  ok("no unsubstituted {{...}}", !/\{\{\w+\}\}/.test(html), (html.match(/\{\{\w+\}\}/g) || []).join(" "));

  const scripts = [...html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)].map(m => m[1]);
  let parsed = true, why = "";
  for (const src of scripts) {
    // new Function compiles without running, so document/navigator being
    // absent here does not matter.
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

  // decoder handles blobs (a camera frame, a display frame), if the page
  // decodes OSC at all
  if (/decodeMessage|function decode/.test(script))
    ok("decoder has a blob case", /['"]b['"]/.test(script));

  // a page that opens a serial port must cancel its reader on disconnect:
  // a pending read() holds the stream lock, port.close() throws on a locked
  // stream, and the browser then keeps the port open forever -- which blocks
  // every later flash of the board. Found the hard way on TDisplayS3Oscuino,
  // the one page that had dropped the cancel from the shared boilerplate.
  if (script.includes("requestPort"))
    ok("disconnect cancels the reader", script.includes(".cancel()"));
  return script;
}

const generatedDirs = new Set();

for (const board of BOARDS) {
  const name = sketchName(board);
  generatedDirs.add(name);
  console.log(`\n${name}${board.firmware ? ` (${board.firmware})` : board.handwritten ? " (hand-written sketch)" : ""}`);

  const outs = outputs(board);
  const missing = outs.filter(o => !existsSync(o.url));
  if (missing.length) {
    ok("all generated files exist", false,
      "run `make generate` — missing: " + missing.map(o => o.rel).join(", "));
    continue;
  }
  ok(`all generated files exist (${outs.length})`, true);

  const htmlOut = outs.find(o => o.file.endsWith(".html"));
  const html = readFileSync(htmlOut.url, "utf8");
  const script = pageChecks(html);

  // 1. Where the sketch comes from. A hand-written sketch is never a generator
  //    output, and must already be there beside the page.
  let codeUrl;
  if (board.handwritten) {
    ok("generator writes only the page", outs.length === 1 && htmlOut.file === `${name}.html`,
      outs.map(o => o.file).join(", "));
    codeUrl = new URL(`${name}/${name}.ino`, EXAMPLES_DIR);
    ok("hand-written sketch is present", existsSync(codeUrl), `${name}/${name}.ino`);
    if (!existsSync(codeUrl)) continue;
  } else {
    const codeOut = outs.find(o =>
      o.file.endsWith(".ino") || o.file === "main.py" || o.file === "code.py");
    codeUrl = codeOut.url;
  }
  const code = readFileSync(codeUrl, "utf8");
  const codeFile = decodeURIComponent(String(codeUrl).split("/").pop());

  // 2. Board identity — evaluate just section 0 and compare against boards.json.
  const identity = identityOf(script);
  ok("BOARD_NAME matches", identity.BOARD_NAME === board.name,
    `${identity.BOARD_NAME} != ${board.name}`);
  ok("BOARD_FQBN matches", identity.BOARD_FQBN === board.fqbn,
    `${identity.BOARD_FQBN} != ${board.fqbn}`);
  ok("NATIVE_USB matches", identity.NATIVE_USB === !!board.nativeUSB);

  const wantTransport = board.transport || "serial";
  ok(`transport '${wantTransport}' is one the page offers`, wantTransport in TRANSPORTS);
  ok("DEFAULT_TRANSPORT matches", identity.DEFAULT_TRANSPORT === wantTransport,
    `${identity.DEFAULT_TRANSPORT} != ${wantTransport}`);
  ok("page offers all three transports",
    ['value="serial"', 'value="ble"', 'value="http"'].every(v => html.includes(v)));

  // The bundle box: ticked unless boards.json says the sketch reads bare
  // messages only.
  const bundleTicked = /id="bundle" checked/.test(html);
  ok(`bundle box ${board.bundles === false ? "unticked" : "ticked"} by default`,
    bundleTicked === (board.bundles !== false));

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

  // 3. requestPort() argument: filtered normally, unfiltered when "show all
  //    ports" is ticked. Mirrors the expression in serialTransport.connect().
  const opts = (allPorts, F) => allPorts || !F.length ? {} : { filters: F };
  ok("filtered when allPorts off",
    JSON.stringify(opts(false, identity.USB_FILTERS)) ===
    JSON.stringify(wantFilters.length ? { filters: wantFilters } : {}));
  ok("unfiltered when allPorts on",
    JSON.stringify(opts(true, identity.USB_FILTERS)) === "{}");

  // 4. Chips carry the addresses boards.json asked for, in order. The pin
  //    panel's own /s chips sit in a different block and are not counted.
  const chipBlock = html.split('id="addr"')[1] || "";
  const chips = [...chipBlock.matchAll(/<button data-a="([^"]*)" data-g="([^"]*)">/g)]
    .map(m => ({ addr: m[1], args: m[2] }));
  ok(`chips (${board.chips.length})`,
    JSON.stringify(chips) === JSON.stringify(board.chips.map(c => ({ addr: c.addr, args: c.args }))),
    JSON.stringify(chips));

  // 5. The firmware names its own page and reports the right hello name.
  //    The V1 deployment strips comments to fit the on-board compiler, taking
  //    the header's page reference with them — identity must still survive in
  //    the title line and the /enq payload.
  ok(`${codeFile} references its own page`, board.firmware === "microbit"
    ? code.includes(name)
    : code.includes(`${name}.html`));
  ok("hello identifies the sketch", board.firmware
    ? code.includes(`'/enq', '${name}'`)
    : code.includes(`.add("${name}")`));

  // 6. Pin clamp present exactly when boards.json asks for one (generated
  //    Arduino sketches only: the python firmwares resolve pins by name at
  //    runtime, and a hand-written sketch owns its own pin table).
  if (!board.firmware && !board.handwritten) {
    const clamped = code.includes("#define NUM_DIGITAL_PINS");
    ok(`pin clamp ${board.pinClamp ? "present" : "absent"}`, clamped === !!board.pinClamp);
  }
}

/* ------------------------------------------------------------------------
   The board-less page: same template, no chips, no filter, any transport.
   ------------------------------------------------------------------------ */
{
  console.log("\noscuino.html (universal)");
  const out = universalOutput();
  ok("exists", existsSync(out.url), "run `make generate`");
  if (existsSync(out.url)) {
    const html = readFileSync(out.url, "utf8");
    const script = pageChecks(html);
    const identity = identityOf(script);
    ok("no USB filter", identity.USB_FILTERS.length === 0);
    ok("no board chips", !/id="addr"[\s\S]*?<div class="chips">\s*<\/div>/.test(html) === false ||
      (html.split('id="addr"')[1] || "").match(/<button data-a=/g) === null);
    ok("default transport is serial", identity.DEFAULT_TRANSPORT === "serial");
    ok("bundle box unticked by default", !/id="bundle" checked/.test(html));
    ok("carries no FQBN", identity.BOARD_FQBN === "");
  }
}

/* ------------------------------------------------------------------------
   Hand-written pages. Everything above is bounded by boards.json, which left
   the remaining hand-written demos with no automated check at all: a page
   whose blob decoder was missing, or whose script had stopped parsing,
   passed `make test` in silence. This pass applies the board-agnostic checks.
   ------------------------------------------------------------------------ */
const examplesRoot = new URL(".", EXAMPLES_DIR);
const handwritten = readdirSync(examplesRoot).filter(d => {
  if (generatedDirs.has(d)) return false;
  try { return existsSync(new URL(`${d}/${d}.html`, examplesRoot)); }
  catch { return false; }
});

for (const name of handwritten) {
  console.log(`\n${name} (hand-written page)`);
  pageChecks(readFileSync(new URL(`${name}/${name}.html`, examplesRoot), "utf8"));
}

console.log(`\n${pass} passed, ${fail} failed\n`);
process.exit(fail ? 1 : 0);

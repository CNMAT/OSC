/* Checks every generated page as a page rather than as a codec:
     - its <script> parses (catches a board note or label that broke out of a
       string or an attribute during substitution)
     - the board identity block matches boards.json
     - the chips match boards.json
     - the port-filter logic picks the right requestPort() argument

   test-codec.mjs proves the OSC bytes are right; this proves the generated
   wrapper around them is right, for all six boards rather than one.

   Run: node test/test-pages.mjs   (or `make test`). No dependencies. */

import { readFileSync, existsSync } from "node:fs";
import { BOARDS, EXAMPLES_DIR, sketchName } from "../render.mjs";

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};

for (const board of BOARDS) {
  const name = sketchName(board);
  console.log(`\n${name}`);

  const htmlPath = new URL(`${name}/${name}.html`, EXAMPLES_DIR);
  const inoPath = new URL(`${name}/${name}.ino`, EXAMPLES_DIR);
  if (!existsSync(htmlPath) || !existsSync(inoPath)) {
    ok("both files exist", false, "run `make generate`");
    continue;
  }
  ok("both files exist", true);

  const html = readFileSync(htmlPath, "utf8");
  const ino = readFileSync(inoPath, "utf8");
  const script = html.split(/<script>/)[1].split(/<\/script>/)[0];

  // 1. The whole script must parse, UI section included. new Function compiles
  //    without running, so document/navigator being absent here does not matter.
  let parsed = true, why = "";
  try { new Function(script); } catch (e) { parsed = false; why = e.message; }
  ok("script parses", parsed, why);

  // 2. No placeholder survived substitution, in either file.
  ok("no unsubstituted {{...}} in html", !/\{\{\w+\}\}/.test(html),
    (html.match(/\{\{\w+\}\}/g) || []).join(" "));
  ok("no unsubstituted {{...}} in ino", !/\{\{\w+\}\}/.test(ino),
    (ino.match(/\{\{\w+\}\}/g) || []).join(" "));

  // 3. Board identity — evaluate just section 0 and compare against boards.json.
  const identity = new Function(
    script.split("/* ============================================================\n   1. OSC")[0] +
    "return { BOARD_NAME, BOARD_FQBN, USB_FILTERS, NATIVE_USB };")();

  ok("BOARD_NAME matches", identity.BOARD_NAME === board.name,
    `${identity.BOARD_NAME} != ${board.name}`);
  ok("BOARD_FQBN matches", identity.BOARD_FQBN === board.fqbn,
    `${identity.BOARD_FQBN} != ${board.fqbn}`);
  ok("NATIVE_USB matches", identity.NATIVE_USB === !!board.nativeUSB);

  const wantFilters = (board.usbFilters || []).map(f =>
    ({ usbVendorId: Number(f.usbVendorId), usbProductId: Number(f.usbProductId) }));
  ok(`USB filters (${wantFilters.length})`,
    JSON.stringify(identity.USB_FILTERS) === JSON.stringify(wantFilters),
    `${JSON.stringify(identity.USB_FILTERS)} != ${JSON.stringify(wantFilters)}`);

  // Hex in boards.json must have survived as a number, not a string — a quoted
  // id silently matches nothing and the board never appears in the chooser.
  ok("filter ids are numbers",
    identity.USB_FILTERS.every(f =>
      typeof f.usbVendorId === "number" && typeof f.usbProductId === "number"));

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

  // 6. The sketch names its own pair and reports the right hello address.
  ok("ino references its own page", ino.includes(`${name}.html`));
  ok("hello identifies the board", ino.includes(`.add("${name}")`));

  // 7. Pin clamp present exactly when boards.json asks for one.
  const clamped = ino.includes("#define NUM_DIGITAL_PINS");
  ok(`pin clamp ${board.pinClamp ? "present" : "absent"}`, clamped === !!board.pinClamp);
}

console.log(`\n${pass} passed, ${fail} failed\n`);
process.exit(fail ? 1 : 0);

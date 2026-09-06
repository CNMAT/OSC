/* Shared render path for generate.mjs and check.mjs.
   Both must produce byte-identical output from the same inputs, which is the
   whole point of the drift check — so neither owns the substitution logic. */

import { readFileSync } from "node:fs";

const HERE = new URL("./", import.meta.url);

export const BOARDS = JSON.parse(readFileSync(new URL("boards.json", HERE), "utf8")).boards;
export const EXAMPLES_DIR = new URL("../../examples/", HERE);
export const PYTHON_DIR = new URL("../python/", HERE);
export const WEBSERIAL_DIR = HERE;

const TEMPLATE_HTML = readFileSync(new URL("template.html", HERE), "utf8");
const TEMPLATE_INO = readFileSync(new URL("template.ino", HERE), "utf8");
const TEMPLATE_PY = {
  microbit: readFileSync(new URL("template-microbit.py", HERE), "utf8"),
  circuitpython: readFileSync(new URL("template-circuitpython.py", HERE), "utf8"),
  fruitjam: readFileSync(new URL("template-fruitjam.py", HERE), "utf8"),
};
const TEMPLATE_BOOT = readFileSync(new URL("template-boot.py", HERE), "utf8");
const TEMPLATE_MICROBIT_MODULES = {
  "slip.py": readFileSync(new URL("template-microbit-slip.py", HERE), "utf8"),
  "osce.py": readFileSync(new URL("template-microbit-osce.py", HERE), "utf8"),
  "oscd.py": readFileSync(new URL("template-microbit-oscd.py", HERE), "utf8"),
};

// The non-Arduino runtimes a boards.json entry can name in `firmware`. The
// pair file is what a user copies onto the board, so it keeps the name that
// runtime insists on rather than <Id>Oscuino.py. bootPy marks the runtimes
// that need the usb_cdc data channel enabled at enumeration time.
const FIRMWARES = {
  microbit: { pairFile: "main.py", name: "MicroPython" },
  circuitpython: { pairFile: "code.py", name: "CircuitPython", bootPy: true },
  fruitjam: { pairFile: "code.py", name: "CircuitPython", bootPy: true },
};

// The carriers the page offers. boards.json picks the default with
// `transport`; the page still lets a user switch to any the browser has.
export const TRANSPORTS = {
  serial: "Web Serial",
  ble: "Web Bluetooth",
  http: "HTTP",
};

const esc = s => String(s)
  .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
  .replace(/"/g, "&quot;");

// `{{KEY}}` only — no conditionals, no loops. Anything that needs branching is
// computed here in JS and substituted as a finished string, which keeps the
// templates readable as the .html and .ino files they will become.
function fill(template, vars) {
  return template.replace(/\{\{(\w+)\}\}/g, (whole, key) => {
    if (!(key in vars)) throw new Error(`template placeholder {{${key}}} has no value`);
    return vars[key];
  });
}

function chipHtml(chips) {
  return chips
    .map(c => `      <button data-a="${esc(c.addr)}" data-g="${esc(c.args)}">${esc(c.label)}</button>`)
    .join("\n");
}

// Filters go to navigator.serial.requestPort(), which wants numbers. boards.json
// carries them as "0x239A" strings so the file stays readable next to a datasheet.
// usbProductId is optional: vendor-only matches every product of that vendor.
function usbFiltersJs(filters = []) {
  const parts = filters.map(f => f.usbProductId === undefined
    ? `{ usbVendorId: ${f.usbVendorId} }`
    : `{ usbVendorId: ${f.usbVendorId}, usbProductId: ${f.usbProductId} }`);
  return parts.length ? `[\n  ${parts.join(",\n  ")},\n]` : "[]";
}

// The line under the Connect button when Web Serial is selected. Rendered
// here rather than branched in page JS so a python firmware can say something
// true about its own deployment without every page carrying every variant.
function supportHtml(board) {
  const b = `<b>${board.name}</b>`;
  if (board.firmware === "microbit")
    return b + " runs MicroPython — flash MicroPython, then copy <code>main.py</code> onto it. " +
      "Leave the baud at 115200: the DAPLink bridge carries a real UART that must match " +
      "<code>uart.init()</code> in <code>main.py</code>.";
  if (board.firmware === "circuitpython" || board.firmware === "fruitjam")
    return b + ": native USB, so the baud rate is ignored. Copy <code>boot.py</code> and " +
      "<code>code.py</code> to the CIRCUITPY drive and press reset, then pick the board's " +
      "<i>second</i> serial port here — the data channel boot.py adds.";
  if (!board.fqbn)
    return b + ": pick the board's serial port. The baud rate is ignored by native-USB boards " +
      "and must match <code>SLIPSerial.begin()</code> on a board behind a USB-serial bridge.";
  return board.nativeUSB
    ? b + " has native USB, so the baud rate above is ignored by the hardware " +
      "&mdash; Web Serial still requires one. Build for <code>" + board.fqbn + "</code>."
    : b + " talks through a USB-serial bridge, so the baud rate must match " +
      "<code>SLIPSerial.begin()</code> in the sketch exactly. Build for <code>" + board.fqbn + "</code>.";
}

// The line under the title.
function subHtml(board, name, pairFile, fwName) {
  if (board.universal)
    return "The one page for every Oscuino sketch: it asks <code>/enq</code>, reads back the " +
      "capability list and shows a panel per capability the board announced (ADDRESSES.md). " +
      "Web Serial, Web Bluetooth or HTTP &mdash; whichever the sketch and the browser have. " +
      "No server, no dependencies.";
  const via = { serial: "USB serial", ble: "Bluetooth LE (Nordic UART Service)", http: "WiFi (HTTP bridge)" }[board.transport || "serial"];
  return `OSC for <span class="board">${esc(board.name)}</span> (${esc(board.mcu)}) over ${via}. ` +
    `Browser &harr; OSC &harr; ${fwName}. Pair with <code>${esc(pairFile)}</code>. ` +
    "No server, no dependencies.";
}

function bundleNote(board, pairFile) {
  if (board.universal)
    return "<b>bundle</b> wraps the message in a #bundle. Every sketch here reads a bare message; " +
      "the OSCBundle-based ones read bundles too, the OSCMessage-based WiFi and " +
      "expansion-board twins do not, so it starts unticked here.";
  return board.bundles === false
    ? `<b>bundle</b> is unticked because <code>${esc(pairFile)}</code> parses each packet as one ` +
      "OSCMessage and would drop a bundle."
    : `<b>bundle</b> is ticked by default because <code>${esc(pairFile)}</code> reads bundles, ` +
      "as do the stock Oscuino examples and the CNMAT Max patches.";
}

function pinClamp(board) {
  if (!board.pinClamp) return "// This variant's NUM_*_PINS macros match its pads; nothing to clamp.";
  const { digital, analog } = board.pinClamp;
  return [
    "// This variant declares more pins than it actually routes to pads, and the",
    "// route loops below walk those counts. Clamp them or the sketch will happily",
    "// analogRead() channels the silicon does not connect to anything.",
    "#undef NUM_DIGITAL_PINS",
    `#define NUM_DIGITAL_PINS ${digital}`,
    "#undef NUM_ANALOG_INPUTS",
    `#define NUM_ANALOG_INPUTS ${analog}`,
  ].join("\n");
}

const TONE_BODY = `  for (int pin = 0; pin < NUM_DIGITAL_PINS; pin++) {
    if (!msg.match(numToOSCAddress(pin), addrOffset)) continue;

    unsigned int freq = 0;
    if (msg.isInt(0))        freq = (unsigned int)msg.getInt(0);
    else if (msg.isFloat(0)) freq = (unsigned int)msg.getFloat(0);

    if (freq == 0) noTone(pin);
    else if (msg.isInt(1))   tone(pin, freq, msg.getInt(1));
    else                     tone(pin, freq);
    return;
  }`;

// boards.json may name a user button: {"button": {"pin": 9, "activeLow": true}}.
// Only a pin somebody has actually checked belongs here -- an unchecked guess
// makes the sketch read a bus line and report it as a button.
function buttonDefine(board) {
  const b = board.button;
  if (!b) return "// This board declares no user button in boards.json.";
  if (typeof b.pin !== "number")
    throw new Error(`${board.id}: button.pin must be a number`);
  return `#define BOARD_BUTTON_PIN ${b.pin}\n`
       + `#define BOARD_BUTTON_ACTIVE_LOW ${b.activeLow === false ? 0 : 1}`;
}

// boards.json may carry `defines`: lines emitted ABOVE the includes, which is
// the only place they can reach OSCBoards.h's capability decisions. The case
// this exists for is a board built against a GENERIC variant: the LilyGO
// T-Encoder-Pro has no core definition, so it builds as esp32s3, whose variant
// declares an RGB LED the board does not physically have. Without OSC_NO_RGB
// the sketch would announce /enq/rgb for a pixel that is not there -- a board
// lying about its hardware, which is exactly what "absence is silence" forbids.
function defines(board) {
  const d = board.defines;
  if (!d || !d.length) return "// This board adds no build defines.";
  if (!Array.isArray(d)) throw new Error(`${board.id}: defines must be an array`);
  return d.map(x => {
    if (typeof x === "string") return `#define ${x}`;
    if (!x.name) throw new Error(`${board.id}: a define needs a name`);
    return `#define ${x.name}${x.why ? `        // ${x.why}` : ""}`;
  }).join("\n");
}

const TONE_UNSUPPORTED = `  // This core ships no tone()/noTone(). Left as a no-op so the address space
  // stays identical across boards and a client does not have to special-case it.
  (void)msg; (void)addrOffset;`;

const SERIAL_DECL = `// OSCBoards.h defines BOARD_HAS_USB_SERIAL and thisBoardsSerialUSB for boards
// with native USB. Selecting through the macro rather than naming a port keeps
// this example working when a variant calls its USB CDC something unexpected.
#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif`;

// micro:bit V1, measured 2026-08-26 on the board: 8752 bytes of heap free at
// a clean REPL, and the on-device compiler needs ~2.1x a file's source bytes
// to parse it. The boot file gets the largest budget (before the REPL
// exists); every runtime import gets what earlier files left resident. One
// 9058-byte file died in MemoryError, so did 8332, so did every two-file
// split tried — main.py + three ~1 KB codec modules is the shape that boots.
// The squeeze below drops comment lines, blank lines, inline comments on
// quote-free lines, and collapses each 4-space indent level to one space.
// Lines carrying quotes keep their comments (an interior '#' may be data —
// '#bundle'), and the templates stay fully commented for humans.
function squeezeForV1(py, templateName) {
  const title = py.split("\n", 1)[0];
  const body = [];
  for (let line of py.split("\n")) {
    const t = line.trim();
    if (t === "" || t.startsWith("#")) continue;
    if (line.includes("#") && !line.includes('"') && !line.includes("'") && !line.includes("\\"))
      line = line.slice(0, line.indexOf("#")).trimEnd();
    const stripped = line.replace(/^ +/, "");
    const level = Math.floor((line.length - stripped.length) / 4);
    body.push(" ".repeat(level) + stripped);
  }
  return [
    title,
    "# GENERATED; squeezed so the V1's on-board compiler fits in RAM.",
    `# Readable source: extras/webserial/${templateName}  docs: extras/python/`,
  ].concat(body).join("\n") + "\n";
}

function renderHtml(board, name, pairFile, fwName) {
  const transport = board.transport || "serial";
  if (!(transport in TRANSPORTS)) throw new Error(`unknown transport '${transport}' on ${board.id}`);
  return fill(TEMPLATE_HTML, {
    SKETCH: esc(name),
    SUB_HTML: subHtml(board, name, pairFile, fwName),
    CHIPS: chipHtml(board.chips || []),
    NOTE_HTML: board.universal
      ? "Chips and notes for a particular board live on that board's own generated page; " +
        "this one carries only what every sketch shares."
      : `<b>${esc(board.name)}.</b> ${esc(board.note)}`,
    BUNDLE_CHECKED: board.universal || board.bundles === false ? "" : " checked",
    BUNDLE_NOTE: bundleNote(board, pairFile),
    BOARD_NAME_JS: JSON.stringify(board.name),
    FQBN_JS: JSON.stringify(board.fqbn),
    USB_FILTERS: usbFiltersJs(board.usbFilters),
    NATIVE_USB: board.nativeUSB ? "true" : "false",
    SUPPORT_HTML_JS: JSON.stringify(supportHtml(board)),
    DEFAULT_TRANSPORT_JS: JSON.stringify(transport),
  });
}

export function render(board) {
  const name = sketchName(board);
  const fw = FIRMWARES[board.firmware];
  if (board.firmware && !fw) throw new Error(`unknown firmware '${board.firmware}' on ${board.id}`);
  if (board.firmware && board.handwritten)
    throw new Error(`${board.id}: handwritten is for Arduino sketches, python firmwares are always generated`);
  const pairFile = fw ? fw.pairFile : `${name}.ino`;
  const fwName = fw ? fw.name : "the CNMAT OSC library";

  const html = renderHtml(board, name, pairFile, fwName);

  if (fw) {
    let code = fill(TEMPLATE_PY[board.firmware], {
      ID: board.id,
      NAME: board.name,
      MCU: board.mcu,
      NOTE: board.note,
    });
    if (board.firmware === "microbit") code = squeezeForV1(code, "template-microbit.py");
    const files = { [pairFile]: code, [`${name}.html`]: html };
    if (fw.bootPy) files["boot.py"] = fill(TEMPLATE_BOOT, { ID: board.id });
    if (board.firmware === "microbit")
      for (const [file, body] of Object.entries(TEMPLATE_MICROBIT_MODULES))
        files[file] = squeezeForV1(body, `template-microbit-${file.replace(".py", "")}.py`);
    return { html, code, pairFile, files };
  }

  // A hand-written sketch keeps its .ino: only the page is generated beside
  // it, so the demos with real peripherals get the same page as everyone.
  if (board.handwritten)
    return { html, ino: null, code: null, pairFile, files: { [`${name}.html`]: html } };

  const ino = fill(TEMPLATE_INO, {
    ID: board.id,
    NAME: board.name,
    MCU: board.mcu,
    FQBN: board.fqbn,
    NOTE: board.note,
    SERIAL_DECL,
    PIN_CLAMP: pinClamp(board),
    TONE_BODY: board.tone === false ? TONE_UNSUPPORTED : TONE_BODY,
    BUTTON_DEFINE: buttonDefine(board),
    DEFINES: defines(board),
    BAUD_NOTE: board.nativeUSB
      ? "ignored on native USB, but Web Serial still demands a value"
      : "must match the baud picked in the browser exactly",
  });

  return { html, ino, code: ino, pairFile, files: { [pairFile]: ino, [`${name}.html`]: html } };
}

// Every file a board renders to, with repo-relative label and absolute URL.
// Python firmwares land in extras/python/ rather than examples/ so the Arduino
// IDE never lists a sketch-less folder.
export function outputs(board) {
  const name = sketchName(board);
  const { files } = render(board);
  const root = board.firmware ? PYTHON_DIR : EXAMPLES_DIR;
  const relRoot = board.firmware ? "extras/python" : "examples";
  return Object.entries(files).map(([file, body]) => ({
    file,
    rel: `${relRoot}/${name}/${file}`,
    url: new URL(`${name}/${file}`, root),
    dir: new URL(`${name}/`, root),
    body,
  }));
}

// The board-less page: no chips, no port filter, any transport. Rendered
// from the same template so it cannot drift from the per-board pages.
export const UNIVERSAL = {
  id: "Oscuino",
  universal: true,
  name: "any Oscuino board",
  mcu: "",
  fqbn: "",
  nativeUSB: true,
  usbFilters: [],
  chips: [],
  note: "",
  transport: "serial",
};

export function universalOutput() {
  const html = renderHtml(UNIVERSAL, "Oscuino", "any Oscuino sketch", "the CNMAT OSC library");
  return {
    file: "oscuino.html",
    rel: "extras/webserial/oscuino.html",
    url: new URL("oscuino.html", WEBSERIAL_DIR),
    dir: WEBSERIAL_DIR,
    body: html,
  };
}

// The board table in README.md. Generated for the same reason everything else
// here is: a hand-kept copy of boards.json drifts, and this one had — it listed
// 9 of 23 boards, and carried build sizes stale by up to 256 bytes.
//
// No size column. A byte count is a property of the core version and its
// compiler rather than of this repository: update a core and every figure
// changes with no commit here to notice, and nothing in the suite compares one
// to a real build, so it rots silently. Whether a sketch fits is enforced where
// it matters — arduino-cli fails the build when it does not, and CI compiles a
// subset on every push. Measured sizes for the constrained parts, carrying the
// date and the denominator, live in BOARDS.md.
export function boardTableMd() {
  const rows = BOARDS.map(board => {
    const where = board.fqbn
      ? "`" + board.fqbn + "`"
      : "interpreted, `extras/python/`";
    return `| \`${sketchName(board)}\` | ${board.name} | ${where} |`;
  });
  return [
    "| Example | Board | FQBN |",
    "|---------|-------|------|",
    ...rows,
  ].join("\n");
}

const README_BEGIN = "<!-- BEGIN generated board table — `make generate` writes this, `make check` guards it -->";
const README_END = "<!-- END generated board table -->";

// Spliced rather than rendered whole: README.md is mostly prose that no
// template owns, so only the marked region is generated. The comparison in
// check.mjs still works — body is the file with a freshly rendered region, so a
// stale table differs from it exactly as a stale .ino does.
function readmeOutput() {
  const url = new URL("README.md", HERE);
  const current = readFileSync(url, "utf8");
  const a = current.indexOf(README_BEGIN);
  const b = current.indexOf(README_END);
  if (a < 0 || b < 0 || b < a)
    throw new Error("extras/webserial/README.md: board-table markers missing or out of order");
  const body =
    current.slice(0, a + README_BEGIN.length) +
    "\n\n" + boardTableMd() + "\n\n" +
    current.slice(b);
  return { url, rel: "extras/webserial/README.md", dir: HERE, body };
}

// Everything the generator writes: every board's files, the universal page,
// then the board table spliced into this README.
export function allOutputs() {
  const list = [];
  for (const board of BOARDS) list.push(...outputs(board));
  list.push(universalOutput());
  list.push(readmeOutput());
  return list;
}

// `sketch` names the folder and basename when a hand-written sketch does not
// follow the <Id>Oscuino convention (EggC3WiFi, XiaoC6ExpBLE, …).
export const sketchName = board => board.sketch || `${board.id}Oscuino`;

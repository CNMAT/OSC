/* Shared render path for generate.mjs and check.mjs.
   Both must produce byte-identical output from the same inputs, which is the
   whole point of the drift check — so neither owns the substitution logic. */

import { readFileSync } from "node:fs";

const HERE = new URL("./", import.meta.url);

export const BOARDS = JSON.parse(readFileSync(new URL("boards.json", HERE), "utf8")).boards;
export const EXAMPLES_DIR = new URL("../../examples/", HERE);

const TEMPLATE_HTML = readFileSync(new URL("template.html", HERE), "utf8");
const TEMPLATE_INO = readFileSync(new URL("template.ino", HERE), "utf8");

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
function usbFiltersJs(filters = []) {
  const parts = filters.map(f =>
    `{ usbVendorId: ${f.usbVendorId}, usbProductId: ${f.usbProductId} }`);
  return parts.length ? `[\n  ${parts.join(",\n  ")},\n]` : "[]";
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

export function render(board) {
  const common = {
    ID: board.id,
    NAME: board.name,
    BOARD_NAME: esc(board.name),
    MCU: esc(board.mcu),
    FQBN: esc(board.fqbn),
    NOTE: esc(board.note),
  };

  const html = fill(TEMPLATE_HTML, {
    ...common,
    CHIPS: chipHtml(board.chips),
    BOARD_NAME_JS: JSON.stringify(board.name),
    FQBN_JS: JSON.stringify(board.fqbn),
    USB_FILTERS: usbFiltersJs(board.usbFilters),
    NATIVE_USB: board.nativeUSB ? "true" : "false",
  });

  const ino = fill(TEMPLATE_INO, {
    ID: board.id,
    NAME: board.name,
    MCU: board.mcu,
    FQBN: board.fqbn,
    NOTE: board.note,
    SERIAL_DECL,
    PIN_CLAMP: pinClamp(board),
    TONE_BODY: board.tone === false ? TONE_UNSUPPORTED : TONE_BODY,
    BAUD_NOTE: board.nativeUSB
      ? "ignored on native USB, but Web Serial still demands a value"
      : "must match the baud picked in the browser exactly",
  });

  return { html, ino };
}

export const sketchName = board => `${board.id}Oscuino`;

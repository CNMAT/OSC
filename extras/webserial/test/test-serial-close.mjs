/* The Web Serial pages let go of the port, and only of their own.

   Two defects, found on the XIAO SAMD21 page on 2026-10-28:

   1. port.readable.pipeThrough(...) pipes inside the stream, where the page
      cannot wait for it. After reader.cancel() that hidden pipe can still hold
      port.readable when port.close() runs, and Chrome will not close a port
      whose stream is locked. The page's catch swallows the error, the port
      stays open, and the next upload to the board fails "Resource busy".
      The fix pipes with pipeTo() and waits for that pipe before closing.
   2. A "disconnect" listener on navigator.serial that does not compare
      e.target with its own port drops the connection when any serial device
      is unplugged. The Web Serial spec ("ondisconnect") fires the event at
      the port that went, bubbling to navigator.serial.

   Each page's whole script runs here against a stub document (every element
   a Proxy that takes any property, call or iteration and does nothing) and a
   fake Web Serial. Its port, like Chrome's, refuses close() while its
   readable or writable is locked, and its readable's cancel completes a few
   milliseconds later, as an operating system's close does. The board-less
   generated page, whose transport already closed correctly, is the control:
   it shows the fake port can be closed at all.

   Run: node test/test-serial-close.mjs   (or `make test`). No dependencies. */

import { readFileSync } from "node:fs";

const ROOT = new URL("../../../", import.meta.url);
const read = p => readFileSync(new URL(p, ROOT), "utf8");

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};

// A stub remembers what the page assigns to it (a button's onclick, a select's
// value) and answers anything else with another stub, so a handler the page
// installs can be called back and a value it sets can be read back.
const stub = () => {
  const own = {};
  return new Proxy(function () {}, {
    get: (_, k) => k in own ? own[k]
                 : k === Symbol.iterator ? function* () {}
                 : k === Symbol.toPrimitive ? () => ""
                 : k === "then" ? undefined : stub(),
    set: (_, k, v) => { own[k] = v; return true; },
    apply: () => stub(),
    construct: () => stub(),
  });
};
// One element per id, as a document has.
const documentStub = () => {
  const byId = new Map();
  const doc = stub();
  doc.getElementById = id => { if (!byId.has(id)) byId.set(id, stub()); return byId.get(id); };
  return doc;
};

// A serial port as Chrome behaves: no streams until open(), and close()
// refused while either stream is locked.
function fakePort({ opens = true } = {}) {
  return {
    readable: null, writable: null, closed: false, refused: null,
    async open() {
      if (!opens) throw new DOMException("Failed to open serial port.", "NetworkError");
      this.readable = new ReadableStream({
        pull: () => new Promise(() => {}),                  // the board says nothing
        cancel: () => new Promise(r => setTimeout(r, 5)),   // the OS takes a moment
      });
      this.writable = new WritableStream({ write() {} });
    },
    async close() {
      if (!this.readable) throw new DOMException("The port is already closed.", "InvalidStateError");
      if (this.readable.locked || this.writable.locked) {
        this.refused = `readable locked ${this.readable.locked}, writable locked ${this.writable.locked}`;
        throw new TypeError("Cannot cancel a locked stream");
      }
      await this.readable.cancel();
      await this.writable.abort();
      this.closed = true;
    },
  };
}

// The page's one <script>, run with `ret` appended so its own functions and
// state can be reached. The page asks for ports through `offer`.
function load(file, ret) {
  const html = read(file);
  const script = html.split(/<script>/)[1].split(/<\/script>/)[0];
  const listeners = {};
  let offered = null;
  const navigator = { serial: {
    addEventListener: (type, f) => { listeners[type] = f; },
    requestPort: async () => offered,
    getPorts: async () => [],
  } };
  const page = new Function("document", "navigator", "location", "window",
                            "setInterval", "requestAnimationFrame", "ResizeObserver",
                            "getComputedStyle", "matchMedia", "localStorage",
    script + `\nreturn { ${ret} };`)(
    documentStub(), navigator, { search: "", protocol: "http:", hostname: "localhost" }, stub(),
    () => 0, () => 0, stub(), stub(), stub(), stub());
  return { page, listeners, offer: p => { offered = p; } };
}

const tick = () => new Promise(r => setTimeout(r, 20));

// The five hand-written pages share one shape: top-level connect(),
// disconnect() and `port`.
const HAND = ["CircuitPlaygroundSensors", "PyBadgeOscuino", "HallowingOscuino",
              "EsploraOscuino", "XiaoS3SenseOscuino"];
for (const name of HAND) {
  console.log(`\n${name}`);
  const { page, listeners, offer } = load(`examples/${name}/${name}.html`,
    "connect, disconnect, held: () => port");

  let p = fakePort();
  offer(p);
  await page.connect();
  await page.disconnect();
  ok("Disconnect closes the port", p.closed, p.refused ? "close() refused: " + p.refused : "");

  offer(fakePort({ opens: false }));
  await page.connect().catch(() => {});
  ok("a port that would not open is not held", page.held() === null);

  p = fakePort();
  offer(p);
  await page.connect();
  listeners.disconnect({ target: fakePort() });
  await tick();
  ok("another device unplugged leaves this port open", page.held() === p && !p.closed);
  listeners.disconnect({ target: p });
  await tick();
  ok("this port unplugged lets it go", page.held() === null);
}

// The pages generated from template.html, through the board-less one (make
// check holds every generated page to the template).
console.log("\noscuino.html (generated from template.html)");
{
  const { page, listeners, offer } = load("extras/webserial/oscuino.html",
    "serialTransport, current: () => transport, use: t => { transport = t; }");
  const t = page.serialTransport;

  let p = fakePort();
  offer(p);
  await t.connect();
  await t.disconnect();
  ok("control: Disconnect closes the port", p.closed, p.refused ? "close() refused: " + p.refused : "");

  p = fakePort();
  offer(p);
  await t.connect();
  page.use(t);
  listeners.disconnect({ target: fakePort() });
  await tick();
  ok("another device unplugged leaves this port connected", page.current() === t && !p.closed);
  listeners.disconnect({ target: p });
  await tick();
  ok("this port unplugged ends the connection", page.current() === null && p.closed);
}

console.log(`\n${pass} passed, ${fail} failed\n`);
if (fail) process.exit(1);

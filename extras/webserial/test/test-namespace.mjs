/* The address-space contract, enforced.

   ADDRESSES.md names every OSC address root a sketch, firmware or page in
   this repository may use. This scans all of them and fails on anything
   else, so the per-board dialects the contract replaced (/pybadge, /cpx,
   /pb, /xb, /egg, ...) cannot creep back in one demo at a time -- which is
   exactly how they arrived the first time.

   Scope: every sketch, header, page, firmware and template under examples/,
   extras/python/ and extras/webserial/ -- the tutorial examples and the
   classic SerialOscuino pair included, not only the demos with pages. The
   only exclusions are codec and host tests, whose addresses are fixtures
   that exist to exercise the parser, not vocabulary anyone speaks.

   Run: node test/test-namespace.mjs   (or `make test`). No dependencies. */

import { readFileSync, readdirSync, statSync } from "node:fs";
import { join, extname, relative } from "node:path";

const ROOT = new URL("../../../", import.meta.url).pathname;

// The contract. Anything under one of these roots is allowed; the exact
// sub-addresses are documented in ADDRESSES.md, this only guards the roots
// (a wrong sub-address is a bug, a new root is a dialect).
const ALLOWED = new Set([
  "enq", "d", "a", "tone", "s", "rate", "heartbeat", "deadband", "state",
  "rgb", "display", "buzz", "btn", "imu", "mic", "light", "temp", "hum", "bat", "chg",
  "c", "touch", "cap", "joy", "pot", "enc", "rtc", "rfid", "cam", "stream",
  "motor", "servo", "relay", "net", "diag",
]);

// Page-internal HTTP routes are URLs, not OSC addresses.
const HTTP_ROUTES = new Set(["osc"]);

const SCAN_DIRS = ["examples", "extras/python", "extras/webserial"];
const EXTS = new Set([".ino", ".h", ".html", ".py", ".json"]);
// Codec and host tests use fixture addresses to exercise the parser, and
// ETC_EOS_TCP speaks the Eos lighting console's own protocol (/eos/...),
// which ETC defines, not this repository. Nothing else is exempt.
const SKIP = /node_modules|\/test\/|test_host\.py$|ETC_EOS_TCP/;

function* walk(dir) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    const st = statSync(p);
    if (st.isDirectory()) yield* walk(p);
    else if (EXTS.has(extname(name))) yield p;
  }
}

// An OSC address literal: a quoted string starting with "/" followed by an
// identifier. Filesystem paths and URLs are excluded by requiring the first
// segment to be a bare identifier and the literal not to contain "." or ":".
const LITERAL = /["']\/([A-Za-z][A-Za-z0-9_]*)(?:\/[^"'\s]*)?["']/g;

const violations = [];
let files = 0, literals = 0;

for (const dir of SCAN_DIRS) {
  for (const file of walk(join(ROOT, dir))) {
    const rel = relative(ROOT, file);
    if (SKIP.test(rel)) continue;
    files++;
    // Route handlers match address FRAGMENTS relative to an offset --
    // fullMatch("/u", addrOffset), match("/12", addrOffset) -- and the
    // template's pinAddress(buf, "/d", pin, "/u") assembles one from parts.
    // Neither is an address. Strip those call sites before scanning.
    const text = readFileSync(file, "utf8")
      .replace(/\b(?:fullMatch|match)\(\s*"\/[^"]*"/g, "")
      .replace(/\bpinAddress\([^)]*\)/g, "")
      .replace(/\bstrcat\([^)]*\)/g, "")           // the 2012 sketches' strcat(out, "/u")
      // A filesystem path is not an OSC address, and the two look identical:
      // WiFiSetup keeps its credentials in "/fs/oscwifi" on a NINA module's
      // flash. Constants named *_FILE / *_PATH / *_DIR are storage, so their
      // initialisers are skipped -- the name is the declaration of intent.
      .replace(/\b\w*(?:FILE|PATH|DIR)\w*\s*=\s*"[^"]*"/gi, "")
      // An HTTP route is not an OSC address either. WiFiSetup serves a
      // settings page and compares the request path against "/save" and
      // "/forget"; a literal tested against a .path member is a URL, so it
      // is skipped by shape rather than by adding its words to a whitelist,
      // which would let those roots through as OSC addresses everywhere.
      .replace(/\.path\s*,\s*"[^"]*"/g, "");
    for (const m of text.matchAll(LITERAL)) {
      const whole = m[0], root = m[1];
      if (whole.includes(".") || whole.includes(":")) continue;   // a path or URL
      literals++;
      if (ALLOWED.has(root)) continue;
      if (HTTP_ROUTES.has(root)) continue;                    // http.on("/osc")
      // A pin-number root like "/12" is the template's numToOSCAddress
      // digit table, matched by the standard /d, /a, /tone routes.
      if (/^\d+$/.test(root)) continue;
      const line = text.slice(0, m.index).split("\n").length;
      violations.push(`${rel}:${line}  ${whole}`);
    }
  }
}

const byFile = new Map();
for (const v of violations) {
  const f = v.split(":")[0];
  byFile.set(f, (byFile.get(f) || 0) + 1);
}

console.log(`scanned ${files} files, ${literals} address literals`);
if (violations.length) {
  console.log(`\n${violations.length} addresses outside ADDRESSES.md, in ${byFile.size} files:\n`);
  for (const [f, n] of [...byFile].sort((a, b) => b[1] - a[1]))
    console.log(`  ${String(n).padStart(3)}  ${f}`);
  if (process.argv.includes("-v"))
    for (const v of violations) console.log("    " + v);
  console.log("\nnamespace: FAIL");
  process.exit(1);
}
console.log("namespace: every address is in the contract");

/* Announce and answer: the static half of contractprobe.

   ADDRESSES.md says "Every request that reads something answers on the same
   address." A sketch that puts /enq/<capability> in its enq bundle is
   claiming to have that thing, so a client is entitled to ask for it.

   contractprobe.py checks this on hardware, board by board, as boards come to
   hand. This checks it in the source for every sketch at once, with no board
   at all, which is the only way the answer arrives before someone is standing
   at a bench. The two agree by construction: both read the capability list out
   of the sketch's own announcement rather than from a per-board table.

   Actuators (rgb, buzz, display, motor, servo, relay) and passive capabilities
   (touch, cam, net, diag, rfid) are excluded -- they have nothing to read.

   Run: node test/test-announce.mjs   (or `make test`). No dependencies. */

import { readFileSync, readdirSync, existsSync } from "node:fs";
import { join } from "node:path";

const ROOT = new URL("../../../", import.meta.url).pathname;
const EXAMPLES = join(ROOT, "examples");

// capability -> the address a reader asks for it. Transcribed from the
// capability table in ADDRESSES.md, same list contractprobe.py carries.
const ASK = {
  btn: "/btn", imu: "/imu", cap: "/cap", joy: "/joy", pot: "/pot",
  bat: "/bat", light: "/light", temp: "/temp", hum: "/hum",
  chg: "/chg", enc: "/enc", rtc: "/rtc", mic: "/mic", volts: "/s/v",
};

const ENQ = /"\/enq\/([a-z]+)"/g;
const ROUTED = /(?:\.route|\.dispatch|dispatch)\(\s*"(\/[A-Za-z0-9_/]+)"/g;
const EMITTED = /\.add\(\s*"(\/[A-Za-z0-9_/]+)"/g;

/* The WiFi twins answer differently, and legitimately. They keep the state
   bundle encoded in `stateBuf` and return those same bytes as the reply to
   EVERY inbound packet (and from GET /state), so a client that asks /btn does
   receive /btn -- there is simply no per-address route to find. Requiring one
   here would be requiring a particular implementation rather than the promised
   behaviour, so for these the test asks only that the address is in the
   bundle they send back. */
const ANSWERS_EVERYTHING_WITH_STATE = /stateBuf/;

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};

for (const dir of readdirSync(EXAMPLES)) {
  const ino = join(EXAMPLES, dir, `${dir}.ino`);
  if (!existsSync(ino)) continue;
  const src = readFileSync(ino, "utf8");

  const announced = [...src.matchAll(ENQ)].map(m => m[1]);
  if (!announced.length) continue;
  const routed = new Set([...src.matchAll(ROUTED)].map(m => m[1]));
  const emitted = new Set([...src.matchAll(EMITTED)].map(m => m[1]));
  const viaState = ANSWERS_EVERYTHING_WITH_STATE.test(src);

  for (const cap of [...new Set(announced)]) {
    const ask = ASK[cap];
    if (!ask) continue;                       // an actuator or a passive one
    const answerable = routed.has(ask) || (viaState && emitted.has(ask));
    ok(`${dir}: announces /enq/${cap}, so ${ask} must be answerable`,
       answerable,
       `the enq bundle promises ${cap} but nothing answers ${ask}: no route ` +
       `handles it${viaState ? " and it is not in the state bundle this sketch " +
       "returns to every request" : ""}. A client that asks gets silence, and ` +
       `ADDRESSES.md says a request that reads something answers on the same ` +
       `address`);
  }
}

console.log(`\n${pass} passed, ${fail} failed`);
if (fail) process.exit(1);

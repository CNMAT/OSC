/* The capability model, driven under node.

   The page decides what to show by decoding a /enq bundle — `/enq <name>`
   then one `/enq/<cap> [params]` per capability present — and keeps its panels
   current from stream bundles of `/state <seq> <millis>` plus one message per
   streamed capability (ADDRESSES.md). That logic lives in section 3 of the
   page, before the `@@CORE-END@@` marker, with no DOM behind it, precisely so
   this script can build real bundles with the page's own codec and push them
   through applyPacket() and panelsFor().

   It reads the generated board-less page so that what is tested is what
   ships; `make check` proves the per-board pages carry the same script.

   Run: node test/test-panels.mjs   (or `make test`). No dependencies. */

import { readFileSync } from "node:fs";

const PAGE = new URL("../oscuino.html", import.meta.url);
const CONTRACT = new URL("../../../ADDRESSES.md", import.meta.url);

let html;
try { html = readFileSync(PAGE, "utf8"); }
catch { console.log("\nNo generated oscuino.html. Run `make generate` first.\n"); process.exit(1); }

const script = html.split(/<script>/)[1].split(/<\/script>/)[0];
const core = script.split("/* @@CORE-END@@")[0];
const M = new Function(core + `
  return { encodeMessage, encodeBundle, decodePacket, PANEL_ORDER,
           newModel, applyPacket, panelsFor, flatten, dbfs };`)();
const { encodeMessage, encodeBundle, decodePacket, PANEL_ORDER, newModel, applyPacket, panelsFor } = M;

let pass = 0, fail = 0;
const ok = (label, cond, detail = "") => {
  if (cond) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}${detail ? "\n       " + detail : ""}`); }
};
const eq = (label, a, b) => ok(label, JSON.stringify(a) === JSON.stringify(b),
                               `${JSON.stringify(a)}\n       != ${JSON.stringify(b)}`);
const f = v => ({ type: "f", value: v });
const bundle = msgs => decodePacket(encodeBundle(msgs.map(([a, args]) => encodeMessage(a, args))));

console.log("\nThe contract's capabilities all have a panel");
{
  const contract = readFileSync(CONTRACT, "utf8");
  const table = contract.slice(contract.indexOf("## Capabilities"), contract.indexOf("## What moved"));
  const caps = [...table.matchAll(/^\| \*\*([a-z]+)\*\*/gm)].map(m => m[1]);
  // motor/servo/relay share one row; diag has no panel by design (it is
  // free text for the log).
  const rowCaps = [...table.matchAll(/^\| \*\*([a-z]+)\*\*(?:, \*\*([a-z]+)\*\*)?(?:, \*\*([a-z]+)\*\*)?/gm)]
    .flatMap(m => [m[1], m[2], m[3]].filter(Boolean));
  ok("contract table parsed", caps.length > 10, String(caps.length));
  for (const c of rowCaps) if (c !== "diag")
    ok(`${c} is in PANEL_ORDER`, PANEL_ORDER.includes(c));
  for (const c of PANEL_ORDER)
    ok(`page has a panel element for ${c}`, html.includes(`data-cap="${c}"`));
  ok("panels start hidden", (html.match(/class="panel cap" data-cap="[a-z]+" hidden/g) || []).length === PANEL_ORDER.length);
}

console.log("\nBefore any hello");
{
  const m = newModel();
  eq("no panels", panelsFor(m), []);
  const r = applyPacket(m, bundle([["/state", [1, 10]], ["/btn", [1]]]));
  ok("a stream before hello is not a hello", r.hello === false);
  eq("still no panels: a stream does not announce", panelsFor(m), []);
  eq("but /state was folded in", m.state, { seq: 1, millis: 10 });
}

console.log("\nA enq bundle from a board with real peripherals (FruitJam-shaped)");
const m = newModel();
{
  const r = applyPacket(m, bundle([
    ["/enq", ["FruitJamOscuino"]],
    ["/enq/btn", [3]],
    ["/enq/rgb", [5]],
    ["/enq/buzz", []],
    ["/enq/display", [80, 30]],
  ]));
  ok("recognised as a hello", r.hello === true);
  eq("name", m.name, "FruitJamOscuino");
  eq("caps with their shapes", m.caps, { btn: [3], rgb: [5], buzz: [], display: [80, 30] });
  eq("panels, in page order, only what was announced", panelsFor(m), ["rgb", "display", "buzz", "btn"]);
  ok("imu is not shown: it was not announced", !panelsFor(m).includes("imu"));
}

console.log("\nA stream bundle updates the panels it names");
{
  const r = applyPacket(m, bundle([
    ["/state", [7, 1234]],
    ["/btn", [0, 1, 0]],
    ["/imu", [f(0.01), f(-0.02), f(0.99)]],
  ]));
  ok("not a hello", r.hello === false);
  eq("state", m.state, { seq: 7, millis: 1234 });
  eq("btn values", m.values.btn, [0, 1, 0]);
  ok("updated set names state, btn, imu",
    ["state", "btn", "imu"].every(c => r.updated.has(c)), [...r.updated].join(" "));
  ok("imu values are kept even though its panel is hidden",
    m.values.imu.length === 3 && Math.abs(m.values.imu[2] - 0.99) < 1e-6);
  eq("an unannounced capability still does not get a panel", panelsFor(m), ["rgb", "display", "buzz", "btn"]);
}

console.log("\nReplies and echoes");
{
  applyPacket(m, bundle([["/rgb", [255, 0, 0]]]));
  eq("/rgb echo", m.values.rgb, [255, 0, 0]);
  applyPacket(m, bundle([["/rgb/3", [0, 0, 255]]]));
  eq("/rgb/<n> echo lands on that pixel", m.pixels[3], [0, 0, 255]);
  applyPacket(m, bundle([["/rgb/bright", [40]]]));
  eq("/rgb/bright echo is a sub-address", m.sub["/rgb/bright"], [40]);
  applyPacket(m, bundle([["/display/text", [2]]]));
  eq("/display/text reply: lines drawn", m.display.drawn, 2);
  const frame = new Uint8Array(80 * 30).fill(1);
  applyPacket(m, bundle([["/display/frame", [frame]]]));
  ok("/display/frame blob kept", m.display.frame instanceof Uint8Array && m.display.frame.length === 2400);
  applyPacket(m, bundle([["/rate", [50]]]));
  eq("/rate echo", m.rate, 50);
  applyPacket(m, bundle([["/mic/gain", [50]]]));
  eq("/mic/gain does not masquerade as a /mic reading", m.values.mic, undefined);
  eq("/mic/gain is a sub-address", m.sub["/mic/gain"], [50]);
  applyPacket(m, bundle([["/mic", [42, 900]]]));
  eq("/mic rms, peak", m.values.mic, [42, 900]);
  applyPacket(m, bundle([["/touch", [120, 100, 2]]]));
  eq("/touch x y gesture", m.touch, { x: 120, y: 100, gesture: 2 });
  applyPacket(m, bundle([["/diag", ["scroll at", 12]]]));
  eq("/diag is free text, kept as a line", m.diag, ["scroll at 12"]);
  const r = applyPacket(m, bundle([["/d/13", [1]], ["/a/0", [512]], ["/s/m", [99]]]));
  ok("pin replies touch no capability panel", r.updated.size === 0 && r.hello === false);
  ok("dBFS of full scale is 0", Math.abs(M.dbfs(32767)) < 1e-9);
  ok("dBFS of silence is floored, not -Infinity", Number.isFinite(M.dbfs(0)));
}

console.log("\nA WiFi twin's hello carries /enq/net");
{
  const w = newModel();
  applyPacket(w, bundle([
    ["/enq", ["XiaoC6ExpWiFi"]],
    ["/enq/btn", [1]],
    ["/enq/buzz", []],
    ["/enq/display", [128, 64]],
    ["/enq/net", ["192.168.1.50", -67, 8000]],
  ]));
  eq("net panel shown last", panelsFor(w), ["display", "buzz", "btn", "net"]);
  eq("ip, rssi, port", w.net, { ip: "192.168.1.50", rssi: -67, port: 8000 });
}

console.log("\nA second hello replaces the first");
{
  applyPacket(m, bundle([["/enq", ["FruitJamOscuino"]], ["/enq/btn", [3]], ["/enq/rgb", [5]]]));
  eq("codec and display gone: a bare board announces less", panelsFor(m), ["rgb", "btn"]);
  applyPacket(m, decodePacket(encodeMessage("/enq", ["GemmaOscuino"])));
  eq("a bare /enq (the generated template's) announces nothing", panelsFor(m), []);
  eq("but names the sketch", m.name, "GemmaOscuino");
  ok("caps is an empty object, not null: a hello did arrive", m.caps !== null && Object.keys(m.caps).length === 0);
}

console.log("\nA nested bundle and a bare message both flatten");
{
  const n = newModel();
  const inner = encodeBundle([encodeMessage("/enq/temp", []), encodeMessage("/enq/light", [])]);
  const outer = encodeBundle([encodeMessage("/enq", ["X"]), inner]);
  applyPacket(n, decodePacket(outer));
  eq("nested /enq lines reach the model", panelsFor(n), ["light", "temp"]);
  applyPacket(n, decodePacket(encodeMessage("/temp", [f(21.5)])));
  ok("a bare /temp reply", Math.abs(n.values.temp[0] - 21.5) < 1e-6);
}

console.log(`\n${pass} passed, ${fail} failed\n`);
process.exit(fail ? 1 : 0);

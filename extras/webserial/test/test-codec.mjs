/* Pulls the codec + SLIP sections out of a *generated* page and checks them
   against hand-computed OSC 1.0 byte sequences. Testing the generated artifact
   rather than the template is deliberate: the template cannot be evaluated on
   its own (it still holds {{...}} placeholders), and the generated file is what
   actually ships next to the sketch.

   Every page is rendered from one template, so checking one covers all of them —
   `make check` is what proves the others have not drifted.

   Run: node test/test-codec.mjs   (or `make test`). No dependencies. */
import { readFileSync } from "node:fs";

const PAGE = new URL("../../../examples/GemmaOscuino/GemmaOscuino.html", import.meta.url);

let html;
try {
  html = readFileSync(PAGE, "utf8");
} catch {
  console.log("\nNo generated page found. Run `make generate` first.\n");
  process.exit(1);
}

const script = html.split(/<script>/)[1].split(/<\/script>/)[0];
// Everything up to the marker is codec, SLIP and the capability model: pure
// functions with no DOM behind them. The transports and UI follow it.
const codecOnly = script.split("/* @@CORE-END@@")[0];

const mod = new Function(codecOnly + `
  return { encodeMessage, encodeBundle, decodePacket, decodeMessage,
           slipEncode, SlipDecoder, TIMETAG_IMMEDIATE };`)();
const { encodeMessage, encodeBundle, decodePacket, slipEncode, SlipDecoder } = mod;

let pass = 0, fail = 0;
const hex = b => Array.from(b, x => x.toString(16).padStart(2, "0")).join(" ");

function eq(label, got, want) {
  const g = got instanceof Uint8Array ? hex(got) : JSON.stringify(got);
  const w = want instanceof Uint8Array ? hex(want) : JSON.stringify(want);
  if (g === w) { pass++; console.log(`  ok   ${label}`); }
  else { fail++; console.log(`  FAIL ${label}\n       got  ${g}\n       want ${w}`); }
}
const bytes = s => new Uint8Array(s.trim().split(/\s+/).map(x => parseInt(x, 16)));

console.log("\nOSC encoding — against hand-computed bytes");

eq("/d/13 ,i 1",
  encodeMessage("/d/13", [1]),
  bytes("2f 64 2f 31 33 00 00 00  2c 69 00 00  00 00 00 01"));

eq("/s/m (no args)",
  encodeMessage("/s/m", []),
  bytes("2f 73 2f 6d 00 00 00 00  2c 00 00 00"));

// address length already a multiple of 4 must still gain a full pad block
eq("/abc (3+1=4, no extra pad)",
  encodeMessage("/abc", []),
  bytes("2f 61 62 63 00 00 00 00  2c 00 00 00"));

eq("/oscillator/4/frequency ,f 440.0",
  encodeMessage("/oscillator/4/frequency", [{ type: "f", value: 440.0 }]),
  bytes(`2f 6f 73 63 69 6c 6c 61 74 6f 72 2f 34 2f 66 72 65 71 75 65 6e 63 79 00
         2c 66 00 00  43 dc 00 00`));

// The worked example from the OSC 1.0 spec
eq("/foo ,iisff 1000 -1 hello 1.234 5.678",
  encodeMessage("/foo", [1000, -1, "hello", { type: "f", value: 1.234 }, { type: "f", value: 5.678 }]),
  bytes(`2f 66 6f 6f 00 00 00 00
         2c 69 69 73 66 66 00 00
         00 00 03 e8
         ff ff ff ff
         68 65 6c 6c 6f 00 00 00
         3f 9d f3 b6
         40 b5 b2 2d`));

eq("bundle wrapping one message",
  encodeBundle([encodeMessage("/s/m", [])]),
  bytes(`23 62 75 6e 64 6c 65 00
         00 00 00 00 00 00 00 01
         00 00 00 0c
         2f 73 2f 6d 00 00 00 00  2c 00 00 00`));

console.log("\nType inference");
eq("bare 1 -> int",      decodePacket(encodeMessage("/x", [1])).args,      [{ type: "i", value: 1 }]);
eq("bare 0.5 -> float",  decodePacket(encodeMessage("/x", [0.5])).args,    [{ type: "f", value: 0.5 }]);
eq("bare 'hi' -> string",decodePacket(encodeMessage("/x", ["hi"])).args,   [{ type: "s", value: "hi" }]);
eq("true -> T",          decodePacket(encodeMessage("/x", [true])).args,   [{ type: "T", value: true }]);
eq("forced f:1",         decodePacket(encodeMessage("/x", [{ type: "f", value: 1 }])).args,
                                                                          [{ type: "f", value: 1 }]);

console.log("\nRound trips");
{
  const cases = [
    ["/d/13", [1]],
    ["/a/0", []],
    ["/d/9", [{ type: "f", value: 0.5 }]],
    ["/tone/2", [440, 500]],
    ["/s/l", [1]],
    ["/mixed", [1, { type: "f", value: -2.5 }, "abcd", true, false, null]],
    ["/empty/string/arg", [""]],
    ["/unicode", ["café ✓"]],
    ["/big", [2147483647, -2147483648]],
  ];
  for (const [addr, args] of cases) {
    const p = decodePacket(encodeMessage(addr, args));
    const reencoded = encodeMessage(p.address, p.args);
    eq(`round trip ${addr}`, hex(reencoded), hex(encodeMessage(addr, args)));
  }
  // blob
  const blob = new Uint8Array([0xc0, 0xdb, 0x00, 0xff, 0x42]);
  const p = decodePacket(encodeMessage("/blob", [blob]));
  eq("blob survives", hex(p.args[0].value), hex(blob));

  // nested bundle
  const nested = encodeBundle([
    encodeMessage("/a", [1]),
    encodeBundle([encodeMessage("/b", [2]), encodeMessage("/c", ["x"])]),
  ]);
  const d = decodePacket(nested);
  eq("bundle: 2 elements", d.packets.length, 2);
  eq("bundle: first is /a", d.packets[0].address, "/a");
  eq("bundle: nested is a bundle", d.packets[1].bundle, true);
  eq("bundle: nested /c arg", d.packets[1].packets[1].args[0].value, "x");
}

console.log("\nSLIP framing");
{
  // 0xC0 and 0xDB in the payload must be escaped; frame ends with a single END
  const payload = new Uint8Array([0x01, 0xc0, 0x02, 0xdb, 0x03]);
  eq("escapes END and ESC",
    slipEncode(payload),
    bytes("01 db dc 02 db dd 03 c0"));

  eq("no leading END", slipEncode(new Uint8Array([0x41]))[0], 0x41);

  // decode helper
  function decodeAll(chunks) {
    const out = [];
    const d = new SlipDecoder();
    const controller = { enqueue: v => out.push(v) };
    for (const c of chunks) d.transform(c, controller);
    return out;
  }

  eq("decode restores payload", hex(decodeAll([slipEncode(payload)])[0]), hex(payload));

  // frames must survive arbitrary chunk boundaries, including splitting an
  // escape pair across two reads (exactly what Web Serial will do to you)
  const original = encodeMessage("/esc", [new Uint8Array([0xc0, 0xdb, 0xc0])]);
  const framed = slipEncode(original);
  let bad = null;
  for (let split = 1; split < framed.length; split++) {
    const got = decodeAll([framed.slice(0, split), framed.slice(split)]);
    if (got.length !== 1 || hex(got[0]) !== hex(original)) { bad = split; break; }
  }
  if (bad === null) { pass++; console.log(`  ok   survives all ${framed.length - 1} chunk boundaries`); }
  else { fail++; console.log(`  FAIL chunk split at byte ${bad}`); }

  // byte-at-a-time delivery
  const byByte = decodeAll(Array.from(framed, b => new Uint8Array([b])));
  eq("byte-at-a-time delivery", byByte.length, 1);
  eq("byte-at-a-time content", decodePacket(byByte[0]).address, "/esc");

  // leading END / empty frames are skipped, back-to-back frames both emerge.
  // CNMAT's beginPacket() emits a leading END, so this is the real wire shape.
  const noisy = new Uint8Array([0xc0, 0xc0, ...slipEncode(encodeMessage("/a", [])), ...slipEncode(encodeMessage("/b", []))]);
  const frames = decodeAll([noisy]);
  eq("empty frames skipped, 2 real frames", frames.length, 2);
  eq("frame 1", decodePacket(frames[0]).address, "/a");
  eq("frame 2", decodePacket(frames[1]).address, "/b");
}

console.log(`\n${pass} passed, ${fail} failed\n`);
process.exit(fail ? 1 : 0);

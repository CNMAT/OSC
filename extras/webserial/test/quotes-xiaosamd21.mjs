/* Checks the quotations XiaoSamd21Oscuino's I2C tab attributes to a source
   (14373d4 on: the page's I2C pane and Nunchuk notes, the sketch's WII
   EXTENSIONS notes, WiiExtension.h, ADDRESSES.md's wii fields), and those of
   KB2040Oscuino's page and sketch, which carry that work to another board,
   against that source, verbatim: NintendoExtensionCtrl's installed files, and
   two web pages, fetched. The XIAO's /cap tab's quotations (99b4d17) are not
   in this table.

   Each entry is the quotation exactly as written between its marks (<q> and
   </q>, or double quotes), and it must stand between those marks in every
   file the entry names, so a quotation edited in a file and not here fails.
   Every <q> in either page's I2C pane must have an entry, and every quoted
   comment in
   WiiExtension.h, a run of comment lines read as one, is checked against
   the library's files. Text is compared with entities decoded, curly quotes
   made straight and whitespace runs made one space; tags are removed from
   the web pages, comment leaders from the repository's files. The controls,
   a web and a library entry each with a letter changed, must be found
   neither in their file nor in their source, so a pass is not an empty
   search.

   Run: node test/quotes-xiaosamd21.mjs   Needs the network; a source that
   cannot be read fails the run. NXC_SRC as in test-xiaosamd21-contract.mjs. */

import { readFileSync, readdirSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";

const ROOT = new URL("../../../", import.meta.url).pathname;
const HTML = "examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.html",
      INO = "examples/XiaoSamd21Oscuino/XiaoSamd21Oscuino.ino",
      WII = "examples/XiaoSamd21Oscuino/WiiExtension.h", MD = "ADDRESSES.md",
      KHTML = "examples/KB2040Oscuino/KB2040Oscuino.html", KINO = "examples/KB2040Oscuino/KB2040Oscuino.ino";
const NXC = process.env.NXC_SRC ||
  join(homedir(), "Documents", "Arduino", "libraries", "Nintendo_Extension_Ctrl", "src", "controllers");
const ADAFRUIT = "https://learn.adafruit.com/adafruit-wii-nunchuck-breakout-adapter/pinouts";
const SEEED = "https://wiki.seeedstudio.com/Seeeduino-XIAO/";
const LIB = "the library's src/controllers", PROPS = "the library's library.properties";

// [the quotation as written, the files that make it, its source]
const QUOTES = [
  ["give it the same power as the logic level of your microcontroller", [HTML, KHTML, KINO], ADAFRUIT],
  ["the same power as the logic level of your microcontroller", [HTML], ADAFRUIT],
  ["the 3.3V output from the voltage regulator", [HTML], ADAFRUIT],
  ["notched side of the connector U goes on the side that says Notch Up", [KHTML], ADAFRUIT],
  ["either version 3 of the License, or (at your option) any later version", [HTML, KHTML], LIB],
  ["Voltage input connected to general I/O pins may cause chip damage if it' higher than 3.3V",
   [HTML, INO], SEEED],
  ["6 bits, -30-29", [HTML, MD, WII, KHTML], LIB],
  ["Inverted so pulling back is a positive pitch", [HTML], LIB],
  ["Nintendo Extension Ctrl", [INO, WII, KINO], PROPS],
];

const ENT = { amp: "&", lt: "<", gt: ">", quot: '"', apos: "'", nbsp: " ", rsquo: "'", lsquo: "'",
              rdquo: '"', ldquo: '"', ndash: "–", mdash: "—", hellip: "…" };
const decode = s => s.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (m, e) =>
  e[0] !== "#" ? ENT[e.toLowerCase()] ?? m
    : String.fromCodePoint(e[1] === "x" || e[1] === "X" ? parseInt(e.slice(2), 16) : +e.slice(1)));
const plain = s => decode(s).replace(/[‘’]/g, "'").replace(/[“”]/g, '"')
  .replace(/ /g, " ").replace(/\s+/g, " ");
const fromWeb = s => plain(s.replace(/<[^>]*>/g, ""));
const fromRepo = s => plain(s.split("\n").map(l => l.replace(/^\s*(\*|\/\/)\s?/, "")).join("\n"));
const marked = (text, q) => text.includes("<q>" + q + "</q>") || text.includes('"' + q + '"');

let passes = 0, fails = 0;
const ok = (name, cond, got = "") => {
  cond ? passes++ : fails++;
  console.log(`  ${cond ? "ok  " : "FAIL"} ${name}${cond || !got ? "" : "  -- " + got}`);
};

const repo = {};
for (const f of [HTML, INO, WII, MD, KHTML, KINO]) repo[f] = fromRepo(readFileSync(join(ROOT, f), "utf8"));

const sources = {};
async function fetchText(url) {
  for (let attempt = 1; ; attempt++) {
    try {
      const r = await fetch(url, { redirect: "follow", signal: AbortSignal.timeout(30000) });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      return fromWeb(await r.text());
    } catch (e) {
      if (attempt === 2) throw e;
    }
  }
}
for (const url of [ADAFRUIT, SEEED]) {
  try { sources[url] = [await fetchText(url)]; }
  catch (e) { ok(`fetch ${url}`, false, e.message); }
}
try {
  sources[LIB] = readdirSync(NXC).map(f => fromRepo(readFileSync(join(NXC, f), "utf8")));
  sources[PROPS] = [plain(readFileSync(join(NXC, "..", "..", "library.properties"), "utf8"))];
} catch (e) { ok(`read ${NXC}`, false, e.message); }
const inSource = (src, q) => (sources[src] || []).some(t => t.includes(q));

console.log("each quotation in the files that make it, and in its source:");
for (const [q, files, src] of QUOTES) {
  for (const f of files) ok(`${f.split("/").pop()}: "${q}"`, marked(repo[f], q), "not between quotation marks there");
  if (sources[src]) ok(`  in ${src}`, inSource(src, q), "not found");
}

console.log("every <q> in each page's I2C pane has an entry:");
for (const [file, opener] of [[HTML, '<div class="i2cpane"'], [KHTML, '<div class="panel params" id="wiiPane">']]) {
  const raw = readFileSync(join(ROOT, file), "utf8");
  const at = raw.indexOf(opener), end = raw.indexOf('<div class="panel">', at);
  const pane = [...raw.slice(at, end).matchAll(/<q>([\s\S]*?)<\/q>/g)].map(m => plain(m[1]));
  ok(`${file.split("/").pop()}: the pane found, with ${pane.length} quotations`, at >= 0 && end > at && pane.length > 0);
  for (const q of pane) ok(`"${q}"`, QUOTES.some(e => e[0] === q && e[1].includes(file)), "no entry naming this file");
}

console.log("every quoted comment in WiiExtension.h, in the library's files:");
const runs = [];   // consecutive lines' comments, joined, so a quotation may run on
let prev = false;
for (const l of readFileSync(join(ROOT, WII), "utf8").split("\n")) {
  const c = l.indexOf("//");
  if (c >= 0 && prev) runs[runs.length - 1] += " " + l.slice(c + 2);
  else if (c >= 0) runs.push(l.slice(c + 2));
  prev = c >= 0;
}
const comments = [...new Set(runs.flatMap(r => [...r.matchAll(/"([^"]+)"/g)]).map(m => plain(m[1])))];
ok(`${comments.length} quoted comments found`, comments.length > 0);
if (sources[LIB]) for (const q of comments) ok(`"${q}"`, inSource(LIB, q), "not found");

console.log("controls:");
for (const [q0, from, to] of [[QUOTES[0][0], "power", "pover"], ["Inverted so pulling back is a positive pitch", "back", "bock"]]) {
  const [, files0, src0] = QUOTES.find(e => e[0] === q0), bent = q0.replace(from, to);
  ok(`"${bent}" is in neither ${files0[0].split("/").pop()} nor its source`,
     bent !== q0 && !marked(repo[files0[0]], bent) && !!sources[src0] && !inSource(src0, bent));
}

console.log(`quotes-xiaosamd21: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);

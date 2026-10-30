/* Grades test-serial-close.mjs by mutation, on the XIAO SAMD21 page: each
   mutant undoes one thing the page does to let go of its port, and only of its
   own, in a scratch copy of the files the test reads. The test must fail on
   every one, at the check named beside it, and the unmutated copy must pass,
   so a failure below is the mutant's and not the copy's.

   Run: node test/mutants-serial-close.mjs   No dependencies. */

import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, copyFileSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { tmpdir } from "node:os";
import { join } from "node:path";

const ROOT = new URL("../../../", import.meta.url).pathname;
const TEST = "extras/webserial/test/test-serial-close.mjs";
// Every page the test loads; the hand-written ones as the test itself lists them.
const HAND = JSON.parse(readFileSync(join(ROOT, TEST), "utf8").match(/const HAND = (\[[^\]]*\]);/)[1]);
const FILES = [TEST, "extras/webserial/oscuino.html", ...HAND.map(n => `examples/${n}/${n}.html`)];
const PAGE = "XiaoSamd21Oscuino", HTML = `examples/${PAGE}/${PAGE}.html`;

const MUTANTS = [
  // name, the check that must fail, the page's text, what replaces it
  ["disconnect() closes without waiting for the pipe", "Disconnect closes the port",
   "  try { if (pipeClosed) await pipeClosed; } catch {}\n", ""],
  ["the page reads through pipeThrough(), whose pipe it cannot wait for", "Disconnect closes the port",
   "  pipeClosed = port.readable.pipeTo(slip.writable).catch(() => {});\n  reader = slip.readable.getReader();",
   "  reader = port.readable.pipeThrough(slip).getReader();"],
  ["disconnect() keeps the writer's lock", "Disconnect closes the port",
   "  try { if (writer) writer.releaseLock(); } catch {}\n", ""],
  ["the page holds the port before it has opened", "a port that would not open is not held",
   "  await chosen.open({ baudRate: 115200, bufferSize: 8192 });   // a port that will not open is never ours\n  port = chosen;",
   "  port = chosen;\n  await chosen.open({ baudRate: 115200, bufferSize: 8192 });"],
  ["any device unplugged ends the connection", "another device unplugged leaves this port open",
   "if (port && e.target === port) disconnect();", "if (port) disconnect();"],
  ["the page ignores its own port's unplug", "this port unplugged lets it go",
   "if (port && e.target === port) disconnect();", "if (port && e.target === port) return;"],
  ["the page forgets its own port on unplug without closing it", "this port unplugged lets it go",
   "if (port && e.target === port) disconnect();", "if (port && e.target === port) port = null;"],
  ["the page hides a close() that fails", "a failed close() is reported",
   '} catch (e) { logLine("warn", "port close failed: " + e.message); }', "} catch {}"],
];

function copyTree(dir) {
  for (const f of FILES) {
    mkdirSync(join(dir, f, ".."), { recursive: true });
    copyFileSync(join(ROOT, f), join(dir, f));
  }
}
// The whole test takes under half a second here (node 25.3.0). A mutant whose
// disconnect() waits on a promise that never settles ends the test as soon as
// nothing else is pending, Node exiting 13 for an unsettled top-level await;
// one that keeps a timer alive is stopped at 30 s. Either way the test prints
// no FAIL for the check, so the mutant counts as not caught.
const run = dir => spawnSync(process.execPath, [join(dir, TEST)], { encoding: "utf8", timeout: 30000 });
// What the test printed for this page: from its name to the next blank line.
const section = out => (out.split(`\n${PAGE}\n`)[1] || "").split("\n\n")[0];

let bad = 0;
const control = mkdtempSync(join(tmpdir(), "serial-close-control-"));
copyTree(control);
const c = run(control);
console.log(`${c.status === 0 ? "ok  " : "FAIL"}  unmutated copy passes`);
if (c.status !== 0) bad++;
rmSync(control, { recursive: true });

for (const [name, check, from, to] of MUTANTS) {
  const dir = mkdtempSync(join(tmpdir(), "serial-close-mutant-"));
  copyTree(dir);
  const p = join(dir, HTML), s = readFileSync(p, "utf8");
  const n = s.split(from).length - 1;
  if (n !== 1) {
    console.log(`FAIL  ${name}: mutation site found ${n} times, not once`);
    bad++; rmSync(dir, { recursive: true }); continue;
  }
  writeFileSync(p, s.replace(from, to));
  const r = run(dir);
  const caught = r.status !== 0 && section(r.stdout).includes(`  FAIL ${check}`);
  console.log(`${caught ? "ok  " : "FAIL"}  ${name}  ${caught ? "<- " : "NOT CAUGHT at "}${check}` +
              (r.status === null ? ` (${r.signal})` : ""));
  if (!caught) bad++;
  rmSync(dir, { recursive: true });
}
console.log(`\n${MUTANTS.length} mutants; ${bad ? bad + " problem(s): FAIL" : "all caught, control passes"}`);
if (bad) process.exit(1);

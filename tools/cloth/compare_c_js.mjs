// Compare the C skirt solver against the JS reference, frame by frame, and score the
// C output with the same probe metrics as ClothResearch/tools/skirt_replay.js.
//
//   node tools/cloth/compare_c_js.mjs --dump <file.skd> --c <positions.bin> [--seconds N] [--json out.json] [--label name]
//
// <positions.bin> is written by skirt_replay (SKC1: u32 nVerts, u32 nFrames, then per frame
// u32 frameIndex + f32 pos[nVerts*3]). The JS solver is run here through the ClothResearch
// replay() with an onFrame hook, so it sees exactly the frames and matrices it always does;
// the hook reads the matching C frame and compares / scores it.
//
// ClothResearch is read-only for us: its modules are imported by file URL from the checkout
// $CLOTH_RESEARCH names (no default).

import fs from 'node:fs';
import { pathToFileURL } from 'node:url';

if (!process.env.CLOTH_RESEARCH) { console.error('set CLOTH_RESEARCH to the cloth research checkout'); process.exit(2); }
const CR = pathToFileURL(`${process.env.CLOTH_RESEARCH}/`);
const mod = (p) => new URL(p, CR).href;
const { openDump, skinMatrices, skin } = await import(mod('skirt/chaos_dump.js'));
const { ProbeMetrics, vertexNormals, buildGrid } = await import(mod('skirt/probe_metrics.js'));
const { handCapsules, capsulePoints, capsuleDepth } = await import(mod('skirt/skirt_sim.js'));
const { replay } = await import(mod('tools/skirt_replay.js'));

const args = Object.fromEntries(process.argv.slice(2).reduce((a, x, i, all) => {
  if (x.startsWith('--')) a.push([x.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return a;
}, []));
if (!args.dump || !args.c) { console.error('usage: node compare_c_js.mjs --dump f.skd --c pos.bin [--seconds N] [--json out.json] [--label name]'); process.exit(2); }
const seconds = args.seconds ? +args.seconds : Infinity;
const label = args.label || 'C';
const params = args.params ? JSON.parse(args.params) : {};

// ---- the C output ----
const cfd = fs.openSync(args.c, 'r');
const chead = Buffer.alloc(12); fs.readSync(cfd, chead, 0, 12, 0);
if (chead.toString('latin1', 0, 4) !== 'SKC1') throw new Error('not an SKC1 file');
const cN = chead.readUInt32LE(4), cFrames = chead.readUInt32LE(8);
const h = await openDump(args.dump);
const m = h.meta, N = m.nVerts;
if (cN !== N) throw new Error(`vertex count mismatch ${cN} vs ${N}`);
const recBytes = 4 + N * 12;
const recBuf = Buffer.alloc(recBytes);
let cRead = 0;
const cPos = new Float32Array(N * 3);
function readC(frameIndex) {
  if (cRead >= cFrames) throw new Error('C file has fewer frames than the JS replay');
  fs.readSync(cfd, recBuf, 0, recBytes, 12 + cRead * recBytes); cRead++;
  const idx = recBuf.readUInt32LE(0);
  if (idx !== frameIndex) throw new Error(`frame index mismatch: C ${idx} vs JS ${frameIndex}`);
  new Uint8Array(cPos.buffer).set(recBuf.subarray(4, recBytes));
  return cPos;
}

// ---- C-side scoring: the same objects replay() builds for the JS solver ----
class HandPen {   // as in tools/skirt_replay.js
  constructor(hd) { this.caps = handCapsules(hd); this.pts = this.caps ? new Float64Array(this.caps.n * 6) : null; this.md = hd.cloth.maxDist; this.sum = 0; this.n = 0; this.worst = 0; this.next = 0; }
  frame(t, sim, mats) {
    if (!this.caps || t + 1e-6 < this.next) return;
    this.next += 0.1;
    capsulePoints(this.caps, mats, this.pts);
    let c = 0;
    for (let v = 0; v < this.md.length; v++) {
      if (this.md[v] <= 0) continue;
      const d = capsuleDepth(this.caps, this.pts, sim[v * 3], sim[v * 3 + 1], sim[v * 3 + 2]);
      if (d > 0) { c++; if (d > this.worst) this.worst = d; }
    }
    this.sum += c; this.n++;
  }
  summary() { return { handPenCount: this.n ? this.sum / this.n : 0, handPenWorst: this.worst }; }
}
const tm = h.trimeshes[0];
const colPos = tm ? new Float32Array(tm.pos.length) : null, colNrm = tm ? new Float32Array(tm.pos.length) : null;
const collider = tm ? { nrm: colNrm, grid: null, skinned() { vertexNormals(colPos, tm.tri, colNrm); this.grid = buildGrid(colPos); return colPos; } } : null;
const cloth = { pos: h.cloth.pos, maxDist: h.cloth.maxDist, hemZ: m.hemZ };
const oursC = new ProbeMetrics(cloth, collider);
const handC = new HandPen(h);
const mats = new Float32Array(m.nBones * 16);
const pelvis = new Float32Array(3);

// ---- per-frame comparison ----
const nMax = Math.min(h.nFrames, Math.round(seconds * 60));
const maxDiff = new Float64Array(nMax).fill(NaN), rmsDiff = new Float64Array(nMax).fill(NaN), tFrame = new Float64Array(nMax).fill(NaN);
let nCmp = 0, cBlow = 0;
const THR = [1e-3, 1e-2, 1e-1, 1, 10];
const first = THR.map(() => null);
let worstFrame = -1;

function onFrame(i, f, jsOut, sk) {
  const c = readC(i);
  let mx = 0, sq = 0;
  for (let v = 0; v < N; v++) {
    const dx = jsOut[v * 3] - c[v * 3], dy = jsOut[v * 3 + 1] - c[v * 3 + 1], dz = jsOut[v * 3 + 2] - c[v * 3 + 2];
    const d2 = dx * dx + dy * dy + dz * dz;
    sq += d2; const d = Math.sqrt(d2); if (!Number.isFinite(d)) mx = Infinity; else if (d > mx) mx = d;
  }
  const r = Math.sqrt(sq / N);
  maxDiff[i] = mx; rmsDiff[i] = r; tFrame[i] = f.elapsed; nCmp++;
  if (!Number.isFinite(mx)) cBlow++;
  THR.forEach((th, k) => { if (first[k] === null && mx > th) first[k] = { frame: i, t: f.elapsed, maxDiff: mx }; });
  // C metrics, as replay() does for the solver output
  skinMatrices(h.refInv, f.cst, m.nBones, mats);
  const p = m.pelvisBone * 16; pelvis[0] = f.cst[p + 12]; pelvis[1] = f.cst[p + 13]; pelvis[2] = f.cst[p + 14];
  const t = f.elapsed;
  if (tm && t + 1e-6 >= oursC.next) skin(tm.pos, tm.skinB, tm.skinW, mats, colPos);
  oursC.frame(t, c, sk, pelvis);
  handC.frame(t, c, mats);
}

const t0 = Date.now();
const js = await replay({ dump: h, solver: 'sim', seconds, params, onFrame, quiet: true, chaos: false });
const wall = (Date.now() - t0) / 1000;
const cm = { ...oursC.summary(), ...handC.summary() };
const jm = js.ours;

// ---- summaries ----
function windowStats(t0s, t1s) {
  let mx = 0, sq = 0, n = 0;
  for (let i = 0; i < nMax; i++) {
    const t = tFrame[i]; if (!(t >= t0s && t < t1s)) continue;
    if (maxDiff[i] > mx) mx = maxDiff[i];
    sq += rmsDiff[i] * rmsDiff[i]; n++;
  }
  return { frames: n, maxDiff: mx, rms: n ? Math.sqrt(sq / n) : 0 };
}
const total = windowStats(0, Infinity);
const horizons = [0.5, 1, 2, 5, 10, 20, 30, 60, 90, 120, 135].filter((x) => x <= js.seconds + 0.01 || x === 0.5).map((x) => ({ upToSeconds: x, ...windowStats(0, x) }));
const perSecond = [];
for (let s = 0; s < Math.ceil(js.seconds); s++) perSecond.push({ s, ...windowStats(s, s + 1) });

const keys = [['mean_cm', 'mean'], ['hem_mean_cm', 'hem'], ['max_cm', 'max'], ['jerk cm/s', 'jerk'], ['hem radius x', 'puff'], ['hem drop cm', 'hemDropSim'],
  ['pen mean_of_max cm', 'penMeanOfMax'], ['pen worst cm', 'penWorst'], ['pen count/sample', 'penMeanCount'], ['hand pen count/sample', 'handPenCount'], ['hand pen worst cm', 'handPenWorst']];
const rows = keys.map(([name, k]) => ({ name, js: jm[k], c: cm[k], pct: jm[k] ? (cm[k] / jm[k] - 1) * 100 : (cm[k] === jm[k] ? 0 : NaN) }));

const result = {
  label, dump: args.dump, cFile: args.c, seconds: js.seconds, framesCompared: nCmp, cFramesInFile: cFrames,
  jsMsPerStep: js.msPerStep, jsBlowups: js.blowups, wallSeconds: wall,
  firstExceed: THR.map((th, k) => ({ thresholdCm: th, ...(first[k] || { frame: null, t: null, maxDiff: null }) })),
  total, horizons, perSecond, metrics: rows,
  jsSummary: jm, cSummary: cm,
};
if (args.json) fs.writeFileSync(args.json, JSON.stringify(result, null, 1));

const f4 = (x, d = 4) => (typeof x === 'number' ? x.toFixed(d) : String(x));
console.log(`\n[${label}] C vs JS over ${js.seconds.toFixed(1)} s (${nCmp} frames compared; JS ${js.msPerStep.toFixed(3)} ms/step, ${js.blowups} JS blow-ups)`);
console.log(`overall: max per-vertex diff ${f4(total.maxDiff, 6)} cm, RMS ${f4(total.rms, 6)} cm`);
for (const e of result.firstExceed) console.log(`  first frame with max diff > ${e.thresholdCm} cm: ${e.frame === null ? 'never' : `frame ${e.frame} (t = ${f4(e.t, 3)} s, ${f4(e.maxDiff, 5)} cm)`}`);
console.log('cumulative:');
for (const x of horizons) console.log(`  up to ${String(x.upToSeconds).padStart(5)} s   max ${f4(x.maxDiff, 6).padStart(12)} cm   rms ${f4(x.rms, 6).padStart(12)} cm`);
console.log(`${''.padEnd(24)} ${'JS'.padStart(10)} ${label.padStart(10)}   C/JS-1`);
for (const r of rows) console.log(`${r.name.padEnd(24)} ${f4(r.js, 3).padStart(10)} ${f4(r.c, 3).padStart(10)}   ${Number.isFinite(r.pct) ? r.pct.toFixed(2).padStart(7) + '%' : '    n/a'}`);

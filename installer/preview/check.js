// Runs liquid.js under Node with its index checks on: draws a few seconds of
// frames into two buffers that take turns, as on the Vita, checks each buffer
// ends up the same as one drawn from scratch (so no tile is left stale), and
// writes a few frames out as raw RGBA for a look.
//   node check.js <out folder>
'use strict';
const fs = require('fs');
const path = require('path');
const { createLiquid, SCREEN_W, SCREEN_H } = require('./liquid.js');

const N = SCREEN_W * SCREEN_H;
const RGB = (r, g, b) => ((b << 16) | (g << 8) | r) >>> 0;

// gfx.c's draw_base
function drawBase(base) {
	const bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]];
	const top = [14, 16, 24], bottom = [28, 34, 62];
	for (let y = 0; y < SCREEN_H; y++) {
		const v = [0, 1, 2].map((ch) => top[ch] * 256 + (((bottom[ch] - top[ch]) * 256 * y) / (SCREEN_H - 1) | 0));
		for (let x = 0; x < SCREEN_W; x++) {
			const d = bayer[y & 3][x & 3] * 16;
			base[y * SCREEN_W + x] = (0xFF000000 | RGB((v[0] + d) >> 8, (v[1] + d) >> 8, (v[2] + d) >> 8)) >>> 0;
		}
	}
}

// gfx.c's over_px, gfx_panel and gfx_end
function overPx(overlay, p, rgb, a) {
	if (a <= 0)
		return;
	if (a > 255)
		a = 255;
	const d = overlay[p];
	const sa = a + (a >> 7), keep = 256 - sa;
	const r = (((rgb & 0xFF) * sa) >> 8) + (((d & 0xFF) * keep) >> 8);
	const g = ((((rgb >> 8) & 0xFF) * sa) >> 8) + ((((d >> 8) & 0xFF) * keep) >> 8);
	const b = ((((rgb >> 16) & 0xFF) * sa) >> 8) + ((((d >> 16) & 0xFF) * keep) >> 8);
	const da = a + (((d >>> 24) * keep) >> 8);
	overlay[p] = (((da > 255 ? 255 : da) << 24) | RGB(Math.min(r, 255), Math.min(g, 255), Math.min(b, 255))) >>> 0;
}

function insideRounded(px, py, cx, cy, hw, hh, radius) {
	const qx = Math.abs(px - cx) - (hw - radius), qy = Math.abs(py - cy) - (hh - radius);
	const ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
	const outside = ox > 0 && oy > 0 ? Math.sqrt(ox * ox + oy * oy) : ox + oy;
	const inner = qx > qy ? qx : qy;
	return radius - outside - (inner < 0 ? inner : 0);
}

function panel(overlay, x, y, w, h, radius, rgb, alpha) {
	const cx = x + w * 0.5, cy = y + h * 0.5;
	for (let j = Math.max(0, y); j < y + h && j < SCREEN_H; j++)
		for (let i = Math.max(0, x); i < x + w && i < SCREEN_W; i++) {
			const d = insideRounded(i + 0.5, j + 0.5, cx, cy, w * 0.5, h * 0.5, radius);
			if (d <= -0.5)
				continue;
			const cover = d >= 0.5 ? 1.0 : d + 0.5;
			const p = j * SCREEN_W + i;
			overPx(overlay, p, rgb, (alpha * cover) | 0);
			const edge = 1.0 - Math.abs(d - 1.0);
			if (edge > 0) {
				const strength = 26 + ((22 * (y + h - j)) / h | 0);
				overPx(overlay, p, RGB(255, 255, 255), (strength * edge) | 0);
			}
		}
}

function compose(base, overlay, scene) {
	for (let p = 0; p < N; p++) {
		const o = overlay[p], b = base[p];
		const oa = o >>> 24;
		if (!oa) {
			scene[p] = b;
			continue;
		}
		const keep = 256 - oa - (oa >> 7);
		const r = (o & 0xFF) + (((b & 0xFF) * keep) >> 8);
		const g = ((o >> 8) & 0xFF) + ((((b >> 8) & 0xFF) * keep) >> 8);
		const bl = ((o >> 16) & 0xFF) + ((((b >> 16) & 0xFF) * keep) >> 8);
		scene[p] = (0xFF000000 | RGB(Math.min(r, 255), Math.min(g, 255), Math.min(bl, 255))) >>> 0;
	}
}

const out = process.argv[2] || '.';
fs.mkdirSync(out, { recursive: true });
const base = new Uint32Array(N), overlay = new Uint32Array(N), scene = new Uint32Array(N);
drawBase(base);
const PANEL = RGB(10, 12, 20), ACCENT = RGB(0x7C, 0xC0, 0xFF);
const PANEL_ALPHA = 92;
panel(overlay, 32, 132, 404, 316, 20, PANEL, PANEL_ALPHA);
panel(overlay, 456, 132, 472, 316, 20, PANEL, PANEL_ALPHA);
panel(overlay, 32, 464, 896, 46, 14, PANEL, PANEL_ALPHA);
panel(overlay, 46, 190, 376, 40, 12, ACCENT, 64);
compose(base, overlay, scene);
const layers = { base, overlay, scene };

const liquid = createLiquid(true);
const fbs = [new Uint32Array(N), new Uint32Array(N)];
const lastT = [0, 0];
const FRAMES = 1500, FPS = 60;
const dumps = new Set([90, 400, 700, 1000, 1300]);
let maxShaded = 0, sumShaded = 0, maxTiles = 0, sumTiles = 0, maxCells = 0, maxCopied = 0;
const started = Date.now();
for (let n = 0; n < FRAMES; n++) {
	const buf = n & 1, t = n / FPS;
	const stats = liquid.frame(fbs[buf], buf, t, n < 2, layers);
	lastT[buf] = t;
	maxShaded = Math.max(maxShaded, stats.shaded);
	sumShaded += stats.shaded;
	maxTiles = Math.max(maxTiles, stats.tiles);
	sumTiles += stats.tiles;
	maxCells = Math.max(maxCells, stats.cells);
	maxCopied = Math.max(maxCopied, stats.copied);
	if (dumps.has(n))
		fs.writeFileSync(path.join(out, 'frame' + n + '.rgba'), Buffer.from(fbs[buf].buffer.slice(0)));
}
console.log('%d frames with every index in range, %d ms each in Node', FRAMES, ((Date.now() - started) / FRAMES).toFixed(2));
console.log('tiles drawn a frame: %d on average, %d at most (of 2040)', Math.round(sumTiles / FRAMES), maxTiles);
console.log('pixels shaded a frame: %d on average, %d at most; lit cells at most %d; pixels restored at most %d',
	Math.round(sumShaded / FRAMES), maxShaded, maxCells, maxCopied);

// Each buffer, drawn tile by tile over 750 frames, against one drawn whole.
for (let buf = 0; buf < 2; buf++) {
	const whole = new Uint32Array(N);
	createLiquid(true).frame(whole, 0, lastT[buf], true, layers);
	let differ = 0;
	for (let p = 0; p < N; p++)
		if (whole[p] !== fbs[buf][p])
			differ++;
	console.log('buffer %d against a whole redraw at t=%s: %d pixels differ', buf, lastT[buf].toFixed(3), differ);
	if (differ)
		process.exitCode = 1;
}

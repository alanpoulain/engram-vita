// Draws a still of the installer's liquid over a base image, for the LiveArea artwork.
//   node render_liquid.js <base.rgba> <out.rgba> <colour time> <x,y,r> [<x,y,r> ...]
'use strict';
const fs = require('fs');
const { createLiquid, SCREEN_W, SCREEN_H } = require('../preview/liquid.js');
const [basePath, outPath, time, ...ballArgs] = process.argv.slice(2);
const N = SCREEN_W * SCREEN_H;
const raw = fs.readFileSync(basePath);
const base = new Uint32Array(raw.buffer.slice(raw.byteOffset, raw.byteOffset + N * 4));
const overlay = new Uint32Array(N), fb = new Uint32Array(N);
const balls = ballArgs.map((a) => a.split(',').map(Number));
createLiquid(true).frame(fb, 0, Number(time), true, { base, overlay, scene: base }, balls);
fs.writeFileSync(outPath, Buffer.from(fb.buffer));

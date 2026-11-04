/* scl-codec.js — Scala .scl parser and the compact binary scale library for the FM-1 microtonal firmware.
 *
 * Library format, version 1 (little endian):
 *   header  8 bytes   'M','T','S','1', count u16, reserved u16 (0)
 *   index   8 bytes per scale, sorted by degree count, then period:
 *             offset u32 (from file start), degrees u8, type u8, period u16 (in units of the type)
 *   records one per scale, at its offset:
 *             nameLen u8, name (ASCII, up to 16 bytes)
 *             type 0 (TABLE16): degrees x u16, cents x 16   (0 .. 4095.9375 cents), last value = the period
 *             type 1 (EQUAL):   nothing more: the period in the index, the degrees equally spaced
 *             type 2 (TABLE4):  degrees x u16, cents x 4    (0 .. 16383.75 cents), for very wide periods
 * The decoder on the device only needs the index to search and one record to load a scale.
 */
(function (root) {
  'use strict';

  const MAGIC = [0x4d, 0x54, 0x53, 0x31]; // "MTS1"
  const HEADER = 8, INDEX_ENTRY = 8, NAME_MAX = 16, MAX_DEGREES = 255;
  const T_TABLE16 = 0, T_EQUAL = 1, T_TABLE4 = 2;
  const UNIT = { 0: 16, 1: 16, 2: 4 };
  const EQUAL_TOL = 0.01; // cents: a scale this close to equal steps is stored as EQUAL

  class SclError extends Error {}

  /* one pitch line → cents. Scala: a '.' means cents, otherwise a ratio a/b or a whole number a. */
  function pitchToCents(line, lineNo) {
    const tok = line.trim().split(/\s+/)[0];
    if (!tok) throw new SclError(`line ${lineNo}: empty value`);
    if (tok.includes('.')) {
      if (!/^[+-]?(\d+\.?\d*|\.\d+)$/.test(tok)) throw new SclError(`line ${lineNo}: bad cents value`);
      return parseFloat(tok);
    }
    const m = /^(\d+)(?:\/(\d+))?$/.exec(tok);
    if (!m) throw new SclError(`line ${lineNo}: bad ratio`);
    const a = Number(m[1]), b = m[2] === undefined ? 1 : Number(m[2]);
    if (!(a > 0) || !(b > 0)) throw new SclError(`line ${lineNo}: a ratio must be positive`);
    return 1200 * Math.log2(a / b);
  }

  /* text of one .scl file → { description, cents: [...] }, cents[n-1] = the period */
  function parseScl(text) {
    const lines = text.replace(/\r\n?/g, '\n').split('\n');
    let i = 0, lineNo = 0;
    const next = () => {
      while (i < lines.length) {
        const l = lines[i++]; lineNo = i;
        if (!l.startsWith('!')) return l;
      }
      return null;
    };
    const description = next();
    if (description === null) throw new SclError('the file is empty');
    const countLine = next();
    if (countLine === null) throw new SclError('the number of degrees is missing');
    const count = Number(countLine.trim().split(/\s+/)[0]);
    if (!Number.isInteger(count) || count < 1) throw new SclError(`line ${lineNo}: bad number of degrees`);
    if (count > MAX_DEGREES) throw new SclError(`${count} degrees: the most is ${MAX_DEGREES}`);
    const cents = [];
    for (let k = 0; k < count; k++) {
      const l = next();
      if (l === null) throw new SclError(`degrees missing: found ${k} of ${count}`);
      cents.push(pitchToCents(l, lineNo));
    }
    return { description: description.trim(), cents };
  }

  /* a parsed scale → { type, unit, values, period } as stored */
  function classify(cents) {
    const n = cents.length, period = cents[n - 1];
    if (cents.some(c => c < 0)) throw new SclError('a degree below 0 cents is not supported');
    if (!(period > 0)) throw new SclError('the period must be above 0');
    const equal = cents.every((c, k) => Math.abs(c - period * (k + 1) / n) <= EQUAL_TOL);
    const max = Math.max(...cents);
    let type;
    if (equal && period * 16 <= 0xffff) type = T_EQUAL;
    else if (max * 16 <= 0xffff) type = T_TABLE16;
    else if (max * 4 <= 0xffff) type = T_TABLE4;
    else throw new SclError(`a degree of ${max.toFixed(1)} cents: beyond the 16383-cent range`);
    const unit = UNIT[type];
    const values = type === T_EQUAL ? [] : cents.map(c => Math.round(c * unit));
    return { type, unit, values, period: Math.round(period * unit) };
  }

  function shortName(fileName) {
    const base = String(fileName).split(/[\\/]/).pop().replace(/\.scl$/i, '');
    const ascii = base.normalize('NFKD').replace(/[^\x20-\x7e]/g, '').trim();
    return (ascii || 'scale').slice(0, NAME_MAX);
  }

  /* [{ name, text }] → { bytes, scales, errors, duplicates, textBytes } */
  function buildLibrary(files) {
    const scales = [], errors = [], seen = new Map();
    let duplicates = 0, textBytes = 0;
    for (const f of files) {
      textBytes += f.size !== undefined ? f.size : f.text.length;
      try {
        const { description, cents } = parseScl(f.text);
        const st = classify(cents);
        const key = `${st.type}:${cents.length}:${st.period}:${st.values.join(',')}`;
        if (seen.has(key)) { duplicates++; seen.get(key).aliases.push(f.name); continue; }
        const s = { file: f.name, name: shortName(f.name), description, cents, degrees: cents.length, ...st, aliases: [] };
        seen.set(key, s); scales.push(s);
      } catch (e) {
        if (!(e instanceof SclError)) throw e;
        errors.push({ file: f.name, message: e.message });
      }
    }
    if (scales.length > 0xffff) throw new SclError('more than 65535 scales');
    scales.sort((a, b) => a.degrees - b.degrees || a.period / a.unit - b.period / b.unit || a.name.localeCompare(b.name));

    const recSize = s => 1 + s.name.length + (s.type === T_EQUAL ? 0 : 2 * s.degrees);
    const total = HEADER + INDEX_ENTRY * scales.length + scales.reduce((t, s) => t + recSize(s), 0);
    const bytes = new Uint8Array(total), dv = new DataView(bytes.buffer);
    bytes.set(MAGIC, 0); dv.setUint16(4, scales.length, true); dv.setUint16(6, 0, true);
    let off = HEADER + INDEX_ENTRY * scales.length;
    scales.forEach((s, k) => {
      const ix = HEADER + INDEX_ENTRY * k;
      dv.setUint32(ix, off, true); bytes[ix + 4] = s.degrees; bytes[ix + 5] = s.type; dv.setUint16(ix + 6, s.period, true);
      s.offset = off; s.size = recSize(s) + INDEX_ENTRY;
      bytes[off++] = s.name.length;
      for (let c = 0; c < s.name.length; c++) bytes[off++] = s.name.charCodeAt(c);
      for (const v of s.values) { dv.setUint16(off, v, true); off += 2; }
    });
    return { bytes, scales, errors, duplicates, textBytes };
  }

  /* the device's view: bytes → [{ name, degrees, type, cents: [...] }] */
  function decodeLibrary(bytes) {
    const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    for (let k = 0; k < 4; k++) if (bytes[k] !== MAGIC[k]) throw new SclError('not a library file');
    const count = dv.getUint16(4, true), out = [];
    for (let k = 0; k < count; k++) {
      const ix = HEADER + INDEX_ENTRY * k;
      let off = dv.getUint32(ix, true);
      const degrees = bytes[ix + 4], type = bytes[ix + 5], unit = UNIT[type], period = dv.getUint16(ix + 6, true) / unit;
      const nl = bytes[off++];
      const name = String.fromCharCode(...bytes.subarray(off, off + nl)); off += nl;
      const cents = [];
      for (let d = 0; d < degrees; d++) {
        if (type === T_EQUAL) cents.push(period * (d + 1) / degrees);
        else { cents.push(dv.getUint16(off, true) / unit); off += 2; }
      }
      out.push({ name, degrees, type, period, cents });
    }
    return out;
  }

  const api = { parseScl, classify, buildLibrary, decodeLibrary, shortName, SclError, T_TABLE16, T_EQUAL, T_TABLE4 };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.SclCodec = api;
})(typeof window !== 'undefined' ? window : globalThis);

/* A smoke run of site/src/app.js on a tiny fake DOM: every tab, a scale change, notes, MIDI, the reset.
 * It catches exceptions and broken wiring, not layout. node tests/ui_smoke.js */
'use strict';
const fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..');
const created = [], byId = new Map();
let errors = 0;
class El {
  constructor(tag) { this.tag = tag; this.children = []; this._text = ''; this.attrs = {}; this.dataset = {}; this.style = {};
    this.listeners = {}; this.value = ''; this.options = []; created.push(this);
    const self = this;
    this.classList = { set: new Set(), add(c) { this.set.add(c); }, remove(c) { this.set.delete(c); },
      toggle(c, on) { if (on === undefined ? !this.set.has(c) : on) this.set.add(c); else this.set.delete(c); }, contains(c) { return this.set.has(c); } };
    Object.defineProperty(this, 'className', { get() { return [...self.classList.set].join(' '); }, set(v) { self.classList.set = new Set(String(v).split(/\s+/).filter(Boolean)); } });
  }
  set textContent(v) { this._text = String(v); this.children = []; }
  get textContent() { return this._text; }
  set innerHTML(v) { this.children = []; }
  append(...n) { this.children.push(...n); }
  add(o) { this.options.push(o); this.children.push(o); }
  get length() { return this.options.length; }
  set length(n) { this.options.length = n; }
  setAttribute(k, v) { this.attrs[k] = String(v); }
  getAttribute(k) { return this.attrs[k]; }
  toggleAttribute(k, on) { if (on) this.attrs[k] = ''; else delete this.attrs[k]; }
  addEventListener(t, f) { (this.listeners[t] ||= []).push(f); }
  fire(t, ev = {}) { for (const f of this.listeners[t] || []) f(Object.assign({ preventDefault() {}, target: this, buttons: 1, pointerId: 1 }, ev)); }
  querySelector() { return new El('span'); }
  querySelectorAll() { return []; }
  closest() { return null; }
  releasePointerCapture() {}
  setPointerCapture() {}
  getBoundingClientRect() { return { left: 0, top: 0, width: 400, height: 400 }; }
  set id(v) { this._id = v; byId.set(v, this); }
  get id() { return this._id; }
}
const ids = [...fs.readFileSync(path.join(root, 'site/src/page.html'), 'utf8').matchAll(/id="([^"]+)"/g)].map(m => m[1]);
ids.forEach(i => { const e = new El('div'); e.id = i; });
byId.get('ref').value = '261.63'; byId.get('midiMap').value = 'white'; byId.get('q').value = ''; byId.get('degFilter').value = '';
globalThis.window = globalThis;
globalThis.document = { getElementById: i => byId.get(i) || null, createElement: t => new El(t), createElementNS: (_, t) => new El(t), querySelectorAll: () => [] };
globalThis.Option = function (text, value) { const o = new El('option'); o.text = text; o.value = value; return o; };
const store = new Map();
globalThis.localStorage = { getItem: k => store.get(k) ?? null, setItem: (k, v) => store.set(k, v) };
const posted = [];
let sproc = null;
globalThis.AudioContext = class { constructor() { this.state = 'running'; this.sampleRate = 44100; this.destination = {}; this.audioWorklet = { addModule: async () => {} }; }
  resume() { return Promise.resolve(); } createScriptProcessor() { sproc = { connect() {}, onaudioprocess: null }; return sproc; } };
/* WORKLET=1: a worklet that runs the real processor and answers, as in Chrome; otherwise it never answers */
let wproc = null;
globalThis.AudioWorkletNode = class {
  constructor(_c, _n, opts) {
    const self = this;
    this.port = { postMessage: m => { posted.push(m); if (wproc) wproc.msg(m); }, onmessage: null };
    if (process.env.WORKLET) {
      let Cls = null;
      const base = class { constructor() { this.port = { postMessage: m => setTimeout(() => self.port.onmessage && self.port.onmessage({ data: m })) }; } };
      new Function('AudioWorkletProcessor', 'registerProcessor', window.FM1_WORKLET)(base, (n, c) => { Cls = c; });
      wproc = new Cls(opts);
    }
  }
  connect() {}
};
globalThis.URL.createObjectURL = () => 'blob:x';
const onWin = {};
globalThis.addEventListener = (t, f) => { (onWin[t] ||= []).push(f); };
globalThis.indexedDB = { open() { const r = {}; setTimeout(() => r.onerror && r.onerror()); return r; } };
const realSetInterval = setInterval; globalThis.setInterval = () => 0;
process.on('unhandledRejection', e => { errors++; console.log('REJECTION', e); });

window.FM1_PARAMS = JSON.parse(fs.readFileSync(path.join(root, 'site/params.json'), 'utf8'));
window.FM1_DRUMS = JSON.parse(fs.readFileSync(path.join(root, 'site/drums.json'), 'utf8'));
window.FM1_FX = JSON.parse(fs.readFileSync(path.join(root, 'site/fx.json'), 'utf8'));
window.FM1_SCALES = JSON.parse(fs.readFileSync(path.join(root, 'site/scales.json'), 'utf8'));
window.FM1_WASM_B64 = fs.readFileSync(path.join(root, 'site/engine.wasm')).toString('base64');
window.FM1_WORKLET = fs.readFileSync(path.join(root, 'site/src/worklet.js'), 'utf8');
require(path.join(root, 'tools/scl-codec.js')); window.SclCodec = globalThis.SclCodec || require(path.join(root, 'tools/scl-codec.js'));
const check = (name, f) => { try { f(); } catch (e) { errors++; console.log('FAIL', name, e.stack.split('\n').slice(0, 3).join(' | ')); } };

check('load', () => eval(fs.readFileSync(path.join(root, 'site/src/app.js'), 'utf8')));
(async () => {
  await new Promise(r => setTimeout(r, 50));                       /* the mirror engine and the stored files */
  const tabs = () => byId.get('tabs').children;
  check('tabs', () => { for (let i = 0; i < tabs().length; i++) tabs()[i].fire('click'); });
  check('every control', () => {
    const now = created.slice();                                    /* a firing re-draws and adds elements: not those */
    for (const e of now) if (e.tag === 'input' && e.type === 'range') { e.value = '16383'; e.fire('input'); e.fire('dblclick'); }
    for (const e of now) if (e.tag === 'input' && (e.type === 'checkbox' || e.type === 'number')) { e.checked = true; e.value = '3'; e.fire('change'); }
    for (const e of now) if (e.tag === 'select' && e.options.length) { e.value = String(e.options.length - 1); e.fire('change'); }
  });
  await new Promise(r => setTimeout(r, 0));
  check('scales', () => { const items = byId.get('list').children; items.slice(0, 13).forEach(li => li.children[0].fire('click')); });
  check('window', () => ['winDown', 'winUp', 'perUp', 'perDown'].forEach(i => byId.get(i).fire('click')));
  check('keys', () => { const keys = byId.get('keys').children; keys.forEach(k => k.fire('pointerdown')); keys.forEach(k => k.fire('pointerup')); });
  await new Promise(r => setTimeout(r, 10));
  check('wheel sectors', () => {
    const w = byId.get('wheel'), before = posted.length;
    w.fire('pointerdown', { clientX: 200, clientY: 60, pointerId: 7 });       /* top: degree 0 */
    w.fire('pointermove', { clientX: 340, clientY: 200, pointerId: 7 });      /* right: a quarter round */
    w.fire('pointerup', { clientX: 340, clientY: 200, pointerId: 7 });
    w.fire('pointerdown', { clientX: 200, clientY: 200, pointerId: 8 });      /* the centre: nothing */
    w.fire('pointerup', { clientX: 200, clientY: 200, pointerId: 8 });
    globalThis.__wheelPosted = posted.slice(before);
  });
  check('computer keys', () => { onWin.keydown.forEach(f => f({ code: 'KeyZ', target: new El('div') })); onWin.keyup.forEach(f => f({ code: 'KeyZ' })); });
  check('search', () => { byId.get('q').value = 'edo'; byId.get('q').fire('input'); byId.get('degFilter').value = '22'; byId.get('degFilter').fire('change'); });
  {
    const wp = (globalThis.__wheelPosted || []).filter(m => m.t === 'on' || m.t === 'off');
    console.log('wheel:', JSON.stringify(wp));
  }
  check('sequencer', () => {
    const kids = id => byId.get(id).children.slice();
    kids('patterns')[2].fire('click');
    for (const t of kids('tracks')) { t.children[0].fire('click'); t.children[1].fire('click'); t.children[2].fire('click'); t.children[1].fire('click'); t.children[2].fire('click'); }
    kids('tracks')[0].children[0].fire('click');
    for (const c of kids('steps').slice(0, 6)) { c.fire('click'); }
    kids('steps')[0].fire('click'); kids('steps')[1].fire('dblclick');
    kids('pages')[1].fire('click'); kids('pages')[0].fire('click');
    for (const b of created.slice()) if (b.tag === 'button' && b._text && /Copy|Paste|Shift|Reverse|Double|Euclidean|Random melody|New lock|New row/.test(b._text)) b.fire('click');
    byId.get('stepRecBtn').fire('click');
    onWin.keydown.forEach(f => f({ code: 'KeyC', target: new El('div') })); onWin.keyup.forEach(f => f({ code: 'KeyC' }));
    byId.get('stepRecBtn').fire('click'); byId.get('recBtn').fire('click'); byId.get('recBtn').fire('click');
    byId.get('songBtn').fire('click'); byId.get('songBtn').fire('click');
    byId.get('tempo').value = '133'; byId.get('tempo').fire('change');
    byId.get('undoBtn').fire('click');
    byId.get('playBtn').fire('click');
  });
  check('reset', () => byId.get('resetBtn').fire('click'));
  check('tuning tools', () => {
    byId.get('modeUp').fire('click'); byId.get('modeUp').fire('click'); byId.get('modeDown').fire('click');
    byId.get('refDeg').value = '9'; byId.get('ref').value = '440'; byId.get('ref').fire('change');
    byId.get('edoN').value = '13'; byId.get('edoP').value = '3/1'; byId.get('edoBtn').fire('click');
    byId.get('fineBox').open = true; byId.get('fineBox').fire('toggle');
    byId.get('fineClear').fire('click');
    byId.get('edoP').value = 'nonsense'; byId.get('edoBtn').fire('click');
  });
  const meta = byId.get('sMeta').textContent;
  if (!/13 degrees/.test(meta) || !/1901\.9/.test(meta)) { errors++; console.log('FAIL equal division shown as', meta); }
  check('audio button', () => byId.get('audioBtn').fire('click'));
  await new Promise(r => setTimeout(r, 1700));                     /* the worklet stays silent: the fallback starts */
  if (process.env.WORKLET) {
    const L = new Float32Array(128), R = new Float32Array(128);
    onWin.keydown.forEach(f => f({ code: 'KeyX', target: new El('div') }));
    await new Promise(r => setTimeout(r, 10));
    let peak = 0;
    for (let i = 0; i < 100; i++) { wproc.process([], [[L, R]]); for (const v of L) peak = Math.max(peak, Math.abs(v)); }
    onWin.keyup.forEach(f => f({ code: 'KeyX' }));
    console.log('worklet audio peak:', peak.toFixed(3), '· status:', byId.get('audioState').textContent);
    /* the sequencer in the playing engine: steps 1..6 of track 1, pattern 3, were switched on above; play runs */
    const x = wproc.x;
    if (!x.seq_playing()) x.seq_play();
    const seen = new Set(); let sp = 0;
    for (let i = 0; i < 3000; i++) { wproc.process([], [[L, R]]); seen.add(x.seq_track_pos(0)); for (const v of L) sp = Math.max(sp, Math.abs(v)); }
    console.log('sequencer: pattern', x.seq_pattern_playing() + 1, '· positions seen', [...seen].sort((a, b) => a - b).join(' '), '· peak', sp.toFixed(3));
    if (seen.size < 8 || !(sp > 0.01)) { errors++; console.log('FAIL: the sequencer does not run or does not sound'); }
    if (!(peak > 0.01) || sproc) { errors++; console.log('FAIL: the worklet path did not play, or fell back'); }
  } else if (!sproc || !sproc.onaudioprocess) { errors++; console.log('FAIL: no fallback after a silent worklet'); }
  else {
    const L = new Float32Array(1024), R = new Float32Array(1024);
    onWin.keydown.forEach(f => f({ code: 'KeyX', target: new El('div') }));
    await new Promise(r => setTimeout(r, 10));
    let peak = 0;
    for (let i = 0; i < 20; i++) { sproc.onaudioprocess({ outputBuffer: { getChannelData: c => (c ? R : L) } }); for (const v of L) peak = Math.max(peak, Math.abs(v)); }
    onWin.keyup.forEach(f => f({ code: 'KeyX' }));
    console.log('fallback audio peak:', peak.toFixed(3), '· status:', byId.get('audioState').textContent);
    if (!(peak > 0.01)) { errors++; console.log('FAIL: the fallback is silent'); }
  }
  /* drums and effects: a pad sounds; the effects tab renders and its sliders reach the engine */
  check('drum pads and effects', () => {
    const L = new Float32Array(1024), R = new Float32Array(1024);
    const proc = process.env.WORKLET ? wproc : null;
    const render = () => { if (proc) { const l = new Float32Array(128), r = new Float32Array(128); for (let i = 0; i < 8; i++) { proc.process([], [[l, r]]); L.set(l, i * 128); } } else sproc.onaudioprocess({ outputBuffer: { getChannelData: c => (c ? R : L) } }); };
    for (let i = 0; i < 200; i++) render();                        /* quiet first */
    const pads = byId.get('pads').children.slice();
    if (pads.length !== 8) throw new Error('pads: ' + pads.length);
    const before = posted.length; pads[0].fire('pointerdown', { preventDefault() {} });
    if (process.env.DEBUG) console.log(JSON.stringify(posted.slice(before)), JSON.stringify(posted.filter(m => m.f === 'eng_drum_set' && m.a[0] === 0).slice(-17).map(m => m.a)));
    let pk = 0; for (let i = 0; i < 30; i++) { render(); for (const v of L) pk = Math.max(pk, Math.abs(v)); }
    console.log('kick peak:', pk.toFixed(3));
    if (!(pk > 0.05)) throw new Error('the kick pad is silent');
    const tabs = byId.get('kitTabs').children.slice(); tabs[1].fire('click');
    const size = byId.get('k-rev_size'); if (!size) throw new Error('no reverb controls');
    size.value = '16000'; size.fire('input');
    const sync = byId.get('k-dly_sync'); sync.value = '6'; sync.fire('change');
    byId.get('kitResetBtn').fire('click');
    byId.get('kitTabs').children[0].fire('click');
    byId.get('pads').children[3].fire('pointerdown', { preventDefault() {} });
    const tune = byId.get('k-pitch'); tune.value = '300'; tune.fire('input');
  });
  /* (in the fallback the messages go straight into the engine; the sound above shows they arrived) */
  console.log(errors ? `${errors} problems` : 'ui smoke: ok');
  process.exit(errors ? 1 : 0);
})();

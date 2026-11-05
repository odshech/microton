/* app.js — the page around engine.wasm. Globals from the build: FM1_PARAMS, FM1_WASM_B64, FM1_WORKLET, SclCodec. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const PARAMS = window.FM1_PARAMS, C = window.SclCodec;
  const byKey = Object.fromEntries(PARAMS.map(p => [p.key, p]));

  /* ================================================================== engine connection */
  let ctx = null, node = null, starting = null;
  const pending = [];
  const send = m => { if (node) node.port.postMessage(m); else pending.push(m); };
  function b64bytes(b64) { const s = atob(b64), u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return u; }

  /* Two ways to run the engine. First choice: an AudioWorklet, on the audio thread. If it cannot load
   * (a blocked module, a refused WebAssembly compile, no answer in 1.5 s), the same processor class runs on
   * the page through a ScriptProcessorNode. Every failure is shown on the page, word for word. */
  let rate = 44100, mode = '', stable = false;
  try { stable = localStorage.getItem('fm1-micro-stable') === '1'; } catch (e) { stable = false; }
  function mainThreadNode(bytes) {
    let Cls = null;
    const shimBase = class { constructor() { this.port = { postMessage: m => fromEngine(m) }; } };
    new Function('AudioWorkletProcessor', 'registerProcessor', window.FM1_WORKLET)(shimBase, (n, c) => { Cls = c; });
    const proc = new Cls({ processorOptions: { wasm: bytes } });
    if (!proc.x) throw new Error('WebAssembly');
    /* the page's own thread also draws the page: a long buffer (93 ms, 186 ms stable) rides over the pauses */
    const size = stable ? 8192 : 4096, bl = new Float32Array(128), br = new Float32Array(128), quantum = [[bl, br]];
    const sp = ctx.createScriptProcessor(size, 0, 2);
    sp.onaudioprocess = e => {
      const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
      for (let o = 0; o < L.length; o += 128) { proc.process(null, quantum); L.set(bl, o); R.set(br, o); }
    };
    sp.connect(ctx.destination);
    return { port: { postMessage: m => proc.msg(m) }, keep: sp };
  }
  async function workletNode(bytes) {
    const url = URL.createObjectURL(new Blob([window.FM1_WORKLET], { type: 'application/javascript' }));
    await ctx.audioWorklet.addModule(url);
    const n = new AudioWorkletNode(ctx, 'fm1-engine', { numberOfInputs: 0, outputChannelCount: [2], processorOptions: { wasm: bytes } });
    await new Promise((ok, no) => {
      const t = setTimeout(() => no(new Error('no answer from the audio processor')), 1500);
      n.onprocessorerror = () => { clearTimeout(t); no(new Error('the audio processor failed')); };
      n.port.onmessage = e => {
        if (e.data.t === 'ready') { clearTimeout(t); n.port.onmessage = ev => fromEngine(ev.data); ok(); }
        else if (e.data.t === 'fail') { clearTimeout(t); no(new Error(e.data.msg)); }
      };
    });
    n.connect(ctx.destination);
    return n;
  }
  function startAudio() {
    if (node) { if (ctx.state === 'suspended') ctx.resume(); return Promise.resolve(); }
    if (starting) return starting;
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) { audioFail('This browser cannot play audio from a page.'); return Promise.resolve(); }
    try { ctx = new AC({ sampleRate: 44100, latencyHint: stable ? 'playback' : 'interactive' }); } catch (e) { ctx = new AC(); }
    ctx.resume();                                   /* inside the tap itself: Safari needs this */
    rate = ctx.sampleRate;
    starting = (async () => {
      const bytes = b64bytes(window.FM1_WASM_B64);
      let why = '';
      try {
        if (!ctx.audioWorklet) throw new Error('no AudioWorklet in this browser');
        node = await workletNode(bytes); mode = 'AudioWorklet';
      } catch (err) {
        why = err && err.message || String(err);
        try { node = mainThreadNode(bytes); mode = 'fallback mode'; }
        catch (err2) { audioFail(`Audio did not start: ${why}; ${err2 && err2.message || err2}`); node = null; starting = null; return; }
      }
      pending.length = 0;                             /* the full state follows: nothing old to replay */
      vals.forEach((v, pt) => PARAMS.forEach(p => node.port.postMessage({ t: 'p', part: pt, id: p.id, v: v[p.id] })));
      node.port.postMessage({ t: 'call', f: 'eng_set_rate', a: [rate] });
      kitVals.forEach((v, d) => DP.forEach(p => node.port.postMessage({ t: 'call', f: 'eng_drum_set', a: [d, p.id, v[p.id]] })));
      FXP.forEach(p => node.port.postMessage({ t: 'call', f: 'eng_fx_set', a: [p.id, fxVals[p.id]] }));
      if (mirror) node.port.postMessage({ t: 'proj', bytes: new Uint8Array(mirror.memory.buffer, mirror.seq_project_bytes(), mirror.seq_project_size()).slice() });
      if (lib) node.port.postMessage({ t: 'lib', bytes: lib.bytes });
      applyTuning();
      if (ctx.state === 'suspended') { try { await ctx.resume(); } catch (e) { /* stays suspended */ } }
      if (why) $('audioState').title = why;
      starting = null;
      showAudio();
    })();
    return starting;
  }
  /* "stable": a longer buffer, asked of the system; it restarts the audio */
  $('stableBox').checked = stable;
  $('stableBox').addEventListener('change', async () => {
    stable = $('stableBox').checked;
    try { localStorage.setItem('fm1-micro-stable', stable ? '1' : '0'); } catch (e) { /* storage off */ }
    if (!ctx) return;
    releaseAll();
    const old = ctx; node = null; ctx = null;
    try { await old.close(); } catch (e) { /* already closed */ }
    startAudio().then(showAudio);
  });
  function audioFail(msg) { $('audioLed').className = 'led warn'; $('audioState').textContent = msg; }
  function showAudio() {
    if (!ctx || !node) return;
    const ok = ctx.state === 'running';
    $('audioLed').className = 'led ' + (ok ? 'on' : 'warn');
    const lat = ctx.baseLatency || ctx.outputLatency ? ` · latency ${Math.round(((ctx.baseLatency || 0) + (ctx.outputLatency || 0)) * 1000)} ms` : '';
    $('audioState').textContent = ok
      ? `Audio on · ${mode} · ${rate / 1000} kHz` + (rate !== 44100 ? ' · pitch corrected' : '') + lat
      : 'Audio paused. Press "Start audio".';
    $('audioBtn').textContent = ok ? 'Silence all' : 'Start audio';
  }
  setInterval(showAudio, 1000);
  function fromEngine(m) {
    if (m.t === 'v' || m.t === 'midi' || m.t === 'scalereq') fromSeq(m);
    if (m.t === 'v') { $('voiceState').textContent = `${m.n}/8`; $('voiceLed').className = 'led' + (m.n ? ' on' : ''); }
    else if (m.t === 'scale' && !m.ok) setStatus('The scale did not load into the engine.', true);
    else if (m.t === 'err') setStatus('The library is bigger than the engine\'s space.', true);
  }
  $('audioBtn').addEventListener('click', () => {
    if (node && ctx.state === 'running') { releaseAll(); return; }
    startAudio().then(showAudio);
  });

  /* ================================================================== parameters: labels, units, groups */
  const T = r => { const s = 0.001 * Math.pow(20000, r / 16383); return s < 1 ? `${Math.round(s * 1000)} ms` : `${s.toFixed(2)} s`; };
  const PCT = r => `${Math.round(r / 163.83)}%`;
  const BIP = r => { const v = Math.round((r - 8192) / 81.92); return `${v > 0 ? '+' : ''}${v}%`; };
  const HZ = f => f >= 1000 ? `${(f / 1000).toFixed(2)} kHz` : `${f.toFixed(f < 100 ? 1 : 0)} Hz`;
  const F0 = 440 * Math.pow(2, -69 / 12);
  const WAVES = ['sine', 'triangle', 'saw', 'pulse'];
  const WAVE = r => { const p = r * 3 / 16383, s = Math.min(2, Math.floor(p)), f = p - s;
    if (f < 0.02) return WAVES[s]; if (f > 0.98) return WAVES[s + 1]; return `${WAVES[s]}→${WAVES[s + 1]} ${Math.round(f * 100)}%`; };
  const DEG = r => { const v = r - 24; return `${v > 0 ? '+' : ''}${v}`; };
  const OCT = n => r => { const v = (r - 8192) / 8192 * n; return `${v > 0 ? '+' : ''}${v.toFixed(2)} oct`; };
  const LFOHZ = r => HZ(0.02 * Math.pow(2000, r / 16383));
  const OFF_OR = f => r => r === 0 ? 'off' : f(r);
  const SRC = ['none', 'LFO 1', 'LFO 2', 'envelope 3', 'filter envelope', 'velocity', 'pitch', 'mod wheel', 'aftertouch', 'random per note', 'LFO 1 × wheel'];
  const DST = ['none', 'pitch', 'osc 2 pitch', 'pulse width 1', 'pulse width 2', 'wave 1', 'wave 2', 'cutoff', 'resonance', 'level', 'pan', 'LFO 1 rate', 'LFO 2 rate', 'noise', 'osc 2 level', 'drive'];
  const SHAPES = ['triangle', 'sine', 'saw', 'square', 'sample & hold'];

  const L = {};   /* key → [label, format or options] */
  for (const o of [1, 2]) {
    L[`o${o}_wave`] = ['Wave', WAVE];
    L[`o${o}_pw`] = ['Pulse width', r => `${Math.round(2 + 96 * r / 16383)}%`];
    L[`o${o}_level`] = ['Level', PCT];
    L[`o${o}_deg`] = ['Degrees offset', DEG];
  }
  Object.assign(L, {
    o2_cents: ['Fine tune', r => { const v = (r - 8192) * 100 / 8192; return `${v > 0 ? '+' : ''}${v.toFixed(1)} ¢`; }],
    o2_sync: ['Hard sync', ['off', 'on']],
    sub_level: ['Sub oscillator', PCT], noise_level: ['Noise', PCT], ring_level: ['Ring mod', PCT],
    drive: ['Drive', r => `×${(1 + 7 * r / 16383).toFixed(2)}`],
    flt_mode: ['Type', ['low-pass 24', 'low-pass 12', 'band-pass 12', 'high-pass 12']],
    cutoff: ['Cutoff', r => HZ(F0 * Math.pow(2, (r * 13700 / 16383) / 1200))],
    reso: ['Resonance', PCT], flt_keytrk: ['Key tracking', PCT],
    flt_env: ['Envelope amount', OCT(8)], flt_vel: ['Velocity', OCT(4)],
    hpf: ['High-pass', OFF_OR(r => HZ(F0 * Math.pow(2, (1548 + r * 7976 / 16383) / 1200)))],
    voice_mode: ['Mode', ['poly, 8 voices', 'mono', 'unison']],
    uni_voices: ['Unison voices', ['2', '3', '4']],
    uni_detune: ['Unison detune', r => `${(r * 50 / 16383).toFixed(1)} ¢`], uni_spread: ['Stereo spread', PCT],
    glide: ['Glide', OFF_OR(T)], bend_range: ['Bend range', ['1 degree', '2 degrees', '3 degrees', '4 degrees']],
    vel_amp: ['Velocity to level', PCT],
    drift: ['Pitch drift', r => `${(r * 10 / 16383).toFixed(1)} ¢`], variance: ['Voice variance', PCT],
    out_sat: ['Output saturation', PCT], volume: ['Volume', PCT],
    rev_send: ['Reverb send', PCT], dly_send: ['Delay send', PCT],
    pan: ['Pan', r => r === 8192 ? 'centre' : r < 8192 ? `L ${Math.round((8192 - r) / 81.92)}` : `R ${Math.round((r - 8192) / 81.92)}`],
  });
  for (const e of [1, 2, 3]) {
    L[`e${e}_a`] = ['Attack', T]; L[`e${e}_d`] = ['Decay', T]; L[`e${e}_s`] = ['Sustain', PCT];
    L[`e${e}_r`] = ['Release', T]; L[`e${e}_curve`] = ['Curve', r => r === 0 ? 'exponential' : PCT(r)];
  }
  for (const l of [1, 2]) {
    L[`l${l}_shape`] = ['Shape', SHAPES]; L[`l${l}_rate`] = ['Rate', LFOHZ];
    L[`l${l}_delay`] = ['Delay', OFF_OR(T)]; L[`l${l}_global`] = ['Runs', ['per voice', 'shared']];
  }

  const TABS = [
    ['Oscillators', [['Oscillator 1', ['o1_wave', 'o1_pw', 'o1_level', 'o1_deg']],
                ['Oscillator 2', ['o2_wave', 'o2_pw', 'o2_level', 'o2_deg', 'o2_cents', 'o2_sync']],
                ['More sources', ['sub_level', 'noise_level', 'ring_level', 'drive']]]],
    ['Filter', [['Filter', ['flt_mode', 'cutoff', 'reso', 'flt_keytrk', 'flt_env', 'flt_vel', 'hpf']]]],
    ['Envelopes', [['Envelope 1: level', ['e1_a', 'e1_d', 'e1_s', 'e1_r', 'e1_curve']],
                ['Envelope 2: filter', ['e2_a', 'e2_d', 'e2_s', 'e2_r', 'e2_curve']],
                ['Envelope 3: free', ['e3_a', 'e3_d', 'e3_s', 'e3_r', 'e3_curve']]]],
    ['LFOs', [['LFO 1', ['l1_shape', 'l1_rate', 'l1_delay', 'l1_global']],
                       ['LFO 2', ['l2_shape', 'l2_rate', 'l2_delay', 'l2_global']]]],
    ['Modulation', 'mod'],
    ['Voice', [['Voice', ['voice_mode', 'uni_voices', 'uni_detune', 'uni_spread', 'glide', 'bend_range', 'vel_amp']],
                   ['Analog character', ['drift', 'variance', 'out_sat']], ['Output', ['volume', 'pan', 'rev_send', 'dly_send']]]],
  ];

  /* ================================================================== parameter values: saved per browser */
  /* three sounds: one per melodic track (ENG_PARTS). 'values' is the one being edited */
  const vals = [0, 1, 2].map(() => PARAMS.map(p => p.def));
  let part = 0, values = vals[0];
  const STORE = 'fm1-micro-v1';
  function save() { try { localStorage.setItem(STORE, JSON.stringify({ parts: vals.map(v => Object.fromEntries(PARAMS.map(p => [p.key, v[p.id]]))), ref: $('ref').value, tun,
    kit3: kitVals.map(v => Object.fromEntries(DP.map(p => [p.key, v[p.id]]))),   /* kit3: the fmdrum port's units */ fx: Object.fromEntries(FXP.map(p => [p.key, fxVals[p.id]])) })); } catch (e) { /* storage off */ } }
  let saved = null;
  try { saved = JSON.parse(localStorage.getItem(STORE) || 'null'); } catch (e) { saved = null; }
  const restoreInto = (v, o) => { for (const k in o) if (byKey[k]) v[byKey[k].id] = Math.max(0, Math.min(byKey[k].max, o[k] | 0)); };
  if (saved && saved.parts) saved.parts.forEach((o, i) => i < 3 && restoreInto(vals[i], o));
  else if (saved && saved.v) restoreInto(vals[0], saved.v);
  if (saved && saved.ref) $('ref').value = saved.ref;
  /* the drum kit and the effects: the same store */
  const DP = window.FM1_DRUMS.params, KIT = window.FM1_DRUMS.kit, FXP = window.FM1_FX;
  const dpKey = Object.fromEntries(DP.map(p => [p.key, p])), fxKey = Object.fromEntries(FXP.map(p => [p.key, p]));
  const kitVals = KIT.map(r => r.slice()), fxVals = FXP.map(p => p.def);
  if (saved && saved.kit3) saved.kit3.forEach((o, d) => { if (d < 8) for (const k in o) if (dpKey[k]) kitVals[d][dpKey[k].id] = Math.max(0, Math.min(dpKey[k].max, o[k] | 0)); });
  if (saved && saved.fx) for (const k in saved.fx) if (fxKey[k]) fxVals[fxKey[k].id] = Math.max(0, Math.min(fxKey[k].max, saved.fx[k] | 0));

  const inputs = new Map();  /* id → { el, val, row } */
  function setParam(id, v, fromUi) {
    const p = PARAMS[id]; v = Math.max(0, Math.min(p.max, Math.round(v)));
    values[id] = v; send({ t: 'p', part, id, v });
    recordLock(id, v);
    const ui = inputs.get(id);
    if (ui) { if (!fromUi) ui.el.value = v; showVal(id); }
    if (p.key.endsWith('_dst')) showVal(id + 1);
    clearTimeout(setParam.t); setParam.t = setTimeout(save, 300);
  }
  function showVal(id) {
    const ui = inputs.get(id); if (!ui) return;
    const p = PARAMS[id], v = values[id];
    if (ui.val) {
      if (/^m\d_amt$/.test(p.key)) {
        const a = (v - 8192) / 8192, eff = a * Math.abs(a), dst = values[id - 1];
        ui.val.textContent = dst === 1 || dst === 2 ? `${eff >= 0 ? '+' : ''}${Math.round(eff * 1200)} ¢` : `${eff >= 0 ? '+' : ''}${(eff * 100).toFixed(1)}%`;
      } else ui.val.textContent = L[p.key][1](v);
    }
    if (ui.row) ui.row.classList.toggle('changed', v !== p.def);
  }
  function control(key) {
    const p = byKey[key], [label, fmt] = L[key];
    const row = document.createElement('div'); row.className = 'ctl';
    const lab = document.createElement('label'); lab.textContent = label; lab.htmlFor = 'p-' + key;
    let el, val = null;
    if (Array.isArray(fmt)) {
      el = document.createElement('select');
      fmt.forEach((name, i) => el.add(new Option(name, String(i))));
      el.addEventListener('change', () => setParam(p.id, +el.value, true));
      row.append(lab, el, document.createElement('span'));
    } else {
      el = document.createElement('input'); el.type = 'range'; el.min = 0; el.max = p.max; el.step = 1;
      el.addEventListener('input', () => setParam(p.id, +el.value, true));
      el.addEventListener('dblclick', () => setParam(p.id, p.def));
      val = document.createElement('span'); val.className = 'val';
      row.append(lab, el, val);
    }
    el.id = 'p-' + key; el.value = values[p.id];
    inputs.set(p.id, { el, val, row }); showVal(p.id);
    return row;
  }
  function modTab() {
    const box = document.createElement('div'); box.className = 'group';
    const hint = document.createElement('p'); hint.className = 'note';
    hint.textContent = 'Eight routings: source, destination and amount. The amount is finer near zero, for gentle vibrato.';
    box.append(hint);
    for (let k = 1; k <= 8; k++) {
      const src = byKey[`m${k}_src`], dst = byKey[`m${k}_dst`], amt = byKey[`m${k}_amt`];
      const row = document.createElement('div'); row.className = 'modrow';
      const n = document.createElement('span'); n.className = 'n'; n.textContent = k;
      const mk = (p, opts) => { const s = document.createElement('select'); s.id = 'p-' + p.key; opts.forEach((o, i) => s.add(new Option(o, String(i)))); s.value = values[p.id];
        s.setAttribute('aria-label', `Routing ${k}: ${p === src ? 'source' : 'destination'}`); s.addEventListener('change', () => setParam(p.id, +s.value, true)); inputs.set(p.id, { el: s, val: null, row: null }); return s; };
      row.append(n, mk(src, SRC), mk(dst, DST));
      const ar = document.createElement('div'); ar.className = 'modamt';
      const r = document.createElement('input'); r.type = 'range'; r.min = 0; r.max = 16383; r.value = values[amt.id]; r.id = 'p-' + amt.key;
      r.setAttribute('aria-label', `Routing ${k}: amount`);
      r.addEventListener('input', () => setParam(amt.id, +r.value, true)); r.addEventListener('dblclick', () => setParam(amt.id, 8192));
      const v = document.createElement('span'); v.className = 'val';
      ar.append(r, v); inputs.set(amt.id, { el: r, val: v, row: null });
      box.append(row, ar); showVal(amt.id);
    }
    return box;
  }
  let tab = 0;
  function renderTabs() {
    const bar = $('tabs'); bar.textContent = '';
    TABS.forEach(([name], i) => {
      const b = document.createElement('button'); b.type = 'button'; b.setAttribute('role', 'tab'); b.textContent = name;
      b.setAttribute('aria-selected', String(i === tab)); b.addEventListener('click', () => { tab = i; renderTabs(); });
      bar.append(b);
    });
    const body = $('tabBody'); body.textContent = ''; inputs.clear();
    const def = TABS[tab][1];
    if (def === 'mod') { body.append(modTab()); return; }
    def.forEach(([title, keys]) => {
      const g = document.createElement('div'); g.className = 'group';
      const h = document.createElement('h3'); h.textContent = title; g.append(h);
      keys.forEach(k => g.append(control(k)));
      body.append(g);
    });
  }
  $('resetBtn').addEventListener('click', () => { PARAMS.forEach(p => setParam(p.id, p.def)); renderTabs(); });
  /* which sound the editor shows; the keys play the sound of the selected melodic track */
  function setPart(pt) { part = pt; values = vals[pt]; document.querySelectorAll('#partTabs button').forEach((b, i) => b.setAttribute('aria-pressed', String(i === pt))); renderTabs(); }
  document.querySelectorAll('#partTabs button').forEach((b, i) => b.addEventListener('click', () => setPart(i)));
  const playPart = () => part;

  /* ================================================================== drums and effects */
  const DRUM_NAMES = ['Kick', 'Snare', 'Closed hat', 'Open hat', 'Clap', 'Low tom', 'High tom', 'Rim'];
  const HZC = c => HZ(F0 * Math.pow(2, c / 1200));
  const PANF = r => r === 8192 ? 'centre' : r < 8192 ? `L ${Math.round((8192 - r) / 81.92)}` : `R ${Math.round((r - 8192) / 81.92)}`;
  const DL = {
    model: ['Model', ['kick', 'snare', 'hat', 'metal', 'perc']],
    pitch: ['Pitch', r => `${Math.max(10, r)}% · ${HZ(0.48 * Math.max(10, r))}`],
    sweep: ['Sweep', r => r ? `+${r * 4} Hz` : 'off'],
    decay: ['Decay', r => r <= 100 ? String(r) : `${r} (long)`],
    op1_index: ['Op 1 index', r => `${(r * 0.225).toFixed(1)} rad`], op2_index: ['Op 2 index', r => `${(r * 0.1).toFixed(1)} rad`],
    op1_ratio: ['Op 1 ratio', r => (r / 10).toFixed(1)], op2_ratio: ['Op 2 ratio', r => (r / 10).toFixed(1)],
    feedback: ['Op 2 feedback', r => `${r}%`], noise: ['Noise', r => `${r}%`],
    drive: ['Drive', r => `×${(1 + r / 20).toFixed(2)}`], level: ['Level', r => `${r}%`], pan: ['Pan', PANF],
    rev: ['Reverb send', PCT], dly: ['Delay send', PCT], choke: ['Choke group', ['none', 'group 1', 'group 2']],
    clap: ['Clap bursts', ['none', '1', '2', '3']],
  };
  const FL = {
    rev_size: ['Size (decay)', r => (0.2 + 0.78 * r / 16383).toFixed(2)], rev_pre: ['Pre-delay', r => `${Math.round(r * 100 / 16383)} ms`],
    rev_damp: ['Damping', PCT], rev_bw: ['Brightness', PCT], rev_mod: ['Movement', PCT], rev_level: ['Level', PCT],
    dly_time: ['Free time', r => { const ms = 10 * Math.pow(120, r / 16383); return ms < 1000 ? `${Math.round(ms)} ms` : `${(ms / 1000).toFixed(2)} s`; }],
    dly_sync: ['Tempo sync', ['free', '1/32', '1/16 triplet', '1/16', '1/8 triplet', '1/16 dotted', '1/8', '1/4 triplet', '1/8 dotted', '1/4', '1/4 dotted', '1/2']],
    dly_fb: ['Feedback', r => `${Math.round(r * 110 / 16383)}%`], dly_tone: ['Tone', r => HZC(3000 + r * 10000 / 16383)],
    dly_lowcut: ['Low cut', r => HZC(2400 + r * 6000 / 16383)], dly_ping: ['Ping-pong', PCT], dly_wow: ['Tape wow', PCT],
    dly_drive: ['Saturation', r => `×${(1 + 3 * r / 16383).toFixed(2)}`], dly_level: ['Level', PCT], dly_to_rev: ['Into the reverb', PCT],
  };
  const DRUM_GROUPS = [['Voice', ['model', 'pitch', 'sweep', 'decay', 'noise']],
    ['FM', ['op1_index', 'op1_ratio', 'op2_index', 'op2_ratio', 'feedback']],
    ['Output', ['drive', 'level', 'pan', 'rev', 'dly', 'choke', 'clap']]];
  const FX_GROUPS = [['Reverb', ['rev_size', 'rev_pre', 'rev_damp', 'rev_bw', 'rev_mod', 'rev_level']],
    ['Delay', ['dly_sync', 'dly_time', 'dly_fb', 'dly_tone', 'dly_lowcut', 'dly_ping', 'dly_wow', 'dly_drive', 'dly_level', 'dly_to_rev']]];
  let kitTab = 0, drumSel = 0;
  const kitInputs = new Map();
  function setDrum(d, id, v) {
    const p = DP[id]; v = Math.max(0, Math.min(p.max, Math.round(v))); kitVals[d][id] = v;
    send({ t: 'call', f: 'eng_drum_set', a: [d, id, v] });
    const ui = kitInputs.get('d' + id); if (ui && d === drumSel) { ui.el.value = v; ui.show(); }
    clearTimeout(setParam.t); setParam.t = setTimeout(save, 300);
  }
  function setFx(id, v) {
    const p = FXP[id]; v = Math.max(0, Math.min(p.max, Math.round(v))); fxVals[id] = v;
    send({ t: 'call', f: 'eng_fx_set', a: [id, v] });
    const ui = kitInputs.get('f' + id); if (ui) { ui.el.value = v; ui.show(); }
    clearTimeout(setParam.t); setParam.t = setTimeout(save, 300);
  }
  function kitControl(key, isFx) {
    const p = isFx ? fxKey[key] : dpKey[key], [label, fmt] = (isFx ? FL : DL)[key];
    const get = () => isFx ? fxVals[p.id] : kitVals[drumSel][p.id];
    const def = () => isFx ? p.def : KIT[drumSel][p.id];
    const set = v => isFx ? setFx(p.id, v) : setDrum(drumSel, p.id, v);
    const row = document.createElement('div'); row.className = 'ctl';
    const lab = document.createElement('label'); lab.textContent = label; lab.htmlFor = 'k-' + key;
    let el, val = null;
    if (Array.isArray(fmt)) {
      el = document.createElement('select'); fmt.forEach((n, i) => el.add(new Option(n, String(i))));
      el.addEventListener('change', () => set(+el.value)); row.append(lab, el, document.createElement('span'));
    } else {
      el = document.createElement('input'); el.type = 'range'; el.min = 0; el.max = p.max; el.step = 1;
      el.addEventListener('input', () => set(+el.value)); el.addEventListener('dblclick', () => set(def()));
      val = document.createElement('span'); val.className = 'val'; row.append(lab, el, val);
    }
    el.id = 'k-' + key; el.value = get();
    const show = () => { if (val) val.textContent = fmt(get()); row.classList.toggle('changed', get() !== def()); };
    kitInputs.set((isFx ? 'f' : 'd') + p.id, { el, show }); show();
    return row;
  }
  function hitDrum(d, vel = 110) {
    startAudio();
    send({ t: 'call', f: 'eng_drum_hit', a: [d, vel] });
    const b = $('pads').children[d]; if (b) { b.classList.add('hit'); setTimeout(() => b.classList.remove('hit'), 90); }
  }
  function renderKit() {
    const bar = $('kitTabs'); bar.textContent = '';
    ['Drums', 'Reverb and delay'].forEach((name, i) => {
      const b = document.createElement('button'); b.type = 'button'; b.setAttribute('role', 'tab'); b.textContent = name;
      b.setAttribute('aria-selected', String(i === kitTab)); b.addEventListener('click', () => { kitTab = i; renderKit(); });
      bar.append(b);
    });
    const pads = $('pads'); pads.textContent = ''; pads.hidden = kitTab !== 0;
    DRUM_NAMES.forEach((n, d) => {
      const b = document.createElement('button'); b.type = 'button'; b.textContent = n; b.setAttribute('aria-pressed', String(d === drumSel));
      b.addEventListener('pointerdown', e => { e.preventDefault(); hitDrum(d); if (d !== drumSel) { drumSel = d; renderKit(); } });
      b.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); hitDrum(d); drumSel = d; renderKit(); } });
      pads.append(b);
    });
    const body = $('kitBody'); body.textContent = ''; kitInputs.clear();
    (kitTab === 0 ? DRUM_GROUPS : FX_GROUPS).forEach(([title, keys]) => {
      const g = document.createElement('div'); g.className = 'group';
      const h = document.createElement('h3'); h.textContent = kitTab === 0 && title === 'Voice' ? `${DRUM_NAMES[drumSel]}: ${title}` : title; g.append(h);
      keys.forEach(k => g.append(kitControl(k, kitTab === 1)));
      body.append(g);
    });
  }
  $('kitResetBtn').addEventListener('click', () => {
    if (kitTab === 0) DP.forEach(p => setDrum(drumSel, p.id, KIT[drumSel][p.id])); else FXP.forEach(p => setFx(p.id, p.def));
    renderKit();
  });

  /* ================================================================== scales */
  const edo = (n, period = 1200) => Array.from({ length: n }, (_, k) => (period * (k + 1) / n).toFixed(5)).join('\n');
  /* the built-in scales, the same list as on the FM-1 (tools/world_scales.js): always in the list, also beside
   * the files you load */
  const EXAMPLES = window.FM1_SCALES.slice();
  const BUILTIN_NAMES = new Set(EXAMPLES.map(f => f.name));

  /* The tuning maths lives in C (engine/tuning.c), the same as on the FM-1. The page runs its own copy of the
   * engine, "mirror", to read the played table; every change goes to both, so the two always agree. */
  let files = EXAMPLES.slice(), isExample = true, lib = null, current = null, mirror = null;
  const tun = { base: { lib: '22-edo.scl' }, mode: 0, fine: {}, refDeg: 0 };
  if (saved && saved.tun) Object.assign(tun, saved.tun);
  let cents = [], winStart = 0;
  const nDeg = () => (mirror ? mirror.tun_degrees() : 12);

  /* the stored library: IndexedDB, so the files survive a reload (optional: the page works without it) */
  const idb = {
    db: null,
    open() { return this.db || (this.db = new Promise((ok, no) => { const r = indexedDB.open('fm1-micro', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('kv'); r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error); })); },
    async get(k) { const db = await this.open(); return new Promise((ok, no) => { const q = db.transaction('kv').objectStore('kv').get(k); q.onsuccess = () => ok(q.result); q.onerror = () => no(q.error); }); },
    async set(k, v) { const db = await this.open(); return new Promise((ok, no) => { const t = db.transaction('kv', 'readwrite'); t.objectStore('kv').put(v, k); t.oncomplete = ok; t.onerror = () => no(t.error); }); },
  };
  const storeFiles = () => { try { idb.set('files', isExample ? null : files.map(f => ({ name: f.name, text: f.text }))).catch(() => {}); } catch (e) { /* no storage */ } };

  function rebuild() {
    lib = C.buildLibrary(isExample ? files : EXAMPLES.concat(files.filter(f => !BUILTIN_NAMES.has(f.name))));
    if (mirror && lib.bytes.length <= mirror.w_lib_cap()) new Uint8Array(mirror.memory.buffer, mirror.w_lib(), lib.bytes.length).set(lib.bytes);
    send({ t: 'lib', bytes: lib.bytes });
    renderFilters(); renderList();
    if (tun.base.lib) current = lib.scales.find(s => s.file === tun.base.lib || s.aliases.includes(tun.base.lib)) || lib.scales.find(s => s.file === '22-edo.scl') || lib.scales[0];
    if (current && !tun.base.edo) tun.base = { lib: current.file };
    applyTuning();
  }
  /* the whole tuning, again, to both copies of the engine: cheap, and nothing can drift */
  function applyTuning() {
    const idx = current ? lib.scales.indexOf(current) : -1, ref = refChz();
    const steps = [];
    if (tun.base.edo) steps.push(['tun_edo', [tun.base.edo, tun.base.period]]);
    else if (idx >= 0) steps.push(['scale', [idx]]);
    steps.push(['tun_rotate', [tun.mode]]);
    for (const d in tun.fine) steps.push(['tun_fine', [+d, tun.fine[d]]]);
    steps.push(['tun_ref', [tun.refDeg, ref]]);
    for (const [f, a] of steps) {
      if (f === 'scale') { if (mirror) mirror.w_scale(lib.bytes.length, a[0]); send({ t: 'scale', i: a[0] }); }
      else { if (mirror) mirror[f](...a); send({ t: 'call', f, a }); }
    }
    readTable();
    save();
  }
  function readTable() {
    const n = nDeg();
    cents = Array.from({ length: n }, (_, k) => (mirror ? mirror.tun_c16(k) / 16 : 1200 * (k + 1) / 12));
    const name = tun.base.edo ? tun.base.name : current ? current.name : '';
    $('sName').textContent = name;
    $('sMeta').textContent = `${n} degrees · period ${cents[n - 1].toFixed(2)} cents` + (tun.mode ? ` · mode ${tun.mode}` : '');
    $('modeVal').textContent = tun.mode;
    $('refDeg').value = tun.refDeg;
    document.querySelectorAll('#list button').forEach(b => b.setAttribute('aria-current', String(!tun.base.edo && current && b.dataset.file === current.file)));
    renderWheel(); renderKeys(); renderFine();
  }
  function select(s) {
    if (!s) return;
    releaseAll();
    current = s; tun.base = { lib: s.file }; tun.mode = 0; tun.fine = {}; winStart = 0;
    applyTuning();
  }
  function setStatus(msg, err) { const st = $('status'); st.textContent = msg; st.classList.toggle('err', !!err); }

  /* mode, reference, equal divisions, fine offsets */
  const rotate = d => { releaseAll(); tun.mode = mod(tun.mode + d, nDeg()); tun.fine = {}; applyTuning(); };
  $('modeDown').addEventListener('click', () => rotate(-1));
  $('modeUp').addEventListener('click', () => rotate(1));
  /* the engine counts time at 44.1 kHz; on another rate the reference is scaled so the pitch stays right */
  const refChz = () => Math.round(Math.max(1, Math.min(20000, parseFloat($('ref').value) || 261.63)) * 100 * 44100 / rate);
  const setRef = () => { tun.refDeg = Math.max(-255, Math.min(255, parseInt($('refDeg').value, 10) || 0)); applyTuning(); };
  $('ref').addEventListener('change', setRef);
  $('refDeg').addEventListener('change', setRef);
  function parsePeriod(txt) {
    const t = String(txt).trim(), m = /^(\d+(?:\.\d+)?)\s*\/\s*(\d+(?:\.\d+)?)$/.exec(t);
    const c = m ? 1200 * Math.log2(+m[1] / +m[2]) : parseFloat(t);
    return c > 0 && c <= 16383 ? c : null;
  }
  $('edoBtn').addEventListener('click', () => {
    const n = parseInt($('edoN').value, 10), c = parsePeriod($('edoP').value);
    if (!(n >= 1 && n <= 255)) { setStatus('The number of steps must be 1 to 255.', true); return; }
    if (c === null) { setStatus('The period must be a ratio such as 3/1, or cents such as 1200.', true); return; }
    releaseAll();
    tun.base = { edo: n, period: Math.round(c * 16), name: `${n} equal steps to ${$('edoP').value.trim()}` };
    tun.mode = 0; tun.fine = {}; winStart = 0; current = null;
    applyTuning();
    setStatus(`${c.toFixed(2)} cents in ${n} steps of ${(c / n).toFixed(3)} cents.`);
  });
  function renderFine() {
    const body = $('fineBody'); body.textContent = '';
    if (!$('fineBox').open) return;
    const n = nDeg();
    for (let d = 1; d < n; d++) {
      const tr = document.createElement('tr');
      const a = document.createElement('td'); a.textContent = d;
      const b = document.createElement('td'); b.textContent = cents[d - 1].toFixed(2);
      const c = document.createElement('td'), inp = document.createElement('input');
      inp.type = 'number'; inp.min = -50; inp.max = 50; inp.step = 0.1; inp.id = 'fine-' + d;
      inp.value = ((tun.fine[d] || 0) / 16).toFixed(1); inp.setAttribute('aria-label', `Offset of degree ${d}`);
      inp.addEventListener('change', () => {
        const v = Math.round(Math.max(-50, Math.min(50, parseFloat(inp.value) || 0)) * 16);
        if (v) tun.fine[d] = v; else delete tun.fine[d];
        applyTuning();
      });
      if (tun.fine[d]) b.className = 'moved';
      c.append(inp); tr.append(a, b, c); body.append(tr);
    }
  }
  $('fineBox').addEventListener('toggle', renderFine);
  $('fineClear').addEventListener('click', () => { tun.fine = {}; applyTuning(); });

  /* a .kbm sets which degree carries which frequency; its key map is not used (the white keys are the map) */
  function applyKbm(text) {
    const v = text.replace(/\r/g, '').split('\n').filter(l => !l.startsWith('!') && l.trim() !== '').map(l => l.trim().split(/\s+/)[0]);
    const middle = parseInt(v[3], 10), refNote = parseInt(v[4], 10), freq = parseFloat(v[5]);
    if (!(freq > 0) || Number.isNaN(middle) || Number.isNaN(refNote)) throw new Error('The mapping file is not valid.');
    $('ref').value = freq; $('refDeg').value = refNote - middle; setRef();
    return `Degree ${refNote - middle} at ${freq} Hz, from the mapping file.`;
  }

  const latin = new TextDecoder('windows-1252');
  async function readAll(list) {
    const out = [], notes = [];
    for (const f of list) {
      if (/\.zip$/i.test(f.name)) {
        if (!window.JSZip) throw new Error('Opening zip files needs the network. Load the scale files themselves.');
        const zip = await JSZip.loadAsync(f);
        for (const e of Object.values(zip.files)) {
          if (e.dir || !/\.scl$/i.test(e.name) || /(^|\/)__MACOSX\//.test(e.name)) continue;
          const u8 = await e.async('uint8array'); out.push({ name: e.name.split('/').pop(), text: latin.decode(u8), size: u8.length });
        }
      } else if (/\.scl$/i.test(f.name)) { const u8 = new Uint8Array(await f.arrayBuffer()); out.push({ name: f.name, text: latin.decode(u8), size: u8.length }); }
      else if (/\.kbm$/i.test(f.name)) notes.push(applyKbm(latin.decode(new Uint8Array(await f.arrayBuffer()))));
    }
    return { out, notes };
  }
  $('pickFiles').addEventListener('change', async e => {
    try {
      const { out: got, notes } = await readAll([...e.target.files]); e.target.value = '';
      if (!got.length) { setStatus(notes.length ? notes.join(' ') : 'No scale files found.', !notes.length); return; }
      if (isExample) { files = []; isExample = false; }
      files = files.concat(got); rebuild(); storeFiles();
      setStatus(`${lib.scales.length.toLocaleString('en')} scales in the library · ${(lib.bytes.length / 1024).toFixed(1)} KB` + (lib.errors.length ? ` · ${lib.errors.length} files left out` : '') + (notes.length ? ' · ' + notes.join(' ') : ''));
      if (lib.bytes.length > 640 * 1024) setStatus('The library is over 640 KB, more than the FM-1 can hold.', true);
    } catch (err) { setStatus(err.message || 'Reading failed.', true); }
  });
  $('resetLib').addEventListener('click', () => { files = EXAMPLES.slice(); isExample = true; storeFiles(); tun.base = { lib: '22-edo.scl' }; rebuild(); setStatus('Back to the built-in scales only.'); });

  function renderFilters() {
    const sel = $('degFilter'), was = sel.value, counts = new Map();
    lib.scales.forEach(s => counts.set(s.degrees, (counts.get(s.degrees) || 0) + 1));
    sel.length = 1;
    [...counts.keys()].sort((a, b) => a - b).forEach(n => sel.add(new Option(`${n} degrees (${counts.get(n)})`, String(n))));
    sel.value = counts.has(+was) ? was : '';
  }
  function renderList() {
    const q = $('q').value.trim().toLowerCase(), deg = $('degFilter').value, ul = $('list');
    const hits = lib.scales.filter(s => (!deg || s.degrees === +deg) && (!q || s.file.toLowerCase().includes(q) || s.description.toLowerCase().includes(q)));
    ul.textContent = '';
    hits.slice(0, 300).forEach(s => {
      const li = document.createElement('li'), b = document.createElement('button');
      b.type = 'button'; b.dataset.file = s.file; b.setAttribute('aria-current', String(s === current && !tun.base.edo));
      const nm = document.createElement('span'); nm.className = 'nm'; nm.textContent = s.name;
      const meta = document.createElement('span'); meta.className = 'meta'; meta.textContent = `${s.degrees} · ${(s.period / s.unit).toFixed(1)}¢`;
      const d = document.createElement('span'); d.className = 'desc'; d.textContent = s.description || '—';
      b.append(nm, meta, d); b.addEventListener('click', () => select(s)); li.append(b); ul.append(li);
    });
  }
  $('q').addEventListener('input', renderList);
  $('degFilter').addEventListener('change', renderList);

  /* ================================================================== wheel */
  const NS = 'http://www.w3.org/2000/svg', CX = 200, CY = 200, R = 150;
  const el = (tag, a) => { const e = document.createElementNS(NS, tag); for (const k in a) e.setAttribute(k, a[k]); return e; };
  const at = (frac, r) => [CX + r * Math.sin(frac * 2 * Math.PI), CY - r * Math.cos(frac * 2 * Math.PI)];
  const mod = (a, n) => ((a % n) + n) % n;
  /* each degree owns the slice of the circle nearest its spoke (exactly the area a touch plays), shaded red
   * from light at degree 0 to dark at the last degree */
  function sectorPath(a0, a1, r) {
    if (a1 - a0 >= 0.9999) return `M${CX - r} ${CY}a${r} ${r} 0 1 0 ${2 * r} 0a${r} ${r} 0 1 0 ${-2 * r} 0`;
    const [x0, y0] = at(a0, r), [x1, y1] = at(a1, r);
    return `M${CX} ${CY}L${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${a1 - a0 > 0.5 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}Z`;
  }
  function renderWheel() {
    const svg = $('wheel'); svg.textContent = '';
    const n = nDeg(), p = cents[n - 1];
    const fr = d => (mod(d, n) ? cents[mod(d, n) - 1] : 0) / p + Math.floor(d / n);
    const inWin = new Set(); for (let j = 0; j < 16; j++) inWin.add(mod(winStart + j, n));
    for (let d = 0; d < n; d++) {
      const f = fr(d), a0 = (fr(d - 1) + f) / 2, a1 = (f + fr(d + 1)) / 2;
      const light = n > 1 ? 76 - 54 * d / (n - 1) : 50;
      const [x, y] = at(f, R), [tx, ty] = at(f, R + 20);
      const g = el('g', { 'data-deg': d });
      g.append(el('path', { d: sectorPath(a0, a1, R), class: 'sector', fill: `hsl(356 78% ${light}%)` }));
      g.append(el('circle', { cx: x, cy: y, r: n > 60 ? 2.5 : 4, class: 'dot' + (inWin.has(d) ? ' win' : '') }));
      if (n <= 36 || d % 5 === 0) { const t = el('text', { x: tx, y: ty + 4, 'text-anchor': 'middle' }); t.textContent = d; g.append(t); }
      svg.append(g);
    }
    svg.append(el('circle', { cx: CX, cy: CY, r: R, class: 'rim' }));
    svg.append(el('circle', { cx: CX, cy: CY, r: 30, class: 'hub' }));
    const c1 = el('text', { x: CX, y: CY + 2, 'text-anchor': 'middle', class: 'centre', 'font-size': '20' }); c1.textContent = n;
    const c2 = el('text', { x: CX, y: CY + 16, 'text-anchor': 'middle', 'font-size': '9' }); c2.textContent = `${p.toFixed(1)}¢`;
    svg.append(c1, c2);
    refreshLights();
  }

  /* the wheel plays by sector, as in the Android app: a touch anywhere in a degree's slice of the circle plays it,
   * and a finger sliding round the circle moves from degree to degree. Each finger is its own note. */
  const wheelSvg = $('wheel'), wheelTouch = new Map();          /* pointer id → degree */
  function degreeAt(ev) {
    const r = wheelSvg.getBoundingClientRect();
    if (!r.width || !cents.length) return null;
    const x = (ev.clientX - r.left) / r.width * 400 - CX, y = (ev.clientY - r.top) / r.height * 400 - CY;
    const dist = Math.hypot(x, y);
    if (dist < 30 || dist > R + 40) return null;                /* the centre and the corners play nothing */
    let frac = Math.atan2(x, -y) / (2 * Math.PI); if (frac < 0) frac += 1;
    const n = nDeg(), p = cents[n - 1];
    let best = 0, bd = 2;
    for (let d = 0; d < n; d++) {
      let dd = Math.abs((d ? cents[d - 1] : 0) / p - frac); dd = Math.min(dd, 1 - dd);
      if (dd < bd) { bd = dd; best = d; }
    }
    return best;
  }
  const inWindow = d => { const n = nDeg(); for (let j = 0; j < 16; j++) if (mod(winStart + j, n) === d) return winStart + j; return d; };
  wheelSvg.addEventListener('pointerdown', ev => {
    ev.preventDefault();
    const d = degreeAt(ev); if (d === null) return;
    try { wheelSvg.setPointerCapture(ev.pointerId); } catch (e) { /* not capturable: fine */ }
    wheelTouch.set(ev.pointerId, d); press('w' + ev.pointerId, inWindow(d));
  });
  wheelSvg.addEventListener('pointermove', ev => {
    if (!wheelTouch.has(ev.pointerId)) return;
    const d = degreeAt(ev);
    if (d === null || d === wheelTouch.get(ev.pointerId)) return;
    release('w' + ev.pointerId); wheelTouch.set(ev.pointerId, d); press('w' + ev.pointerId, inWindow(d));
  });
  ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(t => wheelSvg.addEventListener(t, ev => {
    if (!wheelTouch.has(ev.pointerId)) return;
    wheelTouch.delete(ev.pointerId); release('w' + ev.pointerId);
  }));

  /* ================================================================== keys and notes */
  /* held: what is pressed now; sounding: what the engine was told. A key let go while the audio is still
   * starting never sends its note at all, so nothing can stick. */
  const held = new Map(), sounding = new Map();  /* source id → absolute degree */
  /* Hold latches, as on the FM-1: a key pressed while it is on starts a note that keeps sounding, pressed again it
   * stops it. Turning Hold off releases the latched notes. */
  let holdOn = false;
  const latched = new Map();                    /* absolute degree → part */
  function setHold(on) {
    holdOn = on; $('holdBtn').setAttribute('aria-pressed', String(on));
    if (!on) { for (const [abs, part] of latched) if (![...sounding.values()].some(x => x.abs === abs && x.part === part)) send({ t: 'off', part, d: abs }); latched.clear(); }
    refreshLights();
  }
  async function press(id, abs, vel = 100) {
    if (held.has(id)) return;
    if (holdOn) {
      held.set(id, abs);
      if (!node) await startAudio();
      if (latched.has(abs)) { send({ t: 'off', part: latched.get(abs), d: abs }); latched.delete(abs); }
      else { const pt = playPart(); latched.set(abs, pt); send({ t: 'on', part: pt, d: abs, v: vel }); recordNote(abs, vel); }
      refreshLights();
      return;
    }
    held.set(id, abs); refreshLights();
    if (!node) await startAudio();
    if (held.get(id) !== abs || sounding.has(id)) return;
    const pt = playPart();
    if (![...sounding.values()].some(x => x.abs === abs && x.part === pt)) send({ t: 'on', part: pt, d: abs, v: vel });
    sounding.set(id, { abs, part: pt });
    recordNote(abs, vel);
  }
  function release(id) {
    held.delete(id);
    if (sounding.has(id)) {
      const sn = sounding.get(id); sounding.delete(id);
      if (![...sounding.values()].some(x => x.abs === sn.abs && x.part === sn.part) && latched.get(sn.abs) !== sn.part) send({ t: 'off', part: sn.part, d: sn.abs });
    }
    refreshLights();
  }
  function releaseAll() { held.clear(); sounding.clear(); latched.clear(); send({ t: 'all' }); refreshLights(); }
  function refreshLights() {
    if (!cents.length) return;
    const n = nDeg(), on = new Set([...held.values(), ...latched.keys()].map(a => mod(a, n)));
    document.querySelectorAll('#wheel g[data-deg]').forEach(g => { const d = +g.dataset.deg;
      g.querySelector('.sector').classList.toggle('on', on.has(d)); g.querySelector('.dot').classList.toggle('on', on.has(d)); });
    const absOn = new Set([...held.values(), ...latched.keys()]);
    document.querySelectorAll('.key').forEach(k => k.classList.toggle('on', absOn.has(winStart + +k.dataset.j)));
  }
  function renderKeys() {
    const box = $('keys'), n = nDeg(); box.textContent = '';
    for (let j = 0; j < 16; j++) {
      const abs = winStart + j, d = mod(abs, n), per = Math.floor(abs / n);
      const b = document.createElement('button'); b.type = 'button'; b.className = 'key' + (d === 0 ? ' first' : ''); b.dataset.j = j;
      b.setAttribute('aria-label', `Degree ${d}, period ${per}`);
      b.innerHTML = '<span class="d"></span><span class="p"></span>';
      b.querySelector('.d').textContent = d; b.querySelector('.p').textContent = (per >= 0 ? '+' : '') + per;
      b.addEventListener('pointerdown', e => { e.preventDefault(); b.releasePointerCapture && b.releasePointerCapture(e.pointerId); press('k' + j, abs); });
      b.addEventListener('pointerenter', e => { if (e.buttons) press('k' + j, abs); });
      ['pointerup', 'pointerleave', 'pointercancel'].forEach(t => b.addEventListener(t, () => release('k' + j)));
      box.append(b);
    }
    $('winNote').textContent = n > 16 ? `Degrees ${mod(winStart, n)} to ${mod(winStart + 15, n)}` : n < 16 ? 'The white keys run on into the next period' : 'One whole period in a row';
    refreshLights();
  }
  const shift = d => { winStart += d; renderKeys(); renderWheel(); };
  $('winDown').addEventListener('click', () => shift(-1));
  $('winUp').addEventListener('click', () => shift(1));
  $('perDown').addEventListener('click', () => shift(-nDeg()));
  $('perUp').addEventListener('click', () => shift(nDeg()));
  $('holdBtn').addEventListener('click', () => setHold(!holdOn));

  const KEYMAP = ['KeyZ', 'KeyX', 'KeyC', 'KeyV', 'KeyB', 'KeyN', 'KeyM', 'KeyA', 'KeyS', 'KeyD', 'KeyF', 'KeyG', 'KeyH', 'KeyJ', 'KeyK', 'KeyL'];
  window.addEventListener('keydown', e => {
    if (e.repeat || e.target.closest('input, select, textarea') || e.metaKey || e.ctrlKey) return;
    const j = KEYMAP.indexOf(e.code); if (j >= 0 && cents.length) press('q' + j, winStart + j);
  });
  window.addEventListener('keyup', e => { const j = KEYMAP.indexOf(e.code); if (j >= 0) release('q' + j); });
  window.addEventListener('blur', () => [...held.keys()].forEach(release));

  /* bend springs back, the wheel stays */
  const bend = $('bend');
  bend.addEventListener('input', () => send({ t: 'bend', part: playPart(), v: +bend.value }));
  ['pointerup', 'change', 'touchend'].forEach(t => bend.addEventListener(t, () => { bend.value = 0; send({ t: 'bend', part: playPart(), v: 0 }); }));
  $('wheelIn').addEventListener('input', e => send({ t: 'wheel', part: playPart(), v: +e.target.value }));

  /* ================================================================== MIDI in: notes, bend, wheel, pressure, NRPN */
  const WHITE = [0, 2, 4, 5, 7, 9, 11];
  const whiteIndex = note => { const o = Math.floor(note / 12), pc = note % 12, i = WHITE.indexOf(pc); return i < 0 ? null : o * 7 + i; };
  const nrpn = { msb: 127, lsb: 127, dmsb: 0 };
  function midiDegree(note) {
    if ($('midiMap').value === 'chrom') return winStart + (note - 60);
    const w = whiteIndex(note); return w === null ? null : winStart + (w - whiteIndex(60));
  }
  function onMidi(e) {
    const [st, d1 = 0, d2 = 0] = e.data, type = st & 0xf0;
    if (type === 0x90 && d2) { const d = midiDegree(d1); if (d !== null) press('m' + d1, d, d2); }
    else if (type === 0x80 || (type === 0x90 && !d2)) release('m' + d1);
    else if (type === 0xe0) send({ t: 'bend', part: playPart(), v: ((d2 << 7) | d1) - 8192 });
    else if (type === 0xd0) send({ t: 'at', part: playPart(), v: d1 });
    else if (type === 0xb0) {
      if (d1 === 1) { send({ t: 'wheel', part: playPart(), v: d2 }); $('wheelIn').value = d2; }
      else if (d1 === 99) nrpn.msb = d2; else if (d1 === 98) nrpn.lsb = d2;
      else if (d1 === 6 || d1 === 38) {
        if (d1 === 6) nrpn.dmsb = d2;
        const id = (nrpn.msb << 7) | nrpn.lsb;
        if (id < PARAMS.length) setParam(id, d1 === 6 ? d2 << 7 : (nrpn.dmsb << 7) | d2);
      } else if (d1 === 123) { send({ t: 'all' }); }
    }
  }
  $('midiBtn').addEventListener('click', async () => {
    if (!navigator.requestMIDIAccess) { $('midiState').textContent = 'MIDI is not available here. Open the file in Chrome.'; return; }
    try {
      const acc = await navigator.requestMIDIAccess();
      const hook = () => { const names = []; acc.inputs.forEach(i => { i.onmidimessage = onMidi; names.push(i.name); });
        midiOuts.length = 0; acc.outputs.forEach(o => midiOuts.push(o));
        $('midiState').textContent = names.length ? `Connected: ${names.join(', ')}` : 'No MIDI device connected'; };
      acc.onstatechange = hook; hook(); startAudio();
    } catch (err) { $('midiState').textContent = 'MIDI access was refused.'; }
  });

  /* ================================================================== sequencer
   * The sequencer runs inside the engine (engine/seq.c), the same C as on the FM-1. The page reads the pattern
   * from its own copy ("mirror") and sends every edit to both; the playing copy reports positions back. */
  const SF = { ON: 0, NOTES: 1, DEG: 2, PER: 6, CENTS: 10, VEL: 11, GATE: 12, PROB: 13, RAT: 14, SLIDE: 15, COND: 16, LID: 17, LVAL: 21 };
  const TF = { LEN: 0, SPEED: 1, DIR: 2, SWING: 3, TRANS: 4, BASE: 5, MUTE: 6, SOLO: 7, CHAN: 8 };
  const OP = { CLEAR_STEP: 0, CLEAR_TRACK: 1, CLEAR_PATTERN: 2, COPY_STEP: 3, PASTE_STEP: 4, COPY_PAGE: 5, PASTE_PAGE: 6,
    COPY_TRACK: 7, PASTE_TRACK: 8, COPY_PATTERN: 9, PASTE_PATTERN: 10, SHIFT: 11, REVERSE: 12, DOUBLE: 13, EUCLID: 14, RANDOM: 15, UNDO: 16 };
  const TRACKS = ['Sound 1', 'Sound 2', 'Sound 3', 'External MIDI', 'Drums A', 'Drums B'];
  const DRUMS = [['Kick', 'Snare', 'Closed hat', 'Open hat'], ['Clap', 'Low tom', 'High tom', 'Rim']];
  let editPat = 0, editTrack = 0, page = 0, selStep = 0, rec = false, stepRec = false, lastDeg = 0;
  let play = { playing: 0, pos: [-1, -1, -1, -1, -1, -1], pat: 0, q: -1, row: 0 };

  const sget = (s, f) => (mirror ? mirror.seq_step_get(editPat, editTrack, s, f) : 0);
  const tget = f => (mirror ? mirror.seq_track_get(editPat, editTrack, f) : 0);
  const isDrum = () => editTrack >= 4;
  let projTimer = 0;
  /* an edit: to the page's copy and to the playing one, then saved a moment later */
  function scall(f, ...a) {
    if (mirror) mirror[f](...a);
    send({ t: 'call', f, a });
    clearTimeout(projTimer); projTimer = setTimeout(saveProject, 800);
  }
  const ecall = (f, ...a) => send({ t: 'call', f, a });      /* transport: the playing copy only */
  function saveProject() {
    if (!mirror) return;
    const bytes = new Uint8Array(mirror.memory.buffer, mirror.seq_project_bytes(), mirror.seq_project_size()).slice();
    try { idb.set('project', bytes).catch(() => {}); } catch (e) { /* no storage */ }
  }

  /* one row of a control: label, slider or choice, value */
  function mini(label, kind, min, max, value, fmt, onset) {
    const row = document.createElement('div'); row.className = 'mini';
    const lab = document.createElement('label'); lab.textContent = label;
    let el, v = document.createElement('span'); v.className = 'val';
    if (kind === 'range' || kind === 'number') {
      el = document.createElement('input'); el.type = kind; el.min = min; el.max = max; el.step = 1; el.value = value;
      el.addEventListener(kind === 'range' ? 'input' : 'change', () => { const x = Math.max(min, Math.min(max, parseInt(el.value, 10) || 0)); v.textContent = fmt ? fmt(x) : ''; onset(x); });
    } else if (kind === 'check') {
      el = document.createElement('input'); el.type = 'checkbox'; el.checked = !!value;
      el.addEventListener('change', () => onset(el.checked ? 1 : 0));
    } else {
      el = document.createElement('select'); kind.forEach(([text, val]) => el.add(new Option(text, String(val)))); el.value = String(value);
      el.addEventListener('change', () => onset(parseInt(el.value, 10)));
    }
    el.id = 'sq-' + label.replace(/\s+/g, '-'); lab.htmlFor = el.id;
    v.textContent = fmt ? fmt(value) : '';
    row.append(lab, el, v);
    return row;
  }
  const heading = t => { const h = document.createElement('h3'); h.textContent = t; return h; };
  const button = (t, f) => { const b = document.createElement('button'); b.type = 'button'; b.textContent = t; b.addEventListener('click', f); return b; };

  function renderPatterns() {
    const box = $('patterns'); box.textContent = '';
    for (let p = 0; p < 16; p++) {
      let used = false;
      if (mirror) for (let t = 0; t < 6 && !used; t++) for (let s = 0; s < 64 && !used; s++) used = mirror.seq_step_get(p, t, s, SF.ON) === 1;
      const b = button(String(p + 1), () => { editPat = p; ecall('seq_queue_pattern', p); if (mirror) mirror.seq_queue_pattern(p); renderSeq(); });
      b.setAttribute('aria-pressed', String(p === editPat)); b.setAttribute('aria-label', `Pattern ${p + 1}`);
      if (used) b.classList.add('used');
      if (play.playing && play.pat === p) b.classList.add('playing');
      if (play.q === p) b.classList.add('queued');
      box.append(b);
    }
  }
  function renderTracks() {
    const box = $('tracks'); box.textContent = '';
    TRACKS.forEach((name, t) => {
      const row = document.createElement('div'); row.className = 'trk';
      const b = button(name, () => { editTrack = t; if (t < 3) setPart(t); renderSeq(); }); b.className = 'name';
      b.setAttribute('aria-pressed', String(t === editTrack));
      const mk = (txt, f, label) => { const x = button(txt, () => { scall('seq_track_set', editPat, t, f, mirror.seq_track_get(editPat, t, f) ? 0 : 1); renderTracks(); });
        x.className = 'ms'; x.setAttribute('aria-pressed', String(!!(mirror && mirror.seq_track_get(editPat, t, f)))); x.setAttribute('aria-label', `${label}: ${name}`); return x; };
      row.append(b, mk('M', TF.MUTE, 'Mute'), mk('S', TF.SOLO, 'Solo'));
      box.append(row);
    });
  }
  function stepLabel(s) {
    if (!sget(s, SF.ON)) return ['·', ''];
    if (isDrum()) { const m = sget(s, SF.DEG); return [[0, 1, 2, 3].map(i => (m >> i) & 1 ? '●' : '·').join(''), '']; }
    const per = sget(s, SF.PER), nn = sget(s, SF.NOTES);
    const marks = [];
    if (sget(s, SF.RAT) > 1) marks.push('×' + sget(s, SF.RAT));
    if (sget(s, SF.PROB) < 100) marks.push(sget(s, SF.PROB) + '%');
    if (sget(s, SF.COND)) { const c = sget(s, SF.COND); marks.push(`${c >> 4}:${c & 15}`); }
    if (sget(s, SF.SLIDE)) marks.push('~');
    if (sget(s, SF.GATE) === 17) marks.push('—');
    for (let k = 0; k < 4; k++) if (sget(s, SF.LID + k) !== 255) { marks.push('L'); break; }
    return [`${sget(s, SF.DEG)}${per > 0 ? "'".repeat(per) : per < 0 ? ','.repeat(-per) : ''}${nn > 1 ? '+' + (nn - 1) : ''}`, marks.join(' ')];
  }
  function renderSteps() {
    const box = $('steps'); box.textContent = '';
    const len = tget(TF.LEN), now = play.playing && play.pat === editPat ? play.pos[editTrack] : -1;
    for (let j = 0; j < 16; j++) {
      const s = page * 16 + j, b = document.createElement('button'); b.type = 'button'; b.className = 'cell'; b.dataset.s = s;
      const [d, f] = stepLabel(s);
      b.innerHTML = '<span class="n"></span><span class="d"></span><span class="f"></span>';
      b.querySelector('.n').textContent = s + 1; b.querySelector('.d').textContent = d; b.querySelector('.f').textContent = f;
      if (sget(s, SF.ON)) b.classList.add('on');
      if (s === selStep) b.classList.add('sel');
      if (s === now) b.classList.add('now');
      if (s >= len) b.classList.add('out');
      if (j % 4 === 0) b.classList.add('beat');
      b.setAttribute('aria-label', `Step ${s + 1}${sget(s, SF.ON) ? ', on' : ''}`);
      b.addEventListener('click', () => {
        if (s !== selStep) { selStep = s; if (!sget(s, SF.ON)) stepOn(s); }
        else scall('seq_step_set', editPat, editTrack, s, SF.ON, sget(s, SF.ON) ? 0 : 1);
        renderSteps(); renderStepEd(); renderPatterns();
      });
      b.addEventListener('dblclick', () => { scall('seq_op', OP.CLEAR_STEP, editPat, editTrack, s, 0, 0, 0); renderSteps(); renderStepEd(); });
      box.append(b);
    }
    const pg = $('pages'); pg.textContent = '';
    for (let k = 0; k < 4; k++) { const b = button(`${k * 16 + 1}–${k * 16 + 16}`, () => { page = k; renderSteps(); }); b.setAttribute('aria-pressed', String(k === page)); pg.append(b); }
    $('seqNote').textContent = rec ? 'Live record: what you play is written to the playing step, and so are slider moves.' : stepRec ? 'Step record: each note you play is written to the selected step, and the selection moves on.' : 'Click a step to select it and turn it on; click again to turn it off; double-click clears it.';
  }
  function stepOn(s) {
    scall('seq_step_set', editPat, editTrack, s, SF.ON, 1);
    if (isDrum() && !sget(s, SF.DEG)) scall('seq_step_set', editPat, editTrack, s, SF.DEG, 1);
    else if (!isDrum() && !sget(s, SF.DEG) && !sget(s, SF.PER)) writeDegree(s, lastDeg, 0);
  }
  /* an absolute degree into a step note, against the track's transpose and base period */
  function writeDegree(s, abs, k) {
    const n = nDeg(), rel = abs - tget(TF.TRANS), per = Math.floor(rel / n) - tget(TF.BASE);
    scall('seq_step_set', editPat, editTrack, s, SF.DEG + k, mod(rel, n));
    scall('seq_step_set', editPat, editTrack, s, SF.PER + k, Math.max(-3, Math.min(3, per)));
  }

  const gateText = g => g === 17 ? 'tie' : g === 16 ? 'full' : `${g}/16`;
  const CONDS = [['always', 0]];
  for (let b = 2; b <= 8; b++) for (let a = 1; a <= b; a++) CONDS.push([`pass ${a} of ${b}`, (a << 4) | b]);
  const PARAM_NAMES = PARAMS.map(p => {
    const t = TABS.find(([, g]) => g !== 'mod' && g.some(([, keys]) => keys.includes(p.key)));
    const grp = t ? t[1].find(([, keys]) => keys.includes(p.key))[0] : 'Modulation';
    return [`${grp}: ${L[p.key] ? L[p.key][0] : p.key}`, p.id];
  });
  function renderStepEd() {
    const box = $('stepEd'); box.textContent = '';
    const s = selStep, set = (f, v) => { scall('seq_step_set', editPat, editTrack, s, f, v); renderSteps(); };
    box.append(heading(`Step ${s + 1}`));
    box.append(mini('On', 'check', 0, 1, sget(s, SF.ON), null, v => { set(SF.ON, v); renderPatterns(); }));
    if (isDrum()) {
      const m = sget(s, SF.DEG), names = DRUMS[editTrack - 4];
      names.forEach((nm, i) => box.append(mini(nm, 'check', 0, 1, (m >> i) & 1, null, v => set(SF.DEG, v ? sget(s, SF.DEG) | (1 << i) : sget(s, SF.DEG) & ~(1 << i)))));
    } else {
      const nn = sget(s, SF.NOTES), n = nDeg();
      box.append(mini('Notes in the step', [['1', 1], ['2', 2], ['3', 3], ['4', 4]], 1, 4, nn, null, v => { set(SF.NOTES, v); renderStepEd(); }));
      for (let k = 0; k < nn; k++) {
        const row = document.createElement('div'); row.className = 'noterow';
        const lab = document.createElement('span'); lab.className = 'note'; lab.textContent = `Note ${k + 1}`;
        const d = document.createElement('input'); d.type = 'number'; d.min = 0; d.max = n - 1; d.value = sget(s, SF.DEG + k); d.className = 'mono';
        d.setAttribute('aria-label', `Degree of note ${k + 1}`);
        d.addEventListener('change', () => set(SF.DEG + k, Math.max(0, Math.min(n - 1, parseInt(d.value, 10) || 0))));
        const pr = document.createElement('select'); pr.setAttribute('aria-label', `Period of note ${k + 1}`);
        for (let x = -3; x <= 3; x++) pr.add(new Option(x === 0 ? 'period 0' : `period ${x > 0 ? '+' : ''}${x}`, String(x)));
        pr.value = String(sget(s, SF.PER + k)); pr.addEventListener('change', () => set(SF.PER + k, +pr.value));
        row.append(lab, d, pr); box.append(row);
      }
      box.append(mini('Cents', 'range', -50, 50, sget(s, SF.CENTS), v => `${v > 0 ? '+' : ''}${v} ¢`, v => set(SF.CENTS, v)));
    }
    box.append(mini('Velocity', 'range', 1, 127, sget(s, SF.VEL), v => v, v => set(SF.VEL, v)));
    if (!isDrum()) box.append(mini('Length', 'range', 1, 17, sget(s, SF.GATE), gateText, v => set(SF.GATE, v)));
    box.append(mini('Probability', 'range', 0, 100, sget(s, SF.PROB), v => v + '%', v => set(SF.PROB, v)));
    box.append(mini('Ratchet', [['1', 1], ['2', 2], ['3', 3], ['4', 4]], 1, 4, sget(s, SF.RAT), null, v => set(SF.RAT, v)));
    if (!isDrum()) box.append(mini('Slide to the next step', 'check', 0, 1, sget(s, SF.SLIDE), null, v => set(SF.SLIDE, v)));
    box.append(mini('Condition', CONDS, 0, 0, sget(s, SF.COND), null, v => set(SF.COND, v)));
    if (editTrack < 3) {
      box.append(heading('Parameter locks'));
      let count = 0;
      for (let k = 0; k < 4; k++) {
        const id = sget(s, SF.LID + k); if (id === 255) continue; count++;
        const p = PARAMS[id], row = document.createElement('div'); row.className = 'lockrow';
        const sel = document.createElement('select'); PARAM_NAMES.forEach(([t, i]) => sel.add(new Option(t, String(i)))); sel.value = String(id);
        sel.setAttribute('aria-label', `Lock ${k + 1}: parameter`);
        sel.addEventListener('change', () => { set(SF.LID + k, +sel.value); set(SF.LVAL + k, Math.min(sget(s, SF.LVAL + k), PARAMS[+sel.value].max)); renderStepEd(); });
        const r = document.createElement('input'); r.type = 'range'; r.min = 0; r.max = p.max; r.value = sget(s, SF.LVAL + k); r.setAttribute('aria-label', `Lock ${k + 1}: value`);
        const v = document.createElement('span'); v.className = 'val';
        const fmt = x => (Array.isArray(L[p.key] && L[p.key][1]) ? L[p.key][1][x] : L[p.key] ? L[p.key][1](x) : x);
        v.textContent = fmt(+r.value);
        r.addEventListener('input', () => { v.textContent = fmt(+r.value); set(SF.LVAL + k, +r.value); });
        const x = button('✕', () => { scall('seq_step_unlock', editPat, editTrack, s, id); renderSteps(); renderStepEd(); }); x.setAttribute('aria-label', 'Remove the lock');
        row.append(sel, r, v, x); box.append(row);
      }
      if (count < 4) box.append(button('New lock', () => { scall('seq_step_lock', editPat, editTrack, s, byKey.cutoff.id, vals[editTrack][byKey.cutoff.id]); renderSteps(); renderStepEd(); }));
    }
  }
  function renderTrackEd() {
    const box = $('trackEd'); box.textContent = '';
    const set = (f, v) => { scall('seq_track_set', editPat, editTrack, f, v); renderSteps(); };
    box.append(heading(`Track: ${TRACKS[editTrack]}`));
    box.append(mini('Length', 'range', 1, 64, tget(TF.LEN), v => `${v} steps`, v => set(TF.LEN, v)));
    box.append(mini('Speed', [['×2', 0], ['×1', 1], ['×1/2', 2], ['×1/4', 3], ['×4/3 triplets', 4], ['×2/3', 5]], 0, 5, tget(TF.SPEED), null, v => set(TF.SPEED, v)));
    box.append(mini('Direction', [['forward', 0], ['backward', 1], ['ping-pong', 2], ['random', 3]], 0, 3, tget(TF.DIR), null, v => set(TF.DIR, v)));
    box.append(mini('Swing', 'range', 50, 75, tget(TF.SWING), v => v + '%', v => set(TF.SWING, v)));
    if (!isDrum()) {
      box.append(mini('Transpose', 'range', -64, 64, tget(TF.TRANS), v => `${v > 0 ? '+' : ''}${v} degrees`, v => set(TF.TRANS, v)));
      box.append(mini('Base period', 'range', -3, 3, tget(TF.BASE), v => `${v > 0 ? '+' : ''}${v}`, v => set(TF.BASE, v)));
    }
    if (editTrack >= 3) box.append(mini('MIDI channel', 'range', 1, 16, tget(TF.CHAN), v => v, v => set(TF.CHAN, v)));
    box.append(heading('Edit'));
    const op = (o, s = 0, a = 0, b = 0, c = 0) => () => { scall('seq_op', o, editPat, editTrack, s, a, b, c); renderSeq(); };
    const tools = document.createElement('div'); tools.className = 'tools';
    tools.append(button('Copy step', op(OP.COPY_STEP, selStep)), button('Paste step', () => op(OP.PASTE_STEP, selStep)()),
      button('Copy page', () => op(OP.COPY_PAGE, 0, page)()), button('Paste page', () => op(OP.PASTE_PAGE, 0, page)()),
      button('Copy track', op(OP.COPY_TRACK)), button('Paste track', op(OP.PASTE_TRACK)),
      button('Copy pattern', op(OP.COPY_PATTERN)), button('Paste pattern', op(OP.PASTE_PATTERN)),
      button('Clear track', op(OP.CLEAR_TRACK)), button('Clear pattern', op(OP.CLEAR_PATTERN)),
      button('Shift ←', op(OP.SHIFT, 0, -1)), button('Shift →', op(OP.SHIFT, 0, 1)), button('Reverse', op(OP.REVERSE)), button('Double', op(OP.DOUBLE)));
    box.append(tools);
    /* generators */
    const g = { hits: 5, rot: 0, deg: lastDeg, dens: 60, lo: 0, hi: 2 * nDeg() };
    box.append(heading('Generators'));
    box.append(mini('Hits', 'range', 0, 64, g.hits, v => v, v => { g.hits = v; }));
    box.append(mini('Rotation', 'range', 0, 63, g.rot, v => v, v => { g.rot = v; }));
    box.append(button('Euclidean rhythm', () => op(OP.EUCLID, 0, g.hits, g.rot, isDrum() ? 1 : lastDeg)()));
    if (!isDrum()) {
      box.append(mini('Density', 'range', 0, 100, g.dens, v => v + '%', v => { g.dens = v; }));
      box.append(mini('Lowest degree', 'number', -64, 255, g.lo, null, v => { g.lo = v; }));
      box.append(mini('Highest degree', 'number', -64, 255, g.hi, null, v => { g.hi = v; }));
      box.append(button('Random melody in the scale', () => op(OP.RANDOM, 0, g.dens, g.lo, g.hi)()));
    }
  }
  function renderSongEd() {
    const box = $('songEd'); box.textContent = '';
    if (!mirror) return;
    box.append(heading(`Pattern ${editPat + 1}`));
    box.append(mini('Pattern length', 'range', 1, 64, mirror.seq_pattern_get(editPat, 0), v => `${v} steps`, v => scall('seq_pattern_set', editPat, 0, v)));
    const sc = mirror.seq_pattern_get(editPat, 1), cur = current && !tun.base.edo ? lib.scales.indexOf(current) : -1;
    const opts = [['no scale change', -1]];
    if (sc >= 0 && lib && lib.scales[sc]) opts.push([`scale ${lib.scales[sc].name}`, sc]);
    if (cur >= 0 && cur !== sc) opts.push([`the current scale: ${current.name}`, cur]);
    box.append(mini('Pattern scale', opts, -1, 0, sc, null, v => { scall('seq_pattern_set', editPat, 1, v); renderSongEd(); }));
    box.append(heading('Song'));
    const n = mirror.seq_song_len();
    for (let r = 0; r < n; r++) {
      const row = document.createElement('div'); row.className = 'songrow' + (play.playing && mirror && play.row === r && songOn() ? ' now' : '');
      const i = document.createElement('span'); i.className = 'note'; i.textContent = r + 1;
      const ps = document.createElement('select'); for (let p = 0; p < 16; p++) ps.add(new Option(`Pattern ${p + 1}`, String(p)));
      ps.value = String(mirror.seq_song_get(r, 0)); ps.setAttribute('aria-label', `Row ${r + 1}: pattern`);
      const rp = document.createElement('input'); rp.type = 'number'; rp.min = 1; rp.max = 64; rp.value = mirror.seq_song_get(r, 1); rp.className = 'mono';
      rp.setAttribute('aria-label', `Row ${r + 1}: repeats`);
      const upd = () => scall('seq_song_set', r, +ps.value, Math.max(1, Math.min(64, parseInt(rp.value, 10) || 1)));
      ps.addEventListener('change', upd); rp.addEventListener('change', upd);
      const x = button('✕', () => {
        for (let k = r; k < n - 1; k++) scall('seq_song_set', k, mirror.seq_song_get(k + 1, 0), mirror.seq_song_get(k + 1, 1));
        scall('seq_song_length', Math.max(1, n - 1)); renderSongEd();
      }); x.setAttribute('aria-label', `Delete row ${r + 1}`);
      row.append(i, ps, rp, x); box.append(row);
    }
    if (n < 64) box.append(button('New row', () => { scall('seq_song_set', n, editPat, 1); renderSongEd(); }));
  }
  const songOn = () => mirror && mirror.seq_song_on();
  function renderSeq() {
    renderPatterns(); renderTracks(); renderSteps(); renderStepEd(); renderTrackEd(); renderSongEd();
    $('songBtn').setAttribute('aria-pressed', String(!!songOn()));
    if (mirror) $('tempo').value = (mirror.seq_tempo() / 100).toFixed(1);
  }

  /* transport */
  $('playBtn').addEventListener('click', async () => {
    if (!node) await startAudio();
    if (play.playing) ecall('seq_stop'); else ecall('seq_play');
  });
  $('tempo').addEventListener('change', () => scall('seq_set_tempo', Math.round(Math.max(30, Math.min(300, parseFloat($('tempo').value) || 120)) * 100)));
  $('songBtn').addEventListener('click', () => { scall('seq_song_mode', songOn() ? 0 : 1); renderSeq(); });
  $('recBtn').addEventListener('click', () => { rec = !rec; if (rec) stepRec = false; $('recBtn').setAttribute('aria-pressed', String(rec)); $('stepRecBtn').setAttribute('aria-pressed', 'false'); renderSteps(); });
  $('stepRecBtn').addEventListener('click', () => { stepRec = !stepRec; if (stepRec) rec = false; $('stepRecBtn').setAttribute('aria-pressed', String(stepRec)); $('recBtn').setAttribute('aria-pressed', 'false'); renderSteps(); });
  $('undoBtn').addEventListener('click', () => { scall('seq_op', OP.UNDO, editPat, 0, 0, 0, 0, 0); renderSeq(); });

  /* playing and editing meet: a note played while recording, a knob turned while recording */
  function recordNote(abs, vel) {
    lastDeg = abs;
    if (isDrum()) return;
    if (stepRec) {
      writeDegree(selStep, abs, 0);
      scall('seq_step_set', editPat, editTrack, selStep, SF.ON, 1);
      scall('seq_step_set', editPat, editTrack, selStep, SF.VEL, vel);
      selStep = (selStep + 1) % Math.max(1, tget(TF.LEN)); page = Math.floor(selStep / 16);
      renderSteps(); renderStepEd();
    } else if (rec && play.playing && play.pat === editPat && play.pos[editTrack] >= 0) {
      const s = play.pos[editTrack];
      writeDegree(s, abs, 0);
      scall('seq_step_set', editPat, editTrack, s, SF.ON, 1);
      scall('seq_step_set', editPat, editTrack, s, SF.VEL, vel);
    }
  }
  function recordLock(id, v) {
    if (!rec || !play.playing || play.pat !== editPat || editTrack > 2 || part !== editTrack || !mirror) return;
    const s = play.pos[editTrack];
    if (s >= 0) scall('seq_step_lock', editPat, editTrack, s, id, v);
  }

  /* reports from the playing engine */
  let lastPos = '';
  function fromSeq(m) {
    if (m.t === 'v') {
      play = { playing: m.playing, pos: m.pos, pat: m.pat, q: m.q, row: m.row };
      $('playBtn').textContent = play.playing ? 'Stop' : 'Play';
      const key = JSON.stringify([m.playing, m.pos[editTrack], m.pat, m.q, m.row]);
      if (key !== lastPos) {
        lastPos = key;
        const now = play.playing && play.pat === editPat ? play.pos[editTrack] : -1;
        document.querySelectorAll('#steps .cell').forEach(c => c.classList.toggle('now', +c.dataset.s === now));
        document.querySelectorAll('#patterns button').forEach((b, p) => { b.classList.toggle('playing', !!play.playing && play.pat === p); b.classList.toggle('queued', play.q === p); });
        if (songOn()) document.querySelectorAll('#songEd .songrow').forEach((r, i) => r.classList.toggle('now', !!play.playing && play.row === i));
      }
    } else if (m.t === 'midi') {
      if (midiOuts.length) for (const x of m.m) { const msg = [x >> 16 & 255, x >> 8 & 255, x & 255]; midiOuts.forEach(o => { try { o.send(msg); } catch (e) { /* gone */ } }); }
    } else if (m.t === 'scalereq') {
      if (lib && lib.scales[m.i]) { releaseAll(); current = lib.scales[m.i]; tun.base = { lib: current.file }; tun.mode = 0; tun.fine = {}; applyTuning(); }
    }
  }
  const midiOuts = [];

  /* ================================================================== start */
  renderTabs();
  renderKit();
  (async () => {
    try { mirror = (await WebAssembly.instantiate(b64bytes(window.FM1_WASM_B64), {})).instance.exports; mirror.w_init(); }
    catch (e) { setStatus('The engine did not load in this browser.', true); }
    try {
      const pj = await idb.get('project');
      if (mirror && pj && pj.length === mirror.seq_project_size()) {
        new Uint8Array(mirror.memory.buffer, mirror.seq_project_bytes(), pj.length).set(pj);
        mirror.seq_set_tempo(mirror.seq_tempo());
      }
    } catch (e) { /* no saved project */ }
    let stored = null;
    try { stored = await idb.get('files'); } catch (e) { stored = null; }
    if (stored && stored.length) { files = stored; isExample = false; }
    rebuild();
    renderSeq();
    setStatus(isExample ? `${lib.scales.length} built-in scales from around the world. Load Scala files to add more.` : `${lib.scales.length.toLocaleString('en')} scales: the built-in ones and your saved library.`);
  })();
})();

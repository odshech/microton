/* The AudioWorklet: runs engine.wasm, the same C as on the FM-1, 128 frames at a time. */
class Fm1Engine extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    this.libLen = 0;
    this.tick = 0;
    this.x = null;
    try {
      this.x = new WebAssembly.Instance(new WebAssembly.Module(opts.processorOptions.wasm), {}).exports;
      this.x.w_init();
      this.port.postMessage({ t: 'ready' });
    } catch (err) {
      this.port.postMessage({ t: 'fail', msg: String(err && err.message || err) });
    }
    this.port.onmessage = e => { if (this.x) this.msg(e.data); };
  }
  msg(m) {
    const x = this.x;
    switch (m.t) {
      case 'on': x.eng_note_on(m.part | 0, m.d, m.v); break;
      case 'off': x.eng_note_off(m.part | 0, m.d); break;
      case 'all': x.eng_all_off(); break;
      case 'p': x.eng_set_param(m.part | 0, m.id, m.v); break;
      case 'bend': x.eng_bend(m.part | 0, m.v); break;
      case 'wheel': x.eng_modwheel(m.part | 0, m.v); break;
      case 'at': x.eng_aftertouch(m.part | 0, m.v); break;
      case 'proj': new Uint8Array(x.memory.buffer, x.seq_project_bytes(), m.bytes.length).set(m.bytes); x.seq_set_tempo(x.seq_tempo()); break;
      case 'ref': x.eng_set_ref_chz(m.v); break;
      case 'lib': {
        if (m.bytes.length > x.w_lib_cap()) { this.port.postMessage({ t: 'err', what: 'lib' }); break; }
        new Uint8Array(x.memory.buffer, x.w_lib(), m.bytes.length).set(m.bytes);
        this.libLen = m.bytes.length;
        break;
      }
      case 'call': x[m.f](...m.a); break;                 /* tun_* : the tuning calls, the same as on the page */
      case 'scale': {
        const ok = x.w_scale(this.libLen, m.i) === 0;
        this.port.postMessage({ t: 'scale', ok, n: x.eng_scale_degrees() });
        break;
      }
    }
  }
  process(_inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], x = this.x;
    if (!x) return true;
    x.w_render();
    if (this.buf !== x.memory.buffer) { this.buf = x.memory.buffer; this.view = new Int32Array(this.buf, x.w_out(), 256); }
    const b = this.view;                                  /* no allocation per block: nothing for the GC to do */
    for (let i = 0; i < 128; i++) { L[i] = b[2 * i] / 32768; R[i] = b[2 * i + 1] / 32768; }
    /* the external and drum tracks' MIDI, as it happens; the playing state ~30 times a second */
    let m, midi = null;
    while ((m = x.w_midi()) >= 0) (midi || (midi = [])).push(m);
    if (midi) this.port.postMessage({ t: 'midi', m: midi });
    const req = x.seq_scale_request();
    if (req >= 0) this.port.postMessage({ t: 'scalereq', i: req });
    if ((++this.tick & 7) === 0) {
      const pos = [];
      for (let i = 0; i < 6; i++) pos.push(x.seq_track_pos(i));
      this.port.postMessage({ t: 'v', n: x.eng_voices_active(), pos, pat: x.seq_pattern_playing(), q: x.seq_pattern_queued(),
        row: x.seq_song_row(), playing: x.seq_playing() });
    }
    return true;
  }
}
registerProcessor('fm1-engine', Fm1Engine);

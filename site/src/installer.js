/* installer.js — the page around Felucca's fm1pkg.js and fm1ota.js (inlined before this script by the build). */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const log = m => { const l = $('log'); l.textContent += m + '\n'; l.scrollTop = l.scrollHeight; };
  const say = (msg, kind) => { const s = $('status'); s.textContent = msg; s.className = 'status' + (kind ? ' ' + kind : ''); };
  const TEXT = {
    notfound: 'המכשיר לא נמצא. בדקו שזה כבל נתונים ושאין תוכנה אחרת שמשתמשת בו.',
    model: 'הקובץ לא מתאים למכשיר הזה.',
    stopped: 'המכשיר הפסיק לענות. חברו שוב ולחצו התקנה.',
    lost: 'הכבל התנתק. חברו שוב ולחצו התקנה כדי להמשיך.',
    noloader: 'מצב העדכון לא עלה. חברו את הכבל מחדש ולחצו התקנה שוב.',
    noreturn: 'הכתיבה הסתיימה אבל המכשיר לא חזר. כבו והדליקו אותו.',
    mismatch: 'הותקן, אבל המכשיר מדווח על קושחה אחרת: ',
    badreq: 'המכשיר ביקש חלק שלא קיים בקובץ. ייתכן שהקובץ פגום.',
  };
  let image = null, product = '', busy = false;

  $('file').addEventListener('change', async () => {
    const f = $('file').files[0];
    image = null; $('go').disabled = true; $('pkgInfo').textContent = '';
    if (!f) return;
    try {
      const pkg = new Uint8Array(await f.arrayBuffer());
      product = productOf(pkg);
      if (!/^FM-1_\d+$/.test(product)) throw new Error('זה לא קובץ קושחה של FM-1');
      image = logicalImage(pkg);
      $('pkgInfo').textContent = `${f.name} · ${product} · ${(pkg.length / 1024).toFixed(0)} KB`;
      say('מוכן להתקנה.');
      $('go').disabled = false;
    } catch (e) { say(e.message || 'הקובץ לא נקרא.', 'err'); }
  });

  $('go').addEventListener('click', async () => {
    if (busy || !image) return;
    if (!navigator.requestMIDIAccess) { say('הדפדפן הזה לא תומך במידי. פתחו את הדף בכרום במחשב.', 'err'); return; }
    busy = true; $('go').disabled = true; $('file').disabled = true;
    let midi;
    try { midi = await navigator.requestMIDIAccess({ sysex: true }); }
    catch (e) { say('הגישה למידי נדחתה. אשרו אותה בבקשה של הדפדפן.', 'err'); busy = false; $('go').disabled = false; $('file').disabled = false; return; }
    const total = Math.ceil(image.length / 512) * 1.06 + 80;    /* requests: a pass to check, a pass to write */
    let verified = 0;
    const step = (k, a) => {
      log(`${k} ${a ?? ''}`);
      if (k === 'start') say(`מחובר: ${a}. בודק את הקובץ…`);
      if (k === 'verify') { verified = a; $('bar').value = Math.min(20, a / total * 100); }
      if (k === 'loader') say('עובר למצב עדכון…');
      if (k === 'write') { say('כותב…'); $('bar').value = Math.min(99, 20 + (a / total) * 80); }
      if (k === 'reboot') say('מאתחל…');
      if (k === 'done') { $('bar').value = 100; say(`הותקן: ${a}.`, 'ok'); }
    };
    try {
      const up = new Updater(midi, log);
      const ota = await up.find(id => /^ota-/i.test(id.model));
      if (ota) {
        ota.link.close();
        say('המכשיר כבר במצב עדכון: ממשיך את הכתיבה…');
        const ok = await up.resume(image, step, product);
        if (!ok) say(TEXT.stopped, 'err');
      } else {
        await up.install(image, product, step);
      }
    } catch (e) {
      say(e && e.code && TEXT[e.code] ? TEXT[e.code] + (e.code === 'mismatch' ? e.detail : '') : `ההתקנה נעצרה: ${e && e.message}`, 'err');
      log(String(e && e.stack || e));
    }
    void verified;
    busy = false; $('go').disabled = false; $('file').disabled = false;
  });
})();

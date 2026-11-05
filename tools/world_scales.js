/* world_scales.js — the scales built into Microton, on the FM-1 and in the browser. One list, used by
 * tools/gen_device_scales.js (the device's library) and tools/build_site.py (the page's built-in scales).
 * Each: [file name (at most 16 characters before .scl: the device keeps 16), description, the degrees].
 * The degrees are Scala lines: a ratio (3/2) or cents (701.955), the last one the period.
 * Where a tradition's tuning varies from player to player (maqam, dastgah, gamelan, kiñit) these are common
 * textbook values, marked "approx." in the description. */
'use strict';
const edo = (n, p = 1200) => Array.from({ length: n }, (_, k) => (p * (k + 1) / n).toFixed(5)).join('\n');
const commas = (cum) => cum.map(c => (c * 1200 / 53).toFixed(3)).join('\n');           /* Turkish: Holdrian commas */
const moria = (cum) => cum.map(c => (c * 1200 / 72).toFixed(3)).join('\n');            /* Byzantine: 72 moria */
const cents = (...c) => c.map(x => x.toFixed(1)).join('\n');

/* the first fifteen are the FM-1's original list, in its original order (saved scale numbers stay valid) */
const BASE = [
  ['12-edo', '12 equal steps to the octave', edo(12)], ['17-edo', '17 equal steps to the octave', edo(17)],
  ['19-edo', '19 equal steps to the octave', edo(19)], ['22-edo', '22 equal steps to the octave', edo(22)],
  ['24-edo', 'quarter tones', edo(24)], ['31-edo', '31 equal steps to the octave', edo(31)],
  ['41-edo', '41 equal steps to the octave', edo(41)], ['53-edo', '53 equal steps to the octave', edo(53)],
  ['bp-equal', 'Bohlen-Pierce: 13 equal steps to the 3/1', edo(13, 1901.955)],
  ['just-major', 'just major = India: thaat Bilaval', '9/8\n5/4\n4/3\n3/2\n5/3\n15/8\n2/1'],
  ['pythagorean', 'Pythagorean, 12 notes', '256/243\n9/8\n32/27\n81/64\n4/3\n729/512\n3/2\n128/81\n27/16\n16/9\n243/128\n2/1'],
  ['meantone', 'quarter-comma meantone', '76.04900\n193.15686\n310.26471\n386.31371\n503.42157\n579.47057\n696.57843\n772.62743\n889.73529\n1006.84314\n1082.89214\n1200.0'],
  ['harmonics', 'harmonics 8 to 16', '9/8\n10/8\n11/8\n12/8\n13/8\n14/8\n15/8\n16/8'],
  ['pelog', 'Javanese pelog, approx., 7 notes', cents(120, 270, 540, 670, 785, 950, 1200)],
  ['slendro', 'Javanese slendro, equal 5 (= 5-EDO)', cents(240, 480, 720, 960, 1200)],
];

const WORLD = [
  /* India: the 22 shrutis, and the ten thaats of Hindustani music in just intonation */
  ['shruti-22', 'India: the 22 shrutis', '256/243\n16/15\n10/9\n9/8\n32/27\n6/5\n5/4\n81/64\n4/3\n27/20\n45/32\n729/512\n3/2\n128/81\n8/5\n5/3\n27/16\n16/9\n9/5\n15/8\n243/128\n2/1'],
  ['kalyan-yaman', 'India: thaat Kalyan, raga Yaman (just)', '9/8\n5/4\n45/32\n3/2\n5/3\n15/8\n2/1'],
  ['khamaj', 'India: thaat Khamaj (just)', '9/8\n5/4\n4/3\n3/2\n5/3\n16/9\n2/1'],
  ['kafi', 'India: thaat Kafi (just)', '9/8\n6/5\n4/3\n3/2\n5/3\n9/5\n2/1'],
  ['asavari', 'India: thaat Asavari (just)', '9/8\n6/5\n4/3\n3/2\n8/5\n9/5\n2/1'],
  ['bhairavi', 'India: thaat Bhairavi (just) = Ptolemy intense diatonic', '16/15\n6/5\n4/3\n3/2\n8/5\n9/5\n2/1'],
  ['bhairav', 'India: thaat Bhairav (just)', '16/15\n5/4\n4/3\n3/2\n8/5\n15/8\n2/1'],
  ['todi', 'India: thaat Todi (just)', '16/15\n6/5\n45/32\n3/2\n8/5\n15/8\n2/1'],
  ['purvi', 'India: thaat Purvi (just)', '16/15\n5/4\n45/32\n3/2\n8/5\n15/8\n2/1'],
  ['marwa', 'India: thaat Marwa (just)', '16/15\n5/4\n45/32\n3/2\n5/3\n15/8\n2/1'],
  ['bhupali', 'India: raga Bhupali, pentatonic (just)', '9/8\n5/4\n3/2\n5/3\n2/1'],
  ['malkauns', 'India: raga Malkauns, pentatonic (just)', '6/5\n4/3\n8/5\n9/5\n2/1'],
  /* the Arab world: maqamat on the quarter-tone grid */
  ['maqam-rast', 'Arab maqam Rast, approx. (quarter tones)', cents(200, 350, 500, 700, 900, 1050, 1200)],
  ['maqam-bayati', 'Arab maqam Bayati = Persian dastgah Shur, approx.', cents(150, 300, 500, 700, 800, 1000, 1200)],
  ['maqam-saba', 'Arab maqam Saba, approx.', cents(150, 300, 400, 600, 800, 1000, 1200)],
  ['maqam-hijaz', 'Arab maqam Hijaz, approx.', cents(100, 400, 500, 700, 800, 1000, 1200)],
  ['maqam-sikah', 'Arab maqam Sikah, approx.', cents(150, 350, 500, 700, 850, 1050, 1200)],
  ['maqam-nahawand', 'Arab maqam Nahawand, approx.', cents(200, 300, 500, 700, 800, 1100, 1200)],
  ['maqam-kurd', 'Arab maqam Kurd, approx.', cents(100, 300, 500, 700, 800, 1000, 1200)],
  ['maqam-ajam', 'Arab maqam Ajam = Persian dastgah Mahur', cents(200, 400, 500, 700, 900, 1100, 1200)],
  ['maqam-huzam', 'Arab maqam Huzam, approx.', cents(150, 350, 450, 750, 850, 1050, 1200)],
  /* Turkey: makamlar in 53 commas to the octave (Arel-Ezgi-Uzdilek) */
  ['makam-rast', 'Turkish makam Rast (53 commas)', commas([9, 17, 22, 31, 40, 44, 53])],
  ['makam-ussak', 'Turkish makam Ussak (53 commas)', commas([8, 13, 22, 31, 35, 44, 53])],
  ['makam-hicaz', 'Turkish makam Hicaz (53 commas)', commas([5, 17, 22, 31, 35, 44, 53])],
  ['makam-huseyni', 'Turkish makam Huseyni (53 commas)', commas([8, 13, 22, 31, 39, 44, 53])],
  ['makam-segah', 'Turkish makam Segah (53 commas)', commas([5, 14, 22, 31, 36, 45, 53])],
  ['makam-nihavend', 'Turkish makam Nihavend (53 commas)', commas([9, 13, 22, 31, 35, 48, 53])],
  ['makam-karcigar', 'Turkish makam Karcigar (53 commas)', commas([8, 13, 22, 27, 39, 44, 53])],
  /* Persia: dastgah-ha, Vaziri's quarter tones (Shur: see maqam Bayati; Mahur: maqam Ajam) */
  ['dastgah-homayun', 'Persian dastgah Homayun, approx.', cents(150, 400, 500, 700, 800, 1000, 1200)],
  ['dastgah-segah', 'Persian dastgah Segah, approx.', cents(150, 350, 500, 650, 850, 1000, 1200)],
  ['dastgah-chahrgh', 'Persian dastgah Chahargah, approx.', cents(150, 400, 500, 700, 850, 1100, 1200)],
  /* Indonesia: gamelan, measured sets (they differ from gamelan to gamelan) */
  ['slendro-meas', 'Javanese slendro, a measured gamelan, approx.', cents(231, 474, 717, 955, 1200)],
  ['pelog-meas', 'Javanese pelog, a measured gamelan, approx.', cents(136, 290, 549, 676, 794, 938, 1200)],
  ['pelog-bem', 'Javanese pelog bem (1 2 3 5 6), approx.', cents(120, 270, 670, 785, 1200)],
  ['pelog-barang', 'Javanese pelog barang (2 3 5 6 7), approx.', cents(150, 550, 665, 830, 1200)],
  ['bali-gong-kebyar', 'Balinese gong kebyar, pelog selisir, approx.', cents(129, 263, 676, 791, 1200)],
  /* South-East and East Asia */
  ['thai-7edo', 'Thailand: 7 equal steps', edo(7)],
  ['hirajoshi', 'Japan: hirajoshi (just)', '9/8\n6/5\n3/2\n8/5\n2/1'],
  ['in-miyakobushi', 'Japan: in / miyako-bushi (just)', '16/15\n4/3\n3/2\n8/5\n2/1'],
  ['yo', 'Japan: yo (just)', '9/8\n4/3\n3/2\n27/16\n2/1'],
  ['iwato', 'Japan: iwato (just)', '16/15\n4/3\n64/45\n16/9\n2/1'],
  ['ryukyu', 'Okinawa: ryukyu (just)', '5/4\n4/3\n3/2\n15/8\n2/1'],
  ['china-gong', 'China: gong mode, Pythagorean pentatonic', '9/8\n81/64\n3/2\n27/16\n2/1'],
  ['china-zhi', 'China: zhi mode, Pythagorean pentatonic', '9/8\n4/3\n3/2\n16/9\n2/1'],
  /* Africa */
  ['tizita', 'Ethiopia: tizita major, approx.', cents(200, 400, 700, 900, 1200)],
  ['tizita-minor', 'Ethiopia: tizita minor, approx.', cents(200, 300, 700, 800, 1200)],
  ['bati', 'Ethiopia: bati major, approx.', cents(400, 500, 700, 1100, 1200)],
  ['ambassel', 'Ethiopia: ambassel, approx.', cents(100, 500, 700, 800, 1200)],
  ['anchihoye', 'Ethiopia: anchihoye, approx.', cents(100, 500, 600, 900, 1200)],
  ['mbira-shona', 'Zimbabwe: a Shona mbira, approx. 7 equal', cents(171, 343, 514, 686, 857, 1029, 1200)],
  /* ancient Greece and Byzantium */
  ['archytas-diat', 'Greek: Archytas diatonic (Dorian)', '28/27\n32/27\n4/3\n3/2\n14/9\n16/9\n2/1'],
  ['archytas-chrom', 'Greek: Archytas chromatic', '28/27\n9/8\n4/3\n3/2\n14/9\n27/16\n2/1'],
  ['archytas-enh', 'Greek: Archytas enharmonic', '28/27\n16/15\n4/3\n3/2\n14/9\n8/5\n2/1'],
  ['byzantine-diat', 'Byzantine diatonic (72 moria: 12 10 8)', moria([12, 22, 30, 42, 54, 64, 72])],
  ['byzantine-hard', 'Byzantine hard chromatic (72 moria)', moria([6, 26, 30, 42, 48, 68, 72])],
  /* Europe: historical temperaments */
  ['werckmeister3', 'Werckmeister III (1691)', cents(90.225, 192.18, 294.135, 390.225, 498.045, 588.27, 696.09, 792.18, 888.27, 996.09, 1092.18, 1200)],
  ['kirnberger3', 'Kirnberger III', cents(90.225, 193.157, 294.135, 386.314, 498.045, 590.224, 696.578, 792.18, 889.735, 996.09, 1088.269, 1200)],
  ['young-1799', 'Thomas Young (1799)', cents(93.9, 195.8, 297.8, 391.7, 499.9, 591.9, 697.9, 795.8, 893.8, 997.8, 1091.8, 1200)],
  ['vallotti', 'Vallotti', cents(94.135, 196.09, 298.045, 392.18, 501.955, 592.18, 698.045, 796.09, 894.135, 999.955, 1090.225, 1200)],
  /* the twentieth century */
  ['partch-43', 'Harry Partch: 43 tones', '81/80\n33/32\n21/20\n16/15\n12/11\n11/10\n10/9\n9/8\n8/7\n7/6\n32/27\n6/5\n11/9\n5/4\n14/11\n9/7\n21/16\n4/3\n27/20\n11/8\n7/5\n10/7\n16/11\n40/27\n3/2\n32/21\n14/9\n11/7\n8/5\n18/11\n5/3\n27/16\n12/7\n7/4\n16/9\n9/5\n20/11\n11/6\n15/8\n40/21\n64/33\n160/81\n2/1'],
  ['carlos-alpha', 'Wendy Carlos alpha (78 cents, no octave)', '77.965'],
  ['carlos-beta', 'Wendy Carlos beta (63.8 cents, no octave)', '63.833'],
  ['carlos-gamma', 'Wendy Carlos gamma (35.1 cents, no octave)', '35.099'],
['10-edo', '10 equal steps to the octave', edo(10)],
  ['15-edo', '15 equal steps to the octave', edo(15)], ['16-edo', '16 equal steps to the octave', edo(16)],
  ['23-edo', '23 equal steps to the octave', edo(23)], ['72-edo', '72 equal steps to the octave', edo(72)],
];

const ALL = BASE.concat(WORLD).map(([name, desc, body]) => {
  if (name.length > 16) throw new Error('scale name over 16 characters: ' + name);
  return { name: name + '.scl', desc, text: `! ${name}.scl\n!\n${desc}\n ${body.split('\n').length}\n!\n${body}\n` };
});
module.exports = { BASE_COUNT: BASE.length, SCALES: ALL };

#!/usr/bin/env python3
"""Aliasing of the oscillators: energy outside the harmonics (DC excluded), relative to the total.
python3 alias_check.py <render_raw binary>"""
import subprocess, sys, numpy as np
FS = 44100
def metric(x, f0, band, win=8):
    x = x[:65536]
    spec = np.abs(np.fft.rfft(x * np.blackman(len(x)))) ** 2
    fr = np.fft.rfftfreq(len(x), 1 / FS)
    near = fr < 20
    for h in range(1, int(FS / 2 / f0) + 1):
        near |= np.abs(fr - h * f0) < win
    m = fr < band
    return 10 * np.log10(spec[~near & m].sum() / spec[m & (fr >= 20)].sum())
worst = {}
for name, wave in [('sine', 0), ('triangle', 5461), ('saw', 10923), ('pulse 50%', 16383), ('tri/saw', 8192)]:
    row = []
    for deg in (0, 24, 36):
        raw = subprocess.run([sys.argv[1], str(wave), str(deg), '12', '3'], capture_output=True, check=True).stdout
        x = np.frombuffer(raw, np.int16).astype(float)[FS // 2:]
        f0 = 261.63 * 2 ** (deg / 12)
        full, low = metric(x, f0, 22050), metric(x, f0, 5000)
        row.append(f'{f0:5.0f} Hz {full:6.1f} ({low:6.1f})')
        worst[name] = max(worst.get(name, -999), low)
    print(f'{name:10s}', ' | '.join(row))
print('dB outside the harmonics, whole band (below 5 kHz)')
sys.exit(0 if all(v < -45 for v in worst.values()) else 1)

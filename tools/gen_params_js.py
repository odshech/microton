#!/usr/bin/env python3
"""params.def → site/params.json: the same parameter list, ids and ranges for the browser."""
import json, os, re
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '..', 'engine', 'params.def')).read()
src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
found = re.findall(r'P\(\s*\w+\s*,\s*"(\w+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([CBE])\s*\)', src)
params = [{'id': i, 'key': k, 'def': int(d), 'max': int(mx), 'kind': kind} for i, (k, d, mx, kind) in enumerate(found)]
out = os.path.join(here, '..', 'site', 'params.json')
with open(out, 'w') as fh:
    json.dump(params, fh, separators=(',', ':'))
print('params.json:', len(params), 'parameters')

# the drums (drums.def, with the kit from drums.c) and the effects (fx.def)
dsrc = re.sub(r'/\*.*?\*/', '', open(os.path.join(here, '..', 'engine', 'drums.def')).read(), flags=re.S)
dps = re.findall(r'D\(\s*\w+\s*,\s*"(\w+)"\s*,\s*(\d+)\s*,\s*([CBE])\s*\)', dsrc)
csrc = open(os.path.join(here, '..', 'engine', 'drums.c')).read()
kit_txt = csrc[csrc.index('DKIT[ENG_DRUMS][DP_COUNT] = {'):]
kit_txt = re.sub(r'/\*.*?\*/', '', kit_txt[:kit_txt.index('};')], flags=re.S)
kit = [[int(v) for v in re.findall(r'\d+', row)] for row in re.findall(r'\{([^{}]*)\}', kit_txt)]
assert len(kit) == 8 and all(len(r) == len(dps) for r in kit), 'the kit does not match drums.def'
drums = {'params': [{'id': i, 'key': k, 'max': int(mx), 'kind': kind} for i, (k, mx, kind) in enumerate(dps)], 'kit': kit}
with open(os.path.join(here, '..', 'site', 'drums.json'), 'w') as fh:
    json.dump(drums, fh, separators=(',', ':'))
fsrc = re.sub(r'/\*.*?\*/', '', open(os.path.join(here, '..', 'engine', 'fx.def')).read(), flags=re.S)
fxs = re.findall(r'F\(\s*\w+\s*,\s*"(\w+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([CBE])\s*\)', fsrc)
with open(os.path.join(here, '..', 'site', 'fx.json'), 'w') as fh:
    json.dump([{'id': i, 'key': k, 'def': int(d), 'max': int(mx), 'kind': kind} for i, (k, d, mx, kind) in enumerate(fxs)], fh, separators=(',', ':'))
print('drums.json:', len(dps), 'parameters x 8 drums · fx.json:', len(fxs), 'parameters')

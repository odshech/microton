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

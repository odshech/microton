#!/usr/bin/env python3
"""Assembles the site into one file that runs anywhere Chrome does: double-clicked, on a web server, or as an artifact.
dist/microton.html = a whole document · dist/microton.artifact.html = the same body for an artifact."""
import base64, json, os
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.join(here, '..')
rd = lambda *p: open(os.path.join(root, *p), encoding='utf-8').read()
page = rd('site', 'src', 'page.html')
codec = rd('tools', 'scl-codec.js')
params = rd('site', 'params.json')
drums = rd('site', 'drums.json')
fx = rd('site', 'fx.json')
wasm = base64.b64encode(open(os.path.join(root, 'site', 'engine.wasm'), 'rb').read()).decode()
worklet = rd('site', 'src', 'worklet.js')
app = rd('site', 'src', 'app.js')
scripts = '\n'.join([
    '<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>',
    '<script>\n' + codec + '\n</script>',
    '<script>\nwindow.FM1_PARAMS = ' + params + ';\nwindow.FM1_DRUMS = ' + drums + ';\nwindow.FM1_FX = ' + fx + ';\nwindow.FM1_WASM_B64 = "' + wasm + '";\nwindow.FM1_WORKLET = ' + json.dumps(worklet) + ';\n</script>',
    '<script>\n' + app + '\n</script>',
])
body = page + '\n' + scripts + '\n'
os.makedirs(os.path.join(root, 'dist'), exist_ok=True)
open(os.path.join(root, 'dist', 'microton.artifact.html'), 'w', encoding='utf-8').write(body)
doc = ('<!doctype html>\n<html lang="en" dir="ltr">\n<head>\n<meta charset="utf-8">\n'
       '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n</head>\n<body>\n'
       + body + '</body>\n</html>\n')
open(os.path.join(root, 'dist', 'microton.html'), 'w', encoding='utf-8').write(doc)
print('dist/microton.html:', len(doc.encode()), 'bytes')

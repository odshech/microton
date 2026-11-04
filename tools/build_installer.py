#!/usr/bin/env python3
"""dist/microton-install.html: the installer in one file (Felucca's fm1pkg.js + fm1ota.js inlined, unchanged but for `export`)."""
import os, re
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
rd = lambda *p: open(os.path.join(root, *p), encoding='utf-8').read()
noexp = lambda s: re.sub(r'^export\s+', '', s, flags=re.M)
lib = noexp(rd('site', 'vendor', 'fm1pkg.js')) + '\n' + noexp(rd('site', 'vendor', 'fm1ota.js'))
body = rd('site', 'src', 'installer.html') + '\n<script>\n' + lib + '\n</script>\n<script>\n' + rd('site', 'src', 'installer.js') + '\n</script>\n'
doc = ('<!doctype html>\n<html lang="he" dir="rtl">\n<head>\n<meta charset="utf-8">\n'
       '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n</head>\n<body>\n' + body + '</body>\n</html>\n')
os.makedirs(os.path.join(root, 'dist'), exist_ok=True)
open(os.path.join(root, 'dist', 'microton-install.html'), 'w', encoding='utf-8').write(doc)
print('dist/microton-install.html:', len(doc.encode()), 'bytes')

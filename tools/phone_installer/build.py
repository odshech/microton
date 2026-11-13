#!/usr/bin/env python3
"""The phone installer in one HTML file, with the firmware inside it:
    python3 tools/phone_installer/build.py build/arcturus-s6.fwsc dist/install.html
fm1pkg.js and fm1ota.js are Felucca's (GPL-3.0, Leo Kuroshita), inlined unchanged but for `export`."""
import base64, os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
rd = lambda name: open(os.path.join(here, name), encoding='utf-8').read()
noexp = lambda s: re.sub(r'^export\s+', '', s, flags=re.M)

fwsc, out = sys.argv[1], sys.argv[2]
data = open(fwsc, 'rb').read()
embed = 'const EMBED = "%s";\nconst EMBED_NAME = "%s";\n' % (base64.b64encode(data).decode(), os.path.basename(fwsc))
lib = noexp(rd('fm1pkg.js')) + '\n' + noexp(rd('fm1ota.js'))
doc = ('<!doctype html>\n<html lang="he" dir="rtl">\n<head>\n<meta charset="utf-8">\n'
       '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n'
       + rd('head.part') + '</head>\n<body>\n' + rd('body.part')
       + '<script>\n' + embed + lib + '\n</script>\n<script>\n' + rd('page.js') + '\n</script>\n</body>\n</html>\n')
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
open(out, 'w', encoding='utf-8').write(doc)
print(out, len(doc.encode()), 'bytes')

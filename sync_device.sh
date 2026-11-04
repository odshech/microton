#!/bin/sh
# The engine has one source: engine/. This copies it into the FM-1 firmware tree (device/firmware/src/micro/).
set -e
cd "$(dirname "$0")"
python3 tools/gen_tables.py
for f in engine.c engine.h tuning.c tuning.h seq.c seq.h params.def tables.h; do cp engine/$f device/firmware/src/micro/$f; done
echo "engine copied into device/firmware/src/micro"

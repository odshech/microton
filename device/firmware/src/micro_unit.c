/* SPDX-License-Identifier: GPL-3.0-only */
/* The microtonal engine as its own translation unit: tools/build.py compiles it with -O2 (the audio's hot loops),
 * while the app and the platform stay at -Os (Dinghy: -O2 on usb.c broke USB-MIDI IN). The host emulator includes
 * it from dinghy.c instead. Integers only, no libgcc: the build checks the link. */
#include "micro/config.h"
#include "micro/engine.c"
#include "micro/tuning.c"
#include "micro/seq.c"

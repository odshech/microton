# Microton — microtonal firmware for the M-VAVE FM-1

The engine is portable C with integers only (the FM-1 has no FPU and no 64-bit division). The same source builds for the
browser now and for the FM-1 later.

- `engine/` the synth: tuning from scale tables, 8-voice virtual analog voice, parameters (`params.def`)
- `tools/` table generator, Scala codec (`scl-codec.js`), site builder
- `site/src/` the page, the AudioWorklet, the UI code
- `tests/` engine tests, aliasing check, UI smoke test
- `dist/microton.html` the whole lab in one file: open it in Chrome (double-click works)

Build: `sh build_web.sh` (clang with the wasm32 target, wasm-ld, python3). Tests:
`cc -O2 -Iengine tests/test_engine.c engine/engine.c -lm && ./a.out`, `python3 tests/alias_check.py <render_raw>`,
`node tests/ui_smoke.js` (and `WORKLET=1 node tests/ui_smoke.js`), `cc -O2 -Iengine tests/test_seq.c engine/*.c -lm`.

The engine has three parts (sounds), one per melodic track; `engine/seq.c` is the sequencer, run from inside the audio render.

## The FM-1 firmware (device/)

`device/` is Dinghy (https://github.com/zvenson/dinghy, GPL-3.0) with its app replaced: `device/firmware/src/dinghy.c` is
MICROTON, a test firmware that plays the engine and shows the battery and the audio engine's CPU load. The engine is
copied in by `sh sync_device.sh` (one source: `engine/`). Identity `FM-1_950`.

Build on GitHub: push this repository; `.github/workflows/firmware.yml` downloads the JieLi toolchain, builds, runs the
tests and the firmware on Dinghy's host emulator, and keeps `microton.fwsc` as the run's artifact.

Build on Linux (x86-64, or WSL on Windows):

    sh device/tools/get_toolchain.sh
    git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
    pip3 install Pillow
    sh sync_device.sh && (cd device && sh build.sh)     # → device/build/dinghy.fwsc

Install: `dist/microton-install.html` in Chrome on a computer, the FM-1 on USB (Felucca's update protocol, GPL-3.0).
Back to the stock firmware: install M-VAVE's FM-1.fwsc the same way. Rescue: hold OCT- while switching on.

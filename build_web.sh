#!/bin/sh
# Builds the engine for the browser and checks it against the FM-1's rules: no floating point, no 64-bit division.
set -e
cd "$(dirname "$0")"
python3 tools/gen_tables.py
mkdir -p site
CFLAGS="--target=wasm32 -std=c99 -O2 -nostdlib -ffreestanding -fno-builtin -Wall -Wextra -Iengine"
SRC="engine/engine.c engine/tuning.c engine/seq.c web/wasm_glue.c"

# the rules, read from the compiler's own assembly (a disassembler can mistake data for instructions)
for f in $SRC; do
  clang $CFLAGS -S -o build_check.s "$f"
  if grep -E -q '\b(f32|f64)\.' build_check.s; then echo "FAIL: floating point in $f"; grep -E '\b(f32|f64)\.' build_check.s | head; rm build_check.s; exit 1; fi
  if grep -E -q '\bi64\.(div|rem)_' build_check.s; then echo "FAIL: 64-bit division in $f"; grep -E '\bi64\.(div|rem)_' build_check.s | head; rm build_check.s; exit 1; fi
done
rm -f build_check.s

clang $CFLAGS \
  -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=65536 \
  -Wl,--export=eng_init -Wl,--export=eng_note_on -Wl,--export=eng_note_off -Wl,--export=eng_all_off \
  -Wl,--export=eng_set_param -Wl,--export=eng_get_param -Wl,--export=eng_bend -Wl,--export=eng_modwheel \
  -Wl,--export=eng_aftertouch -Wl,--export=eng_set_ref_chz -Wl,--export=eng_scale_degrees \
  -Wl,--export=eng_voices_active -Wl,--export=w_out -Wl,--export=w_lib -Wl,--export=w_lib_cap \
  -Wl,--export=w_frames -Wl,--export=w_param_count -Wl,--export=w_render -Wl,--export=w_scale \
  -Wl,--export=eng_set_ref_degree -Wl,--export=eng_scale_c16 \
  -Wl,--export=tun_edo -Wl,--export=tun_rotate -Wl,--export=tun_fine -Wl,--export=tun_clear_fine -Wl,--export=tun_ref \
  -Wl,--export=tun_degrees -Wl,--export=tun_mode -Wl,--export=tun_c16 -Wl,--export=tun_base_c16 -Wl,--export=tun_fine_of \
  -Wl,--export=tun_ref_degree -Wl,--export=tun_ref_chz \
  -Wl,--export=eng_part_off -Wl,--export=w_init -Wl,--export=w_midi -Wl,--export=seq_play -Wl,--export=seq_stop -Wl,--export=seq_continue -Wl,--export=seq_playing -Wl,--export=seq_set_tempo -Wl,--export=seq_tempo -Wl,--export=seq_queue_pattern -Wl,--export=seq_pattern_playing -Wl,--export=seq_pattern_queued -Wl,--export=seq_song_mode -Wl,--export=seq_song_on -Wl,--export=seq_song_set -Wl,--export=seq_song_get -Wl,--export=seq_song_length -Wl,--export=seq_song_len -Wl,--export=seq_song_row -Wl,--export=seq_track_pos -Wl,--export=seq_scale_request -Wl,--export=seq_step_get -Wl,--export=seq_step_set -Wl,--export=seq_track_get -Wl,--export=seq_track_set -Wl,--export=seq_pattern_get -Wl,--export=seq_pattern_set -Wl,--export=seq_step_lock -Wl,--export=seq_step_unlock -Wl,--export=seq_op -Wl,--export=seq_project_bytes -Wl,--export=seq_project_size -Wl,--export=eng_param_max \
  -Wl,--export=eng_drum_hit -Wl,--export=eng_drum_set -Wl,--export=eng_drum_get -Wl,--export=eng_drum_param_max -Wl,--export=eng_drum_param_def \
  -Wl,--export=eng_fx_set -Wl,--export=eng_fx_get -Wl,--export=eng_fx_param_max -Wl,--export=eng_fx_param_def -Wl,--export=eng_fx_tempo -Wl,--export=eng_set_rate \
  -o site/engine.wasm $SRC
python3 tools/gen_params_js.py
python3 tools/build_site.py
echo "engine.wasm: $(wc -c < site/engine.wasm) bytes, integer only, no 64-bit division"

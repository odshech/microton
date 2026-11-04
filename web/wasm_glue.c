/* wasm_glue.c — the browser's side of the engine: buffers the AudioWorklet reads, and the two C library
 * functions the compiler may call. Built with clang --target=wasm32, no libc. */
#include "engine.h"
#include "tuning.h"
#include "seq.h"

#define FRAMES 128u                           /* one AudioWorklet render quantum */
static int32_t out[2 * FRAMES];
static uint8_t lib[640 * 1024];               /* the scale library, copied in by the page */

void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }

int32_t *w_out(void) { return out; }
uint8_t *w_lib(void) { return lib; }
uint32_t w_lib_cap(void) { return sizeof lib; }
uint32_t w_frames(void) { return FRAMES; }
uint32_t w_param_count(void) { return P_COUNT; }

void w_init(void) { eng_init(); seq_init(); }
/* the next MIDI message of the external and drum tracks: status << 16 | d1 << 8 | d2, or -1 */
int32_t w_midi(void) { uint8_t s, a, b; return seq_midi_get(&s, &a, &b) ? (int32_t)((uint32_t)s << 16 | (uint32_t)a << 8 | b) : -1; }

void w_render(void)
{
    uint32_t i;
    for (i = 0; i < 2u * FRAMES; i++) out[i] = 0;
    eng_render(out, FRAMES);
}
int w_scale(uint32_t len, uint32_t index) { return tun_from_lib(lib, len, index); }

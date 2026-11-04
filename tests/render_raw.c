/* render_raw <o1_wave raw> <degree> <edo> <seconds> > out.raw : int16 mono, open filter, one held note */
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    int32_t c[255], buf[512];
    int wave = atoi(argv[1]), deg = atoi(argv[2]), n = atoi(argv[3]), frames = (int)(atof(argv[4]) * 44100) / 256 * 256;
    eng_init();
    for (int k = 0; k < n; k++) c[k] = 19200 * (k + 1) / n;
    eng_set_scale_c16((uint32_t)n, c);
    eng_set_param(0, P_O1_WAVE, (uint32_t)wave); eng_set_param(0, P_O2_LEVEL, 0); eng_set_param(0, P_DRIVE, 0);
    eng_set_param(0, P_CUTOFF, 16383); eng_set_param(0, P_RESO, 0); eng_set_param(0, P_FLT_KEYTRK, 0); eng_set_param(0, P_FLT_ENV, 8192);
    eng_set_param(0, P_E1_A, 0); eng_set_param(0, P_E1_S, 16383); eng_set_param(0, P_VARIANCE, 0); eng_set_param(0, P_OUT_SAT, 0);
    eng_set_param(0, P_M1_SRC, 0);
    eng_note_on(0, deg, 127);
    for (int f = 0; f < frames; f += 256) {
        memset(buf, 0, sizeof buf); eng_render(buf, 256);
        for (int i = 0; i < 256; i++) { int v = buf[2 * i]; short s = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); fwrite(&s, 2, 1, stdout); }
    }
    return 0;
}

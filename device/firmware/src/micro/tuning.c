/* tuning.c — see tuning.h */
#include "tuning.h"
#include "engine.h"

static struct {
    uint32_t n;
    int32_t base[ENG_MAX_DEGREES];      /* as loaded */
    int32_t mode;
    int16_t fine[ENG_MAX_DEGREES];      /* by played degree; [0] unused: the root and the period stay put */
    int32_t played[ENG_MAX_DEGREES];
    int32_t ref_deg;
    uint32_t ref_chz;
} T = {12, {1600, 3200, 4800, 6400, 8000, 9600, 11200, 12800, 14400, 16000, 17600, 19200}, 0, {0}, {0}, 0, 26163};

static int32_t base_at(int32_t d)                                    /* c16 of base degree d, any d >= 0 */
{
    int32_t n = (int32_t)T.n, k = d / n, r = d - k * n;
    return k * T.base[n - 1] + (r ? T.base[r - 1] : 0);
}

static void apply(void)
{
    uint32_t k;
    int32_t m = T.mode, r0 = base_at(m), prev = 0;
    for (k = 0; k < T.n; k++) {
        int32_t c = base_at(m + (int32_t)k + 1) - r0;
        if (k + 1u < T.n) c += T.fine[k + 1u];
        if (c <= prev) c = prev + 1;                                 /* offsets never reorder the degrees */
        T.played[k] = c;
        prev = c;
    }
    if (T.played[T.n - 1] != base_at(m + (int32_t)T.n) - r0) T.played[T.n - 1] = base_at(m + (int32_t)T.n) - r0;
    eng_set_scale_c16(T.n, T.played);
    eng_set_ref_degree(T.ref_deg, T.ref_chz);
}

int tun_set_base(uint32_t n, const int32_t *c16)
{
    uint32_t k;
    if (n < 1u || n > ENG_MAX_DEGREES || c16[n - 1] <= 0) return -1;
    for (k = 0; k < n; k++) if (c16[k] < 0) return -1;
    for (k = 0; k < n; k++) { T.base[k] = c16[k]; T.fine[k] = 0; }
    T.n = n;
    T.mode = 0;
    apply();
    return 0;
}

int tun_from_lib(const uint8_t *lib, uint32_t len, uint32_t index)
{
    static int32_t c[ENG_MAX_DEGREES];
    uint32_t n;
    if (eng_scale_from_lib(lib, len, index)) return -1;              /* the engine decodes and checks it */
    n = eng_scale_degrees();
    {
        uint32_t k;
        for (k = 0; k < n; k++) c[k] = eng_scale_c16(k);
    }
    return tun_set_base(n, c);
}

int tun_edo(uint32_t n, int32_t period_c16)
{
    static int32_t c[ENG_MAX_DEGREES];
    uint32_t k;
    if (n < 1u || n > ENG_MAX_DEGREES || period_c16 <= 0) return -1;
    for (k = 0; k < n; k++) c[k] = (int32_t)(((uint32_t)period_c16 * (k + 1u)) / n);
    return tun_set_base(n, c);
}

void tun_rotate(int32_t mode)
{
    int32_t n = (int32_t)T.n;
    uint32_t k;
    mode %= n;
    if (mode < 0) mode += n;
    T.mode = mode;
    for (k = 0; k < T.n; k++) T.fine[k] = 0;
    apply();
}

void tun_fine(uint32_t degree, int32_t c16)
{
    if (degree < 1u || degree >= T.n) return;
    if (c16 > 800) c16 = 800;
    if (c16 < -800) c16 = -800;
    T.fine[degree] = (int16_t)c16;
    apply();
}
void tun_clear_fine(void) { uint32_t k; for (k = 0; k < T.n; k++) T.fine[k] = 0; apply(); }

void tun_ref(int32_t degree, uint32_t chz)
{
    T.ref_deg = degree;
    T.ref_chz = chz;
    eng_set_ref_degree(degree, chz);
}

uint32_t tun_degrees(void) { return T.n; }
int32_t tun_mode(void) { return T.mode; }
int32_t tun_c16(uint32_t k) { return k < T.n ? T.played[k] : 0; }
int32_t tun_base_c16(uint32_t k) { return k < T.n ? T.base[k] : 0; }
int32_t tun_fine_of(uint32_t d) { return d < T.n ? T.fine[d] : 0; }
int32_t tun_ref_degree(void) { return T.ref_deg; }
uint32_t tun_ref_chz(void) { return T.ref_chz; }

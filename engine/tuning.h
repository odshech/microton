/* tuning.h — the scale the instrument plays: a base scale (from the library or the equal-division generator),
 * a mode (rotation), a fine offset per degree, and which degree carries the reference frequency.
 * Main loop only. tun_apply() hands the result to the engine. Integers only. */
#pragma once
#include <stdint.h>

int tun_from_lib(const uint8_t *lib, uint32_t len, uint32_t index);   /* 0 = loaded; mode and offsets reset */
int tun_set_base(uint32_t n, const int32_t *c16);                     /* cents x 16, c16[n-1] = the period */
int tun_edo(uint32_t n, int32_t period_c16);                          /* n equal steps of the period */
void tun_rotate(int32_t mode);                                         /* degree of the base that becomes 0 */
void tun_fine(uint32_t degree, int32_t c16);                           /* degree 1 .. n-1, ±800 (±50 cents) */
void tun_clear_fine(void);
void tun_ref(int32_t degree, uint32_t centihertz);                     /* this degree sounds at this frequency */

uint32_t tun_degrees(void);
int32_t tun_mode(void);
int32_t tun_c16(uint32_t k);                                           /* the played table: k = 0 .. n-1 */
int32_t tun_base_c16(uint32_t k);
int32_t tun_fine_of(uint32_t degree);
int32_t tun_ref_degree(void);
uint32_t tun_ref_chz(void);

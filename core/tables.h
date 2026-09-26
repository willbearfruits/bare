/* Generated fixed-point lookup tables (tools/gen_tables.py) — regenerate, do not edit. */
#pragma once
#include <stdint.h>

/* filter cutoff index 0..127 -> f coefficient (Q15), fc = 30*2^(i/14) Hz at 48 kHz */
extern const uint16_t svf_f_q15[128];
/* -ln u at 256 even steps of u (Q8): exponential waiting times, as a Poisson stream has */
extern const uint16_t neglog_q8[256];
/* GENDY's six step distributions, 256 values each (Q15, +-1): LINEAR CAUCHY LOGIST HYPCOS ARCSIN EXPON */
extern const int16_t gendy_dist_q15[1536];
/* full-cycle sine, 256 entries, Q15 */
extern const int16_t sine_q15[256];
/* full-cycle sine, 8192 entries, Q15 — oscillators, FFT twiddles, windows */
extern const int16_t sine_q15_8192[8192];

/* gow_edge_native_73e0.c -- native replacement for the hot region of the EDGE job module's (d935, lift
 * spu/jobmod_d93503647dd8_e6d98) function 73E0: the outer loop at loc_000079B0 with its two inner loops at
 * loc_00007BB4 and loc_00007ED8, up to loc_000081C0. The d935 lift is built through gen_edge_native_lift.py,
 * which puts a switch before loc_000079B0; GOW_EDGE_73E0 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native.c (-O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1,
 * SPU_FP_CHECK_HOOK=0): the float helpers are the same inline helpers the lift uses. Runs inside
 * spu_run_with_halt, i.e. with MXCSR rounding toward zero.
 *
 * Clean-room: written from our own lift of the module and our own captures.
 *
 * GOW_EDGE_73E0 (read once):
 *   0/unset  lifted region
 *   1        native v3 (both inner loops 8-wide, one lane check per iteration); with GOW_EDGE_73E0_V2=1:
 *            v2 (8-wide, a lane check per float op)
 *   2        native v1 (the region statement for statement, registers in locals)
 *   3        check v3 (or v2 with GOW_EDGE_73E0_V2=1): run it on a copy of the state, then the lifted
 *            region, and compare all GPRs and the whole LS at loc_81C0
 *   4        check v1, the same way
 * GOW_EDGE_73E0_FAULT=1|2 (negative test, first native run only): flip bit 0 of the last LS byte the
 * region's last outer iteration stored / bit 0 of r60.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include "spu_context.h"
#include "spu_helpers.h"

/* ---- LS access: big-endian words, 16-byte aligned, wrapped to the 256 KB store (as
 * spu_ls_read128/write128 do). One byte shuffle swaps the four words. */
static inline __m128i g73_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 g73_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = g73_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void g73_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g73_bswap32x4(x));
}
#define LD(a)    g73_ld(ls, (a))
#define ST(a, v) g73_st(ls, (a), (v))

/* the lifted region: GOW_73_REGS/LOAD/STORE and the segments. Game code, so it is not in the repo:
 * gen_edge_73e0_body.py writes it from the d935 lift at build time. */
#include "gow_edge_73e0_body.h"

/* ---- 8-lane building blocks: two SPU quadwords per vector, the region's vertices 0-3 (the
 * lifted code's "A" registers) in the low 128 bits, vertices 4-7 ("B") in the high ---- */
typedef __m256i v8;

static inline v8 g73_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline u128 g73_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 g73_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline v8 g73_bc(u128 a) { __m128i x; memcpy(&x, &a, 16); return _mm256_broadcastsi128_si256(x); }
static inline v8 g73_ld2(const uint8_t* ls, uint32_t lo, uint32_t hi)
{
    const __m128i a = _mm_loadu_si128((const __m128i*)(ls + (lo & (SPU_LS_MASK & ~0xFu))));
    const __m128i b = _mm_loadu_si128((const __m128i*)(ls + (hi & (SPU_LS_MASK & ~0xFu))));
    return _mm256_shuffle_epi8(_mm256_set_m128i(b, a),
                               _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline void g73_stlo(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g73_bswap32x4(_mm256_castsi256_si128(v))); }
static inline void g73_sthi(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g73_bswap32x4(_mm256_extracti128_si256(v, 1))); }

/* integer ops, exact by construction */
static inline v8 g73_and(v8 a, v8 b)  { return _mm256_and_si256(a, b); }
static inline v8 g73_andc(v8 a, v8 b) { return _mm256_andnot_si256(b, a); }                 /* a & ~b */
static inline v8 g73_xor(v8 a, v8 b)  { return _mm256_xor_si256(a, b); }
static inline v8 g73_selb(v8 a, v8 b, v8 m) { return _mm256_or_si256(_mm256_andnot_si256(m, a), _mm256_and_si256(m, b)); }
static inline v8 g73_cgt0(v8 a) { return _mm256_cmpgt_epi32(a, _mm256_setzero_si256()); }  /* cgti(a, 0) */
static inline v8 g73_max0(v8 a) { return _mm256_and_si256(a, g73_cgt0(a)); }               /* and(a, cgti(a, 0)) */
/* fcgt: the toolkit's key compare (spu__sse_key: magnitude with denormals as 0, negated for negatives) */
static inline v8 g73_key(v8 x)
{
    const v8 m = _mm256_and_si256(x, _mm256_set1_epi32(0x7FFFFFFF));
    const v8 den = _mm256_cmpeq_epi32(_mm256_and_si256(x, _mm256_set1_epi32(0x7F800000)), _mm256_setzero_si256());
    const v8 mag = _mm256_andnot_si256(den, m);
    const v8 neg = _mm256_srai_epi32(x, 31);
    return _mm256_sub_epi32(_mm256_xor_si256(mag, neg), neg);
}
static inline v8 g73_fcgt(v8 a, v8 b) { return _mm256_cmpgt_epi32(g73_key(a), g73_key(b)); }
/* frsqest / fi: spu_helpers.h's AVX2 forms (integer work on the bit fields), eight lanes */
static inline v8 g73_frsqest(v8 x)
{
    const v8 exp = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(x, 18), _mm256_set1_epi32(0x3F));
    const v8 row = _mm256_srli_epi32(idx, 3);
    v8 f = _mm256_permutevar8x32_epi32(_mm256_loadu_si256((const __m256i*)spu_frsqest_fraction_lut), idx);
    for (int k = 1; k < 8; k++)
        f = _mm256_blendv_epi8(f, _mm256_permutevar8x32_epi32(_mm256_loadu_si256((const __m256i*)(spu_frsqest_fraction_lut + 8 * k)), idx),
                               _mm256_cmpeq_epi32(row, _mm256_set1_epi32(k)));
    f = _mm256_and_si256(f, _mm256_set1_epi32(0x7FFFFF));
    v8 rexp = _mm256_sub_epi32(_mm256_set1_epi32(190), _mm256_srli_epi32(_mm256_add_epi32(exp, _mm256_set1_epi32(1)), 1));
    rexp = _mm256_blendv_epi8(rexp, _mm256_set1_epi32(255), _mm256_cmpeq_epi32(exp, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_slli_epi32(rexp, 23), f);
}
static inline v8 g73_fi(v8 a, v8 est)
{
    const v8 y = _mm256_and_si256(a, _mm256_set1_epi32(0x7FFFF));
    const v8 exp = _mm256_and_si256(_mm256_srli_epi32(est, 23), _mm256_set1_epi32(0xFF));
    const v8 b19 = _mm256_slli_epi32(_mm256_and_si256(_mm256_srli_epi32(est, 10), _mm256_set1_epi32(0x1FFF)), 19);
    const v8 sy = _mm256_mullo_epi32(_mm256_and_si256(est, _mm256_set1_epi32(0x3FF)), y);
    const v8 big = _mm256_cmpeq_epi32(_mm256_max_epu32(b19, sy), b19);
    const v8 d = _mm256_sub_epi32(b19, sy);
    const v8 frac = _mm256_and_si256(_mm256_blendv_epi8(_mm256_srli_epi32(d, 8), _mm256_srli_epi32(d, 9), big),
                                     _mm256_set1_epi32(0x7FFFFF));
    const v8 e2 = _mm256_sub_epi32(exp, _mm256_andnot_si256(big, _mm256_set1_epi32(1)));
    const v8 r = _mm256_or_si256(_mm256_or_si256(_mm256_and_si256(est, _mm256_set1_epi32((int)0x80000000u)),
                                                 _mm256_slli_epi32(e2, 23)), frac);
    const v8 zero = _mm256_or_si256(_mm256_cmpeq_epi32(exp, _mm256_setzero_si256()), _mm256_cmpeq_epi32(e2, _mm256_setzero_si256()));
    return _mm256_andnot_si256(zero, r);
}

/* shufb with a loop-invariant control as pshufb (see gow_edge_native.c: built from the control's value
 * exactly as spu_shufb_ref reads it, special selectors included) */
typedef struct { v8 ca, cb, sp; } g73_shuf;
static g73_shuf g73_shuf_make(u128 c)
{
    uint8_t ca[16], cb[16], sp[16];
    for (int h = 0; h < 16; h++) {
        const uint8_t s = c._u8[h];
        ca[h] = cb[h] = 0x80; sp[h] = 0;
        if ((s & 0xC0) == 0x80)      sp[h] = 0x00;
        else if ((s & 0xC0) == 0xC0) sp[h] = ((s & 0xE0) == 0xE0) ? 0x80 : 0xFF;
        else if ((s & 0x1F) < 16)    ca[h] = (uint8_t)SPU_W(s & 0x1F);
        else                         cb[h] = (uint8_t)SPU_W((s & 0x1F) - 16);
    }
    g73_shuf r; u128 t;
    memcpy(t._u8, ca, 16); r.ca = g73_bc(t);
    memcpy(t._u8, cb, 16); r.cb = g73_bc(t);
    memcpy(t._u8, sp, 16); r.sp = g73_bc(t);
    return r;
}
static inline v8 g73_shuf2(const g73_shuf* s, v8 a, v8 b)   /* shufb(a, b, c) */
{
    return _mm256_or_si256(_mm256_or_si256(_mm256_shuffle_epi8(a, s->ca), _mm256_shuffle_epi8(b, s->cb)), s->sp);
}
static inline v8 g73_shuf11(const g73_shuf* s, v8 a)        /* shufb(a, a, c) */
{
    return g73_shuf2(s, a, a);
}

/* fm / fma / fnms / fa / fs eight lanes at a time, with the per-lane rule of the SPU_FP_NATIVE helpers
 * (spu_helpers.h, spu__xn_*): the native op rounded toward zero is kept when every input is an ordinary
 * single or +-0 and the result is an ordinary single below the top binade, or a zero that is exact
 * (fm/fma/fnms: a zero factor, and a zero addend; fa/fs: any zero result), which loses its sign. If any
 * lane fails, the half it is in is redone by the 4-lane helper itself, which returns the same result the
 * lifted code gets. */
static inline v8 g73_bexp(v8 x) { return _mm256_srli_epi32(_mm256_slli_epi32(x, 1), 24); }
static inline v8 g73_zero(v8 x) { return _mm256_cmpeq_epi32(_mm256_slli_epi32(x, 1), _mm256_setzero_si256()); }
static inline v8 g73_plain(v8 x)
{
    const v8 e = g73_bexp(x);
    return _mm256_or_si256(_mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()),
                                            _mm256_cmpgt_epi32(_mm256_set1_epi32(255), e)), g73_zero(x));
}
static inline v8 g73_okr(v8 r)
{
    const v8 e = g73_bexp(r);
    return _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
}
static inline v8 g73_pos0(v8 r, v8 z) { return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r); }
static inline int g73_all(v8 m) { return _mm256_movemask_ps(_mm256_castsi256_ps(m)) == 0xFF; }
static inline v8 g73_f(__m256 x) { return _mm256_castps_si256(x); }
static inline __m256 g73_p(v8 x) { return _mm256_castsi256_ps(x); }

static long s73_fp_slow;   /* 8-lane ops redone by the 4-lane helpers (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) v8 g73_fm_slow(v8 a, v8 b)
{ s73_fp_slow++; return g73_v8(spu_fm(g73_lo(a), g73_lo(b)), spu_fm(g73_hi(a), g73_hi(b))); }
static __attribute__((noinline, cold)) v8 g73_fma_slow(v8 a, v8 b, v8 c)
{ s73_fp_slow++; return g73_v8(spu_fma(g73_lo(a), g73_lo(b), g73_lo(c)), spu_fma(g73_hi(a), g73_hi(b), g73_hi(c))); }
static __attribute__((noinline, cold)) v8 g73_fnms_slow(v8 a, v8 b, v8 c)
{ s73_fp_slow++; return g73_v8(spu_fnms(g73_lo(a), g73_lo(b), g73_lo(c)), spu_fnms(g73_hi(a), g73_hi(b), g73_hi(c))); }
static __attribute__((noinline, cold)) v8 g73_fa_slow(v8 a, v8 b)
{ s73_fp_slow++; return g73_v8(spu_fa(g73_lo(a), g73_lo(b)), spu_fa(g73_hi(a), g73_hi(b))); }
static __attribute__((noinline, cold)) v8 g73_fs_slow(v8 a, v8 b)
{ s73_fp_slow++; return g73_v8(spu_fs(g73_lo(a), g73_lo(b)), spu_fs(g73_hi(a), g73_hi(b))); }

static inline v8 g73_fm(v8 a, v8 b)
{
    const v8 r = g73_f(_mm256_mul_ps(g73_p(a), g73_p(b)));
    const v8 z = _mm256_or_si256(g73_zero(a), g73_zero(b));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g73_plain(a), g73_plain(b)), _mm256_or_si256(g73_okr(r), z));
    if (__builtin_expect(!g73_all(ok), 0)) return g73_fm_slow(a, b);
    return g73_pos0(r, z);
}
static inline v8 g73_fma(v8 a, v8 b, v8 c)   /* a*b + c */
{
    const v8 r = g73_f(_mm256_fmadd_ps(g73_p(a), g73_p(b), g73_p(c)));
    const v8 z = _mm256_and_si256(_mm256_or_si256(g73_zero(a), g73_zero(b)), g73_zero(c));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(_mm256_and_si256(g73_plain(a), g73_plain(b)), g73_plain(c)),
                                   _mm256_or_si256(g73_okr(r), z));
    if (__builtin_expect(!g73_all(ok), 0)) return g73_fma_slow(a, b, c);
    return g73_pos0(r, z);
}
static inline v8 g73_fnms(v8 a, v8 b, v8 c)  /* c - a*b: spu__xn_fma with a's sign flipped */
{
    const v8 x = _mm256_xor_si256(a, _mm256_set1_epi32((int)0x80000000u));
    const v8 r = g73_f(_mm256_fmadd_ps(g73_p(x), g73_p(b), g73_p(c)));
    const v8 z = _mm256_and_si256(_mm256_or_si256(g73_zero(x), g73_zero(b)), g73_zero(c));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(_mm256_and_si256(g73_plain(x), g73_plain(b)), g73_plain(c)),
                                   _mm256_or_si256(g73_okr(r), z));
    if (__builtin_expect(!g73_all(ok), 0)) return g73_fnms_slow(a, b, c);
    return g73_pos0(r, z);
}
static inline v8 g73_fa(v8 a, v8 b)
{
    const v8 r = g73_f(_mm256_add_ps(g73_p(a), g73_p(b)));
    const v8 z = g73_zero(r);
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g73_plain(a), g73_plain(b)), _mm256_or_si256(g73_okr(r), z));
    if (__builtin_expect(!g73_all(ok), 0)) return g73_fa_slow(a, b);
    return g73_pos0(r, z);
}
static inline v8 g73_fs(v8 a, v8 b)          /* a - b: spu__xn_fa with b's sign flipped */
{
    const v8 y = _mm256_xor_si256(b, _mm256_set1_epi32((int)0x80000000u));
    const v8 r = g73_f(_mm256_add_ps(g73_p(a), g73_p(y)));
    const v8 z = g73_zero(r);
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g73_plain(a), g73_plain(y)), _mm256_or_si256(g73_okr(r), z));
    if (__builtin_expect(!g73_all(ok), 0)) return g73_fs_slow(a, b);
    return g73_pos0(r, z);
}


/* ---- v3 building blocks: the lane rule checked once per iteration (see gow_edge_native.c's v3) ----
 * A speculative iteration runs the native ops unchecked and collects, in one running unsigned min and max,
 * the value shifted left by one (sign dropped) of every LS input and every op result: s >= 0x01000000 is
 * exponent >= 1, s <= 0xFDFFFFFF exponent <= 253. Allowed zero lanes (fm/fma/fnms: a zero factor, and a
 * zero addend; fa/fs: any zero result) are lifted out of the min and become +0. The iteration is kept only
 * if every lane passed; then every value it computed was an ordinary single or an allowed zero, so each
 * op's result is the helper's, and its inputs needed no check of their own (they were results, or checked
 * LS data, or whole-word copies of those: shufb controls that move aligned words, selb, sign masks).
 * Otherwise the iteration is recomputed with v2's checked ops. Exponent 254 inputs (allowed by the helpers)
 * and exact +-2^-126 trip the range too: that only costs a recompute. */
typedef struct { v8 mn, mx; } g73_rng;
static inline void g73_rng_add(g73_rng* g, v8 lo, v8 hi) { g->mn = _mm256_min_epu32(g->mn, lo); g->mx = _mm256_max_epu32(g->mx, hi); }
static inline v8 g73_chk_in(g73_rng* g, v8 x)   /* an LS-derived input: track it, return its zero lanes */
{
    const v8 s = _mm256_add_epi32(x, x);
    const v8 z = _mm256_cmpeq_epi32(s, _mm256_setzero_si256());
    g73_rng_add(g, _mm256_or_si256(s, z), s);
    return z;
}
static inline v8 g73_zf(v8 x) { return g73_f(_mm256_cmp_ps(g73_p(x), _mm256_setzero_ps(), _CMP_EQ_OQ)); }   /* zero lanes of a checked value */
static inline v8 g73_chk_out(g73_rng* g, v8 r, v8 zz)
{
    const v8 s = _mm256_add_epi32(r, r);
    g73_rng_add(g, _mm256_or_si256(s, zz), s);
    return _mm256_andnot_si256(zz, r);
}
static inline v8 g73_sfm(g73_rng* g, v8 a, v8 za, v8 b, v8 zb, v8* z)
{
    *z = _mm256_or_si256(za, zb);
    return g73_chk_out(g, g73_f(_mm256_mul_ps(g73_p(a), g73_p(b))), *z);
}
static inline v8 g73_sfma(g73_rng* g, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)
{
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g73_chk_out(g, g73_f(_mm256_fmadd_ps(g73_p(a), g73_p(b), g73_p(c))), *z);
}
static inline v8 g73_sfa(g73_rng* g, v8 a, v8 b, v8* z)
{
    const v8 r = g73_f(_mm256_add_ps(g73_p(a), g73_p(b)));
    *z = g73_zero(r);
    return g73_chk_out(g, r, *z);
}
static inline v8 g73_sfs(g73_rng* g, v8 a, v8 b, v8* z)
{
    const v8 r = g73_f(_mm256_sub_ps(g73_p(a), g73_p(b)));
    *z = g73_zero(r);
    return g73_chk_out(g, r, *z);
}
static inline int g73_rng_ok(const g73_rng* g)
{
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(g->mn, _mm256_set1_epi32(0x01000000)), g->mn);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(g->mx, _mm256_set1_epi32((int)0xFDFFFFFFu)), g->mx);
    return _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_and_si256(lo, hi))) == 0xFF;
}
static inline int g73_plain_all(v8 x)   /* every lane ordinary or +-0 (the helpers' input rule, exactly) */
{
    return g73_all(g73_plain(x));
}
static int g73_plain4(u128 x)
{
    for (int i = 0; i < 4; i++) {
        const uint32_t e = (x._u32[i] >> 23) & 0xFF;
        if (e == 255 || (e == 0 && (x._u32[i] << 1) != 0)) return 0;
    }
    return 1;
}
/* a shufb control whose every result word is one aligned source word in order, or a zero word */
static int g73_ctl_words(u128 c)
{
    for (int w = 0; w < 4; w++) {
        uint8_t s[4];
        for (int i = 0; i < 4; i++) s[i] = c._u8[SPU_W(4 * w + i)];
        int zero = 1, special = 0;
        for (int i = 0; i < 4; i++) { if ((s[i] & 0xC0) != 0x80) zero = 0; if (s[i] & 0x80) special = 1; }
        if (zero) continue;
        if (special) return 0;
        const int base = s[0] & 0x1F;
        if (base & 3) return 0;
        for (int i = 1; i < 4; i++) if ((s[i] & 0x1F) != base + i) return 0;
    }
    return 1;
}
static int g73_is_splat(u128 x, uint32_t v) { return x._u32[0] == v && x._u32[1] == v && x._u32[2] == v && x._u32[3] == v; }
/* do the 16-byte lines [a, a + la) and [b, b + lb) of LS overlap (or wrap past its end, counted as overlap) */
static int g73_overlap(uint32_t a, uint32_t la, uint32_t b, uint32_t lb)
{
    a &= SPU_LS_MASK & ~0xFu; b &= SPU_LS_MASK & ~0xFu;
    if (a + la > SPU_LS_SIZE || b + lb > SPU_LS_SIZE) return 1;
    return a < b + lb && b < a + la;
}

/* =====================================================================================================
 * v1: the region, statement for statement, registers in C locals, direct LS access. The control flow
 * between the generated segments is the lift's (the generator checks the lift has exactly this shape):
 *   79B0: S79B0; if (r116 == 0) goto 7DC0; S79B0T;
 *   7BB4: B7BB4; if (r17 == 0) goto 7BB4;
 *   7DC0: S7DC0; if (r116 == 0) goto 8058; S7DC0T;
 *   7ED8: B7ED8; if (r58 == 0) goto 7ED8;
 *   8058: S8058; if (r52 != 0) goto 79B0;
 *   (81C0)
 * ===================================================================================================== */
static void g73_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_73_REGS()
    GOW_73_LOAD()
    do {
        GOW_73_S79B0()
        if (r116._u32[0] != 0) {
            GOW_73_S79B0T()
            do { GOW_73_B7BB4() } while (r17._u32[0] == 0);
        }
        GOW_73_S7DC0()
        if (r116._u32[0] != 0) {
            GOW_73_S7DC0T()
            do { GOW_73_B7ED8() } while (r58._u32[0] == 0);
        }
        GOW_73_S8058()
    } while (r52._u32[0] != 0);
    GOW_73_STORE()
}

/* =====================================================================================================
 * v2 / v3: v1 with both inner loops 8-wide, every iteration. What the region computes (from the lift;
 * see EDGE_NATIVE.md): an outer iteration takes 8 vertices, gathered through 8 offsets and transposed into
 * SoA quads (A = vertices 0-3, B = 4-7): positions V = (r66|r65, r62|r63, r60|r61), normals
 * N = (r52|r50, r54|r53, r56|r55). Inner loop 1 runs over the r83 lights (16-byte records at r99
 * (position, w = 1/radius), r97 (colour, scaled by r93), r98 (direction)), software pipelined: light j's
 * d = P - V, d2 = d.d + eps and rsq = fi(frsqest(d2)) were formed by the previous iteration. Per light:
 *   n = d * rsq, or the light's direction, by the shufb control r9 (r104 while j < the count in r100,
 *   else r64), stored to the scratch at r23 (+96 per light);
 *   t = min(d2 * rsq * w, 1), s = 1 - t, a = (s*s*(3 - 2s))^2 * colour.w;
 *   wrap = min(|0.5 * (N.n) + 0.5|, 1), weight = |a| * wrap;
 *   colour.xyz * a stored to the scratch at r29; the weight summed (fa) and n * weight accumulated.
 * Inner loop 2 runs over the same lights again, reading the two scratch buffers back (light j+1's
 * values loaded by iteration j): c = clamp(N2.n, 0, 1) with N2 = (r71|r70, r69|r68, r67|r64) (formed in
 * 7DC0), v = colour.xyz * a * c, and the positive and the negated parts of v's three components summed
 * into six accumulators per vertex (fa). Outer block 8058 transposes the results back and stores
 * 3 x 8 quads per group to r73/r74/r75.
 *
 * Registers: the packed loops run every iteration (the last one too) and write back only the registers
 * live at the loop's exit. After loop 1 the region always runs S7DC0, S7DC0T, loop 2 (same count, at
 * least once) and S8058, and those overwrite every register loop 1 writes except its 8 accumulators
 * (r42 r45 r47 r48 r49 r51 r57 r58). After loop 2, S8058 overwrites all but 23 of loop 2's (the 12
 * accumulators, r2 r3 r4 r23 r24 r25 = copies of six of them, r10 r11 r12 = last-iteration values, r66
 * r52 = last-iteration loads). The other registers keep stale values until those blocks overwrite them,
 * so at loc_81C0 (where check mode compares all 128) every register is the lifted one. The last
 * iteration's loads of the next light (records past the last one) feed only dead registers in loop 1
 * and are skipped; in loop 2 they feed r12, which the 4-lane helpers compute from whatever is there.
 *
 * v2 checks every lane of every op (g73_fm & co.). v3 runs an iteration speculatively with one range check
 * (g73_l1_spec, g73_l2_spec) and recomputes it with v2's ops if any lane fails; it needs the region's
 * controls to move whole words and its constants to be plain and nonzero (g73_rk_make), else v2 runs.
 * ===================================================================================================== */
typedef struct { u128 r[128]; } g73_io;   /* the loop's registers, by number (only those it uses are set) */
#define G73_L1_IN(X) X(9) X(10) X(12) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34)     \
    X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) X(50) X(51) X(52)  \
    X(53) X(54) X(55) X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63) X(64) X(65) X(66) X(72) X(78) X(81) X(82)  \
    X(83) X(84) X(93) X(97) X(98) X(99) X(100) X(101) X(102) X(104)
#define G73_L1_OUT(X) X(42) X(45) X(47) X(48) X(49) X(51) X(57) X(58)
#define G73_L2_IN(X) X(5) X(12) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) X(43) \
    X(44) X(45) X(46) X(50) X(52) X(53) X(54) X(55) X(56) X(64) X(67) X(68) X(69) X(70) X(71) X(83)
#define G73_L2_OUT(X) X(2) X(3) X(4) X(10) X(11) X(12) X(23) X(24) X(25) X(34) X(35) X(36) X(37) X(38) X(39) X(40) \
    X(41) X(42) X(43) X(45) X(46) X(52) X(66)
#define G73_PUT(n) io.r[n] = r##n;
#define G73_GET(n) r##n = io.r[n];

static long s73_fb_l1, s73_fb_v2, s73_v3_redo, s73_iters;   /* lab statistics; racy across threads */
static uint64_t s73_cyc_l1, s73_cyc_l2;   /* rdtsc around the packed loops (lab statistic) */
static long s73_nospec[2];                /* iterations that ran the checked ops without trying v3's (per loop) */

/* ---- per region entry: the constants and shuffle tables both loops use. Every register read here is one
 * the region never writes (r64 and r9 are set per outer iteration and handled in the loop). ---- */
typedef struct { v8 idx, nz; } g73_wsh;   /* shufb(x, x, c) for a whole-word control c: vpermilps + zero words */
static g73_wsh g73_wsh_make(u128 c)
{
    u128 idx, nz;
    for (int w = 0; w < 4; w++) {
        const uint8_t s = c._u8[SPU_W(4 * w)];
        const int zero = (s & 0xC0) == 0x80;
        idx._u32[w] = zero ? 0 : ((s & 0x1F) >> 2) & 3;   /* {x, x}: word k of b is word k of a */
        nz._u32[w] = zero ? 0 : 0xFFFFFFFFu;
    }
    g73_wsh r = { g73_bc(idx), g73_bc(nz) };
    return r;
}
static inline v8 g73_wsh1(const g73_wsh* s, v8 x)
{
    return _mm256_and_si256(g73_f(_mm256_permutevar_ps(g73_p(x), s->idx)), s->nz);
}
/* frsqest with one gather for the 64-entry table (exact: integer work on the bit fields) */
static inline v8 g73_frsqest_g(v8 x)
{
    const v8 exp = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(x, 18), _mm256_set1_epi32(0x3F));
    const v8 f = _mm256_and_si256(_mm256_i32gather_epi32((const int*)spu_frsqest_fraction_lut, idx, 4), _mm256_set1_epi32(0x7FFFFF));
    v8 rexp = _mm256_sub_epi32(_mm256_set1_epi32(190), _mm256_srli_epi32(_mm256_add_epi32(exp, _mm256_set1_epi32(1)), 1));
    rexp = _mm256_blendv_epi8(rexp, _mm256_set1_epi32(255), _mm256_cmpeq_epi32(exp, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_slli_epi32(rexp, 23), f);
}

typedef struct {
    v8 ONE, EPS, HALF, TWO, NTWO, THREE, SIGN, R93;
    g73_shuf S59, S78, S81, S82, C104;     /* pshufb forms, exact for any control (the checked ops use them) */
    g73_wsh W59, W78, W81, W82;            /* word forms (v3, when the controls move whole words) */
    int fast;                               /* the region-invariant conditions of v3 hold */
    u128 c64; g73_shuf C64; int c64_set;   /* r64's table, rebuilt when its value changes */
} g73_rk;
static void g73_rk_make(g73_rk* K, int v3, u128 r33, u128 r44, u128 r59, u128 r72, u128 r78, u128 r81, u128 r82,
                        u128 r84, u128 r93, u128 r101, u128 r102, u128 r104)
{
    K->ONE = g73_bc(r33); K->EPS = g73_bc(r84); K->HALF = g73_bc(r72); K->TWO = g73_bc(r102); K->THREE = g73_bc(r101);
    K->NTWO = _mm256_xor_si256(K->TWO, _mm256_set1_epi32((int)0x80000000u));
    K->SIGN = g73_bc(r44); K->R93 = g73_bc(r93);
    K->S59 = g73_shuf_make(r59); K->S78 = g73_shuf_make(r78); K->S81 = g73_shuf_make(r81); K->S82 = g73_shuf_make(r82);
    K->C104 = g73_shuf_make(r104);
    K->W59 = g73_wsh_make(r59); K->W78 = g73_wsh_make(r78); K->W81 = g73_wsh_make(r81); K->W82 = g73_wsh_make(r82);
    K->c64_set = 0;
    /* v3 needs whole-word shuffles, the sign mask, the scalar form of the control choice (r59 splats word
     * 0) and plain nonzero constants (the speculative ops give them no zero lanes) */
    K->fast = v3 && g73_ctl_words(r59) && g73_ctl_words(r78) && g73_ctl_words(r81) && g73_ctl_words(r82) &&
              g73_ctl_words(r104) && g73_is_splat(r44, 0x80000000u) && g73_is_splat(r59, 0x00010203u) &&
              g73_plain4(r33) && g73_plain4(r84) && g73_plain4(r72) && g73_plain4(r101) && g73_plain4(r102) && g73_plain4(r93);
    if (K->fast) {
        const v8 zc = _mm256_or_si256(_mm256_or_si256(_mm256_or_si256(g73_zero(K->EPS), g73_zero(K->HALF)), g73_zero(K->ONE)),
                                      _mm256_or_si256(_mm256_or_si256(g73_zero(K->TWO), g73_zero(K->THREE)), g73_zero(K->R93)));
        if (_mm256_movemask_ps(g73_p(zc))) K->fast = 0;
    }
}
/* every lane of each pair ordinary or +-0 */
#define G73_PLAIN2(a, b) g73_plain_all(g73_v8(a, b))

/* ---- inner loop 1 ---- */
typedef struct { v8 VX, VY, VZ, NVX, NVY, NVZ; uint32_t b99, b97, b98; } g73_l1k;
typedef struct { v8 D2, RS, DX, DY, DZ, IR, LX, LY, LZ, CX, CY, CZ, CW, AX, AY, AZ, WS; } g73_l1s;
typedef struct { v8 NZp, NXp, NYp, OZ, OX, OY; } g73_l1o;

/* one iteration with v2's checked ops: light j's outputs and accumulators, and (unless last) light j+1's
 * pipelined values from its records at o43 */
static inline __attribute__((always_inline)) void g73_l1_exact(const uint8_t* ls, const g73_rk* RK, const g73_l1k* K,
                                                               const g73_shuf* ctl, uint32_t o43, int last,
                                                               const g73_l1s* s, g73_l1s* n, g73_l1o* o)
{
    const v8 DIST = g73_fm(s->D2, s->RS);                                    /* r69 | r68 */
    const v8 NZ = g73_fm(s->DZ, s->RS), NX = g73_fm(s->DX, s->RS), NY = g73_fm(s->DY, s->RS);
    const v8 T = g73_fm(DIST, s->IR);                                        /* r67 | r28 */
    o->NZp = g73_shuf2(ctl, NZ, s->LZ);                                      /* r68 | r20 */
    o->NXp = g73_shuf2(ctl, NX, s->LX);                                      /* r70 | r17 */
    o->NYp = g73_shuf2(ctl, NY, s->LY);                                      /* r69 | r18 */
    const v8 TC = g73_selb(T, RK->ONE, g73_fcgt(T, RK->ONE));               /* r11 | r41 */
    const v8 S = g73_fs(RK->ONE, TC);                                        /* r40 | r3 */
    const v8 DT = g73_fma(K->NVZ, o->NZp, g73_fma(K->NVY, o->NYp, g73_fm(K->NVX, o->NXp)));   /* r5 | r12 */
    const v8 KK = g73_fnms(RK->TWO, S, RK->THREE);                           /* r8 | r26 */
    const v8 SM = g73_fm(g73_fm(S, S), KK);                                  /* r9 | r2 */
    const v8 SM2 = g73_fm(SM, SM);                                           /* r4 | r3 */
    const v8 WR = g73_andc(g73_fma(DT, RK->HALF, RK->HALF), RK->SIGN);      /* r6 | r10 */
    const v8 WRc = g73_selb(WR, RK->ONE, g73_fcgt(WR, RK->ONE));            /* r6 | r7 */
    const v8 ATT = g73_fm(SM2, s->CW);                                       /* r13 | r12 */
    o->OZ = g73_fm(s->CZ, ATT); o->OX = g73_fm(s->CX, ATT); o->OY = g73_fm(s->CY, ATT);   /* r14|r8 r35|r11 r4|r3 */
    const v8 W = g73_fm(g73_andc(ATT, RK->SIGN), WRc);                       /* r37 | r36 */
    n->AZ = g73_fma(o->NZp, W, s->AZ);                                       /* r51 | r47 */
    n->WS = g73_fa(s->WS, W);                                                /* r42 | r45 */
    n->AX = g73_fma(o->NXp, W, s->AX);                                       /* r57 | r48 */
    n->AY = g73_fma(o->NYp, W, s->AY);                                       /* r49 | r58 */
    if (last) return;
    const v8 P = g73_bc(LD(K->b99 + o43)), C = g73_bc(LD(K->b97 + o43)), L = g73_bc(LD(K->b98 + o43));
    n->IR = g73_shuf11(&RK->S78, P);
    n->LZ = g73_shuf11(&RK->S81, L); n->LX = g73_shuf11(&RK->S59, L); n->LY = g73_shuf11(&RK->S82, L);
    n->DX = g73_fs(g73_shuf11(&RK->S59, P), K->VX);                          /* r31 | r32 */
    n->DY = g73_fs(g73_shuf11(&RK->S82, P), K->VY);                          /* r28 | r30 */
    n->DZ = g73_fs(g73_shuf11(&RK->S81, P), K->VZ);                          /* r26 | r27 */
    n->D2 = g73_fma(n->DZ, n->DZ, g73_fma(n->DY, n->DY, g73_fma(n->DX, n->DX, RK->EPS)));   /* r25 | r24 */
    n->RS = g73_fi(n->D2, g73_frsqest_g(n->D2));                             /* r10 | r12 */
    const v8 CC = g73_fm(RK->R93, C);                                        /* r68 = fm(r93, r71) */
    n->CX = g73_shuf11(&RK->S59, CC); n->CY = g73_shuf11(&RK->S82, CC);
    n->CW = g73_shuf11(&RK->S78, CC); n->CZ = g73_shuf11(&RK->S81, CC);
}

/* the same iteration speculatively (whole-word controls); returns whether every lane of every op passed */
static inline __attribute__((always_inline)) int g73_l1_spec(const uint8_t* ls, const g73_rk* RK, const g73_l1k* K,
                                                             const g73_shuf* ctl, uint32_t o43, int last,
                                                             const g73_l1s* s, g73_l1s* n, g73_l1o* o)
{
    g73_rng rg = { _mm256_set1_epi32(-1), _mm256_setzero_si256() };
    const v8 Z = _mm256_setzero_si256();
    const v8 zRS = g73_zf(s->RS);
    v8 z, zDIST, zT, zS, zA, zB, zATT, zW;
    const v8 DIST = g73_sfm(&rg, s->D2, g73_zf(s->D2), s->RS, zRS, &zDIST);
    const v8 NZ = g73_sfm(&rg, s->DZ, g73_zf(s->DZ), s->RS, zRS, &z);
    const v8 NX = g73_sfm(&rg, s->DX, g73_zf(s->DX), s->RS, zRS, &z);
    const v8 NY = g73_sfm(&rg, s->DY, g73_zf(s->DY), s->RS, zRS, &z);
    const v8 T = g73_sfm(&rg, DIST, zDIST, s->IR, g73_zf(s->IR), &zT);
    o->NZp = g73_shuf2(ctl, NZ, s->LZ);
    o->NXp = g73_shuf2(ctl, NX, s->LX);
    o->NYp = g73_shuf2(ctl, NY, s->LY);
    const v8 zNZp = g73_zf(o->NZp), zNXp = g73_zf(o->NXp), zNYp = g73_zf(o->NYp);
    const v8 TC = g73_selb(T, RK->ONE, g73_fcgt(T, RK->ONE));
    const v8 S = g73_sfs(&rg, RK->ONE, TC, &zS);
    v8 DT = g73_sfm(&rg, K->NVX, g73_zf(K->NVX), o->NXp, zNXp, &zA);
    DT = g73_sfma(&rg, K->NVY, g73_zf(K->NVY), o->NYp, zNYp, DT, zA, &zA);
    DT = g73_sfma(&rg, K->NVZ, g73_zf(K->NVZ), o->NZp, zNZp, DT, zA, &zA);
    /* fnms(2, S, 3) = 3 - 2*S: the fused op with the factor's sign flipped (TWO, THREE, HALF, EPS, R93 are
     * checked nonzero constants: no zero lanes) */
    const v8 KK = g73_sfma(&rg, RK->NTWO, Z, S, zS, RK->THREE, Z, &zB);
    const v8 S2 = g73_sfm(&rg, S, zS, S, zS, &z);
    const v8 SM = g73_sfm(&rg, S2, z, KK, zB, &z);
    const v8 SM2 = g73_sfm(&rg, SM, z, SM, z, &z);
    const v8 WR = g73_andc(g73_sfma(&rg, DT, zA, RK->HALF, Z, RK->HALF, Z, &zB), RK->SIGN);
    const v8 WRc = g73_selb(WR, RK->ONE, g73_fcgt(WR, RK->ONE));
    const v8 ATT = g73_sfm(&rg, SM2, z, s->CW, g73_zf(s->CW), &zATT);
    o->OZ = g73_sfm(&rg, s->CZ, g73_zf(s->CZ), ATT, zATT, &z);
    o->OX = g73_sfm(&rg, s->CX, g73_zf(s->CX), ATT, zATT, &z);
    o->OY = g73_sfm(&rg, s->CY, g73_zf(s->CY), ATT, zATT, &z);
    const v8 W = g73_sfm(&rg, g73_andc(ATT, RK->SIGN), zATT, WRc, g73_zf(WRc), &zW);
    n->AZ = g73_sfma(&rg, o->NZp, zNZp, W, zW, s->AZ, g73_zf(s->AZ), &z);
    n->WS = g73_sfa(&rg, s->WS, W, &z);
    n->AX = g73_sfma(&rg, o->NXp, zNXp, W, zW, s->AX, g73_zf(s->AX), &z);
    n->AY = g73_sfma(&rg, o->NYp, zNYp, W, zW, s->AY, g73_zf(s->AY), &z);
    if (!last) {
        const v8 P = g73_bc(LD(K->b99 + o43)), C = g73_bc(LD(K->b97 + o43)), L = g73_bc(LD(K->b98 + o43));
        g73_chk_in(&rg, P);
        const v8 zC = g73_chk_in(&rg, C);
        n->IR = g73_wsh1(&RK->W78, P);
        n->LZ = g73_wsh1(&RK->W81, L); n->LX = g73_wsh1(&RK->W59, L); n->LY = g73_wsh1(&RK->W82, L);
        g73_chk_in(&rg, _mm256_blend_epi32(n->LX, _mm256_blend_epi32(n->LY, n->LZ, 0xCC), 0xF0));   /* L.w is never used */
        v8 zx, zy, zz2;
        n->DX = g73_sfs(&rg, g73_wsh1(&RK->W59, P), K->VX, &zx);
        n->DY = g73_sfs(&rg, g73_wsh1(&RK->W82, P), K->VY, &zy);
        n->DZ = g73_sfs(&rg, g73_wsh1(&RK->W81, P), K->VZ, &zz2);
        v8 D2 = g73_sfma(&rg, n->DX, zx, n->DX, zx, RK->EPS, Z, &z);
        D2 = g73_sfma(&rg, n->DY, zy, n->DY, zy, D2, z, &z);
        n->D2 = g73_sfma(&rg, n->DZ, zz2, n->DZ, zz2, D2, z, &z);
        n->RS = g73_fi(n->D2, g73_frsqest_g(n->D2));
        g73_chk_in(&rg, n->RS);
        const v8 CC = g73_sfm(&rg, RK->R93, Z, C, zC, &z);
        n->CX = g73_wsh1(&RK->W59, CC); n->CY = g73_wsh1(&RK->W82, CC);
        n->CW = g73_wsh1(&RK->W78, CC); n->CZ = g73_wsh1(&RK->W81, CC);
    }
    return g73_rng_ok(&rg);
}

/* inner loop 1, all n iterations; io holds the registers it reads, and gets back the 8 live ones */
static __attribute__((noinline)) void g73_loop1(uint8_t* ls, g73_rk* RK, g73_io* io, uint32_t nit)
{
    const u128* R = io->r;
    g73_l1k K;
    K.VX = g73_v8(R[66], R[65]); K.VY = g73_v8(R[62], R[63]); K.VZ = g73_v8(R[60], R[61]);
    K.NVX = g73_v8(R[52], R[50]); K.NVY = g73_v8(R[54], R[53]); K.NVZ = g73_v8(R[56], R[55]);
    K.b99 = R[99]._u32[0]; K.b97 = R[97]._u32[0]; K.b98 = R[98]._u32[0];
    /* the two controls r9 takes: r104 (region-invariant) and r64 (from LS 0xF920 each outer iteration) */
    if (!RK->c64_set || memcmp(&RK->c64, &R[64], 16)) {
        RK->c64 = R[64]; RK->C64 = g73_shuf_make(R[64]); RK->c64_set = 1;
        if (RK->fast && !g73_ctl_words(R[64])) RK->fast = 0;
    }
    const g73_shuf* const C104 = &RK->C104;
    const g73_shuf* const C64 = &RK->C64;
    g73_shuf Cdyn;
    const g73_shuf* ctl;
    if (!memcmp(&R[9], &R[104], 16)) ctl = C104;
    else if (!memcmp(&R[9], &R[64], 16)) ctl = C64;
    else { Cdyn = g73_shuf_make(R[9]); ctl = &Cdyn; }
    const int fast = RK->fast && (ctl != &Cdyn || g73_ctl_words(R[9])) &&
                     G73_PLAIN2(R[66], R[65]) && G73_PLAIN2(R[62], R[63]) && G73_PLAIN2(R[60], R[61]) &&
                     G73_PLAIN2(R[52], R[50]) && G73_PLAIN2(R[54], R[53]) && G73_PLAIN2(R[56], R[55]);
    if (RK->fast && !fast) s73_fb_v2++;
    g73_l1s s, n;
    s.D2 = g73_v8(R[25], R[24]); s.RS = g73_v8(R[10], R[12]);
    s.DX = g73_v8(R[31], R[32]); s.DY = g73_v8(R[28], R[30]); s.DZ = g73_v8(R[26], R[27]);
    s.IR = g73_bc(R[39]); s.LX = g73_bc(R[40]); s.LY = g73_bc(R[38]); s.LZ = g73_bc(R[41]);
    s.CX = g73_bc(R[37]); s.CY = g73_bc(R[36]); s.CZ = g73_bc(R[35]); s.CW = g73_bc(R[34]);
    s.AX = g73_v8(R[57], R[48]); s.AY = g73_v8(R[49], R[58]); s.AZ = g73_v8(R[51], R[47]); s.WS = g73_v8(R[42], R[45]);
    int spec = fast && g73_plain_all(s.D2) && g73_plain_all(s.RS) && g73_plain_all(s.DX) && g73_plain_all(s.DY) &&
               g73_plain_all(s.DZ) && G73_PLAIN2(R[39], R[40]) && G73_PLAIN2(R[38], R[41]) && G73_PLAIN2(R[37], R[36]) &&
               G73_PLAIN2(R[35], R[34]) && g73_plain_all(s.AX) && g73_plain_all(s.AY) && g73_plain_all(s.AZ) &&
               g73_plain_all(s.WS);
    const int32_t lim = (int32_t)R[100]._u32[0];
    u128 q43 = R[43];
    uint32_t p23 = R[23]._u32[0], p29 = R[29]._u32[0];
    for (uint32_t k = 0; k < nit; k++) {
        const int last = k + 1 == nit;
        const uint32_t o43 = q43._u32[0];
        g73_l1o o;
        int ok = spec && g73_l1_spec(ls, RK, &K, ctl, o43, last, &s, &n, &o);
        if (!ok) {
            if (spec) s73_v3_redo++; else s73_nospec[0]++;
            g73_l1_exact(ls, RK, &K, ctl, o43, last, &s, &n, &o);
        }
        /* the lifted order: the six quads at r23, (the next light's loads,) the six at r29 */
        g73_stlo(ls, p23 + 0x20, o.NZp); g73_stlo(ls, p23 + 0x00, o.NXp); g73_stlo(ls, p23 + 0x10, o.NYp);
        g73_sthi(ls, p23 + 0x30, o.NXp); g73_sthi(ls, p23 + 0x40, o.NYp); g73_sthi(ls, p23 + 0x50, o.NZp);
        g73_stlo(ls, p29 + 0x20, o.OZ); g73_sthi(ls, p29 + 0x50, o.OZ); g73_sthi(ls, p29 + 0x30, o.OX);
        g73_stlo(ls, p29 + 0x00, o.OX); g73_sthi(ls, p29 + 0x40, o.OY); g73_stlo(ls, p29 + 0x10, o.OY);
        p23 += 96; p29 += 96;
        if (last) { s.AX = n.AX; s.AY = n.AY; s.AZ = n.AZ; s.WS = n.WS; break; }
        /* the next control: r9 = selb(r64, r104, shufb(cgt(r100, r43), ., r59)), r43 as it was here */
        if (fast) ctl = lim > (int32_t)o43 ? C104 : C64;
        else {
            const u128 c22 = spu_cgt(R[100], q43);
            const u128 cv = spu_selb(R[64], R[104], spu_shufb(c22, c22, R[59]));
            if (!memcmp(&cv, &R[104], 16)) ctl = C104;
            else if (!memcmp(&cv, &R[64], 16)) ctl = C64;
            else { Cdyn = g73_shuf_make(cv); ctl = &Cdyn; }
        }
        q43 = spu_ai(q43, 16);
        s = n;
        if (fast && !ok)   /* after an exact iteration: are the carried values ordinary or zero */
            spec = g73_plain_all(s.D2) && g73_plain_all(s.RS) && g73_plain_all(s.DX) && g73_plain_all(s.DY) &&
                   g73_plain_all(s.DZ) && g73_plain_all(s.IR) && g73_plain_all(s.LX) && g73_plain_all(s.LY) &&
                   g73_plain_all(s.LZ) && g73_plain_all(s.CX) && g73_plain_all(s.CY) && g73_plain_all(s.CZ) &&
                   g73_plain_all(s.CW) && g73_plain_all(s.AX) && g73_plain_all(s.AY) && g73_plain_all(s.AZ) &&
                   g73_plain_all(s.WS);
        else
            spec = fast;
    }
    s73_iters += nit;
    io->r[57] = g73_lo(s.AX); io->r[48] = g73_hi(s.AX); io->r[49] = g73_lo(s.AY); io->r[58] = g73_hi(s.AY);
    io->r[51] = g73_lo(s.AZ); io->r[47] = g73_hi(s.AZ); io->r[42] = g73_lo(s.WS); io->r[45] = g73_hi(s.WS);
}

/* ---- inner loop 2 ---- */
typedef struct { v8 NNX, NNY, NNZ; } g73_l2k;
typedef struct { v8 X, CX, CY, CZ, PX, PY, PZ, MX, MY, MZ; } g73_l2s;
typedef struct { v8 VXc, VZc, NZn, CYn; } g73_l2o;   /* last-iteration values S8058 leaves live */

static inline __attribute__((always_inline)) void g73_l2_exact(const uint8_t* ls, const g73_rk* RK, const g73_l2k* K,
                                                               uint32_t p30, uint32_t p31, const g73_l2s* s, g73_l2s* n, g73_l2o* o)
{
    const v8 C = g73_and(g73_selb(s->X, RK->ONE, g73_fcgt(s->X, RK->ONE)), g73_cgt0(s->X));   /* r3 | r2 */
    const v8 NXn = g73_ld2(ls, p30 + 0x00, p30 + 0x30), NYn = g73_ld2(ls, p30 + 0x10, p30 + 0x40);
    const v8 NZn = g73_ld2(ls, p30 + 0x20, p30 + 0x50);
    n->CX = g73_ld2(ls, p31 + 0x00, p31 + 0x30); n->CY = g73_ld2(ls, p31 + 0x10, p31 + 0x40);
    n->CZ = g73_ld2(ls, p31 + 0x20, p31 + 0x50);
    const v8 VZc = g73_fm(s->CZ, C), VXc = g73_fm(s->CX, C), VYc = g73_fm(s->CY, C);   /* r58|r8 r61|r5 r7|r26 */
    n->X = g73_fma(K->NNZ, NZn, g73_fma(K->NNY, NYn, g73_fm(K->NNX, NXn)));            /* r12 | r5 */
    n->PX = g73_fa(s->PX, g73_max0(VXc));                                               /* r46 | r35 */
    n->PY = g73_fa(s->PY, g73_max0(VYc));                                               /* r42 | r45 */
    n->PZ = g73_fa(s->PZ, g73_max0(VZc));                                               /* r43 | r34 */
    n->MX = g73_fa(s->MX, g73_max0(g73_xor(VXc, RK->SIGN)));                           /* r39 | r41 */
    n->MY = g73_fa(s->MY, g73_max0(g73_xor(VYc, RK->SIGN)));                           /* r38 | r40 */
    n->MZ = g73_fa(s->MZ, g73_max0(g73_xor(VZc, RK->SIGN)));                           /* r36 | r37 */
    o->VXc = VXc; o->VZc = VZc; o->NZn = NZn; o->CYn = n->CY;
}

/* last: the iteration whose loads read past the records. Of what those feed, only r12 (the A half of the
 * next N2.n) and the raw loads r66/r52 are live after the loop: the loads are not range-checked, and r12
 * is formed by the 4-lane helpers themselves (fm, fma, fma: the lifted ops), so a stale record does not
 * send the iteration to the checked ops. */
static inline __attribute__((always_inline)) int g73_l2_spec(const uint8_t* ls, const g73_rk* RK, const g73_l2k* K,
                                                             uint32_t p30, uint32_t p31, int last,
                                                             const g73_l2s* s, g73_l2s* n, g73_l2o* o)
{
    g73_rng rg = { _mm256_set1_epi32(-1), _mm256_setzero_si256() };
    v8 z, zP;
    const v8 C = g73_and(g73_selb(s->X, RK->ONE, g73_fcgt(s->X, RK->ONE)), g73_cgt0(s->X));
    const v8 zC = g73_zf(C);
    const v8 NXn = g73_ld2(ls, p30 + 0x00, p30 + 0x30), NYn = g73_ld2(ls, p30 + 0x10, p30 + 0x40);
    const v8 NZn = g73_ld2(ls, p30 + 0x20, p30 + 0x50);
    n->CX = g73_ld2(ls, p31 + 0x00, p31 + 0x30); n->CY = g73_ld2(ls, p31 + 0x10, p31 + 0x40);
    n->CZ = g73_ld2(ls, p31 + 0x20, p31 + 0x50);
    const v8 VZc = g73_sfm(&rg, s->CZ, g73_zf(s->CZ), C, zC, &z);
    const v8 VXc = g73_sfm(&rg, s->CX, g73_zf(s->CX), C, zC, &z);
    const v8 VYc = g73_sfm(&rg, s->CY, g73_zf(s->CY), C, zC, &z);
    if (!last) {
        const v8 zNX = g73_chk_in(&rg, NXn), zNY = g73_chk_in(&rg, NYn), zNZ = g73_chk_in(&rg, NZn);
        g73_chk_in(&rg, n->CX); g73_chk_in(&rg, n->CY); g73_chk_in(&rg, n->CZ);
        v8 X = g73_sfm(&rg, K->NNX, g73_zf(K->NNX), NXn, zNX, &zP);
        X = g73_sfma(&rg, K->NNY, g73_zf(K->NNY), NYn, zNY, X, zP, &zP);
        n->X = g73_sfma(&rg, K->NNZ, g73_zf(K->NNZ), NZn, zNZ, X, zP, &zP);
    } else {   /* r29 = fm(r71, r13); r29 = fma(r69, r27, r29); r12 = fma(r67, r66, r29) */
        const u128 x = spu_fma(g73_lo(K->NNZ), g73_lo(NZn), spu_fma(g73_lo(K->NNY), g73_lo(NYn), spu_fm(g73_lo(K->NNX), g73_lo(NXn))));
        n->X = g73_v8(x, x);   /* the B half (r5) is dead */
    }
    n->PX = g73_sfa(&rg, s->PX, g73_max0(VXc), &z);
    n->PY = g73_sfa(&rg, s->PY, g73_max0(VYc), &z);
    n->PZ = g73_sfa(&rg, s->PZ, g73_max0(VZc), &z);
    n->MX = g73_sfa(&rg, s->MX, g73_max0(g73_xor(VXc, RK->SIGN)), &z);
    n->MY = g73_sfa(&rg, s->MY, g73_max0(g73_xor(VYc, RK->SIGN)), &z);
    n->MZ = g73_sfa(&rg, s->MZ, g73_max0(g73_xor(VZc, RK->SIGN)), &z);
    o->VXc = VXc; o->VZc = VZc; o->NZn = NZn; o->CYn = n->CY;
    return g73_rng_ok(&rg);
}

static __attribute__((noinline)) void g73_loop2(const uint8_t* ls, const g73_rk* RK, g73_io* io, uint32_t nit)
{
    const u128* R = io->r;
    g73_l2k K;
    K.NNX = g73_v8(R[71], R[70]); K.NNY = g73_v8(R[69], R[68]); K.NNZ = g73_v8(R[67], R[64]);
    g73_l2s s, n;
    g73_l2o o;
    s.X = g73_v8(R[12], R[5]);
    s.CX = g73_v8(R[56], R[53]); s.CY = g73_v8(R[55], R[52]); s.CZ = g73_v8(R[54], R[50]);
    s.PX = g73_v8(R[46], R[35]); s.PY = g73_v8(R[42], R[45]); s.PZ = g73_v8(R[43], R[34]);
    s.MX = g73_v8(R[39], R[41]); s.MY = g73_v8(R[38], R[40]); s.MZ = g73_v8(R[36], R[37]);
    const int fast = RK->fast && g73_plain_all(K.NNX) && g73_plain_all(K.NNY) && g73_plain_all(K.NNZ);
    if (RK->fast && !fast) s73_fb_v2++;
    int spec = fast && g73_plain_all(s.X) && g73_plain_all(s.CX) && g73_plain_all(s.CY) && g73_plain_all(s.CZ) &&
               g73_plain_all(s.PX) && g73_plain_all(s.PY) && g73_plain_all(s.PZ) && g73_plain_all(s.MX) &&
               g73_plain_all(s.MY) && g73_plain_all(s.MZ);
    uint32_t p30 = R[30]._u32[0], p31 = R[31]._u32[0];
    for (uint32_t k = 0; k < nit; k++) {
        const int ok = spec && g73_l2_spec(ls, RK, &K, p30, p31, k + 1 == nit, &s, &n, &o);
        if (!ok) {
            if (spec) s73_v3_redo++; else s73_nospec[1]++;
            g73_l2_exact(ls, RK, &K, p30, p31, &s, &n, &o);
        }
        s = n;
        p30 += 96; p31 += 96;
        if (fast && !ok)
            spec = g73_plain_all(s.X) && g73_plain_all(s.CX) && g73_plain_all(s.CY) && g73_plain_all(s.CZ) &&
                   g73_plain_all(s.PX) && g73_plain_all(s.PY) && g73_plain_all(s.PZ) && g73_plain_all(s.MX) &&
                   g73_plain_all(s.MY) && g73_plain_all(s.MZ);
        else
            spec = fast;
    }
    u128* W = io->r;
    W[46] = g73_lo(s.PX); W[35] = g73_hi(s.PX); W[42] = g73_lo(s.PY); W[45] = g73_hi(s.PY);
    W[43] = g73_lo(s.PZ); W[34] = g73_hi(s.PZ); W[39] = g73_lo(s.MX); W[41] = g73_hi(s.MX);
    W[38] = g73_lo(s.MY); W[40] = g73_hi(s.MY); W[36] = g73_lo(s.MZ); W[37] = g73_hi(s.MZ);
    W[3] = W[42]; W[2] = W[45]; W[4] = W[35]; W[25] = W[34]; W[24] = W[39]; W[23] = W[38];   /* the fa results before their copies */
    W[10] = g73_lo(g73_xor(o.VZc, RK->SIGN)); W[11] = g73_hi(g73_xor(o.VXc, RK->SIGN));    /* xor(r58, r44), xor(r5, r44) */
    W[12] = g73_lo(s.X); W[66] = g73_lo(o.NZn); W[52] = g73_hi(o.CYn);                      /* fma(r67, r66, r29); the loads */
}

static void g73_v23(spu_context* ctx, int v3)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_73_REGS()
    GOW_73_LOAD()
    g73_io io;
    g73_rk RK;
    g73_rk_make(&RK, v3, r33, r44, r59, r72, r78, r81, r82, r84, r93, r101, r102, r104);
    do {
        GOW_73_S79B0()
        if (r116._u32[0] != 0) {
            GOW_73_S79B0T()
            const uint32_t n = r83._u32[0] - r46._u32[0];
            /* the packed loop loads each iteration's next light before storing its r23 quads (the lift stores
             * first): needs the scratch it writes apart from the records it reads */
            const int apart = !g73_overlap(r23._u32[0], 96 * n, r99._u32[0], 16 * n + 16) &&
                              !g73_overlap(r23._u32[0], 96 * n, r97._u32[0], 16 * n + 16) &&
                              !g73_overlap(r23._u32[0], 96 * n, r98._u32[0], 16 * n + 16) &&
                              !g73_overlap(r29._u32[0], 96 * n, r99._u32[0], 16 * n + 16) &&
                              !g73_overlap(r29._u32[0], 96 * n, r97._u32[0], 16 * n + 16) &&
                              !g73_overlap(r29._u32[0], 96 * n, r98._u32[0], 16 * n + 16);
            if (n >= 1 && n < 0x10000 && apart) {
                G73_L1_IN(G73_PUT)
                const uint64_t t0 = __rdtsc();
                g73_loop1(ls, &RK, &io, n);
                s73_cyc_l1 += __rdtsc() - t0;
                G73_L1_OUT(G73_GET)
            } else {
                s73_fb_l1++;
                do { GOW_73_B7BB4() } while (r17._u32[0] == 0);
            }
        }
        GOW_73_S7DC0()
        if (r116._u32[0] != 0) {
            GOW_73_S7DC0T()
            const uint32_t n = r83._u32[0] - r32._u32[0];
            if (n >= 1 && n < 0x10000) {
                G73_L2_IN(G73_PUT)
                const uint64_t t0 = __rdtsc();
                g73_loop2(ls, &RK, &io, n);
                s73_cyc_l2 += __rdtsc() - t0;
                G73_L2_OUT(G73_GET)
            } else {
                s73_fb_l1++;
                do { GOW_73_B7ED8() } while (r58._u32[0] == 0);
            }
        }
        GOW_73_S8058()
    } while (r52._u32[0] != 0);
    GOW_73_STORE()
}

/* ---- self-test of the widened integer float ops against the 4-lane helpers (once, at mode init) ---- */
static uint32_t s73_rng = 0x2545F491u;
static uint32_t g73_rand(void) { s73_rng ^= s73_rng << 13; s73_rng ^= s73_rng >> 17; s73_rng ^= s73_rng << 5; return s73_rng; }
static void g73_selftest(void)
{
    long bad = 0;
    for (int it = 0; it < 20000; it++) {
        u128 a[2], b[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                uint32_t x = g73_rand(), y = g73_rand();
                if ((it & 7) == 1) x &= 0x807FFFFFu;            /* exponent 0 */
                if ((it & 7) == 2) x |= 0x7F800000u;            /* exponent 255 */
                if ((it & 7) == 3) y = x;                        /* equal operands for fcgt */
                if ((it & 7) == 4) x = (x & 0x80FFFFFFu) | 0x3F000000u;   /* near 1.0 */
                a[h]._u32[i] = x; b[h]._u32[i] = y;
            }
        const v8 A = g73_v8(a[0], a[1]), B = g73_v8(b[0], b[1]);
        const v8 rs = g73_frsqest(A), fi = g73_fi(A, B), gt = g73_fcgt(A, B);
        const v8 fi2 = g73_fi(A, rs);
        const v8 rsg = g73_frsqest_g(A);
        for (int h = 0; h < 2; h++) {
            const u128 e1 = spu_frsqest(a[h]), e2 = spu_fi(a[h], b[h]), e3 = spu_fcgt(a[h], b[h]), e4 = spu_fi(a[h], e1);
            const u128 g1 = h ? g73_hi(rs) : g73_lo(rs), g2 = h ? g73_hi(fi) : g73_lo(fi);
            const u128 g3 = h ? g73_hi(gt) : g73_lo(gt), g4 = h ? g73_hi(fi2) : g73_lo(fi2);
            bad += memcmp(&e1, &g1, 16) != 0;
            bad += memcmp(&e2, &g2, 16) != 0;
            bad += memcmp(&e3, &g3, 16) != 0;
            bad += memcmp(&e4, &g4, 16) != 0;
            const u128 g5 = h ? g73_hi(rsg) : g73_lo(rsg);
            bad += memcmp(&e1, &g5, 16) != 0;
        }
    }
    fprintf(stderr, "[edge-73e0] self-test of frsqest (two forms)/fi/fcgt x8 against the 4-lane helpers: %ld of 200000 differ\n", bad);
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s73_mode = -1;
static int s73_v2;   /* GOW_EDGE_73E0_V2=1: modes 1 and 3 use v2 instead of v3 */

int gow_edge_73e0_mode(void)
{
    if (s73_mode < 0) {
        const char* e = getenv("GOW_EDGE_73E0");
        int m = e && *e ? atoi(e) : 0;   /* default: lifted */
        if (m < 0 || m > 4) m = 0;
        const char* v = getenv("GOW_EDGE_73E0_V2");
        s73_v2 = v && atoi(v) == 1;
        s73_mode = m;
        if (!m) fprintf(stderr, "[edge-73e0] GOW_EDGE_73E0=0 (lifted region)\n");
        else {
            fprintf(stderr, "[edge-73e0] GOW_EDGE_73E0=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : s73_v2 ? "v2" : "v3");
            g73_selftest();
        }
    }
    return s73_mode;
}

static int s73_fault = -1;
static void g73_run(spu_context* ctx)
{
    if (s73_mode == 2 || s73_mode == 4) g73_v1(ctx);
    else                                g73_v23(ctx, !s73_v2);
    if (__builtin_expect(s73_fault != 0, 0)) {
        if (s73_fault < 0) { const char* f = getenv("GOW_EDGE_73E0_FAULT"); s73_fault = f ? atoi(f) : 0; }
        /* 1: the last store of the last outer iteration, ST(r74 + r89, r53) */
        if (s73_fault == 1) ctx->ls[((ctx->gpr[74]._u32[0] + ctx->gpr[89]._u32[0]) & (SPU_LS_MASK & ~0xFu)) + 15] ^= 1;
        if (s73_fault == 2) ctx->gpr[60]._u32[0] ^= 1;
        if (s73_fault > 0) { fprintf(stderr, "[edge-73e0] fault %d injected\n", s73_fault); s73_fault = 0; }
    }
}

/* Modes 1 and 2: replaces the region; the lift then jumps to loc_000081C0. */
void gow_edge_73e0_79b0(spu_context* ctx)
{
    g73_run(ctx);
}

/* Modes 3 and 4 (check): at loc_79B0 run the native region on the live state, keep its result, put the
 * state back and let the lifted region run; at loc_81C0 compare all 128 GPRs and the whole LS. 81C0 is
 * also reached by the skip at 7890 (r107 == 0), hence `pending`. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} g73_check_state;
static _Thread_local g73_check_state* t73_chk;

static long s73_calls, s73_bad, s73_outer, s73_inner, s73_const_diff;
static uint64_t s73_cyc_nat, s73_cyc_lift;
static u128 s73_const[24], s73_f920;
static const int k73_const_regs[24] = { 33, 44, 59, 72, 78, 80, 81, 82, 84, 85, 93, 96, 101, 102, 104,
                                        108, 109, 110, 111, 112, 113, 118, 119, 120 };

static void g73_check_report(void)
{
    fprintf(stderr, "[edge-73e0-check] %ld calls checked, %ld differ; %ld outer iterations (8 vertices each), "
                    "%ld inner iterations per loop; region constants differ from the first call in %ld calls\n",
            s73_calls, s73_bad, s73_outer, s73_inner, s73_const_diff);
    fprintf(stderr, "[edge-73e0-check] fallbacks: %ld inner-loop entries to the lifted loop, %ld to v2 (unexpected "
                    "constants); v3 iterations recomputed exactly: %ld of 2 x %ld; 8-lane ops redone by the 4-lane "
                    "helpers: %ld; tsc: native %.1f M, lifted %.1f M (%.1f vs %.1f per inner iteration)\n",
            s73_fb_l1, s73_fb_v2, s73_v3_redo, s73_iters, s73_fp_slow, s73_cyc_nat / 1e6, s73_cyc_lift / 1e6,
            s73_inner ? (double)s73_cyc_nat / (double)s73_inner : 0.0,
            s73_inner ? (double)s73_cyc_lift / (double)s73_inner : 0.0);
    fprintf(stderr, "[edge-73e0-check] checked ops without a v3 try: loop 1 %ld, loop 2 %ld iterations; "
                    "tsc in the packed loops: loop 1 %.1f M (%.1f per iteration), loop 2 %.1f M (%.1f)\n",
            s73_nospec[0], s73_nospec[1], s73_cyc_l1 / 1e6, s73_iters ? (double)s73_cyc_l1 / (double)s73_iters : 0.0,
            s73_cyc_l2 / 1e6, s73_iters ? (double)s73_cyc_l2 / (double)s73_iters : 0.0);
    if (s73_calls) {
        for (int i = 0; i < 24; i++)
            fprintf(stderr, "[edge-73e0-check]   r%-3d = %08X %08X %08X %08X\n", k73_const_regs[i],
                    s73_const[i]._u32[0], s73_const[i]._u32[1], s73_const[i]._u32[2], s73_const[i]._u32[3]);
        fprintf(stderr, "[edge-73e0-check]   LS[F920] (r64 in loop 1) = %08X %08X %08X %08X\n",
                s73_f920._u32[0], s73_f920._u32[1], s73_f920._u32[2], s73_f920._u32[3]);
    }
}

void gow_edge_73e0_check_begin(spu_context* ctx)
{
    g73_check_state* c = t73_chk;
    if (!c) {
        c = (g73_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t73_chk = c;
    }
    const u128* r = ctx->gpr;
    if (s73_calls == 0) {
        for (int i = 0; i < 24; i++) s73_const[i] = r[k73_const_regs[i]];
        s73_f920 = g73_ld(ctx->ls, 0xF920);
        atexit(g73_check_report);
    } else {
        for (int i = 0; i < 24; i++)
            if (memcmp(&s73_const[i], &r[k73_const_regs[i]], 16)) { s73_const_diff++; break; }
    }
    /* outer iterations: r114 counts 0, 8, .. while r107 > it (clgt); inner: r83 per loop */
    s73_outer += (r[107]._u32[0] + 7) / 8;
    s73_inner += (long)((r[107]._u32[0] + 7) / 8) * (int32_t)r[83]._u32[0];
    if (s73_calls < 4)
        fprintf(stderr, "[edge-73e0-check] call %ld: r107 %u r83 %d r100 %08X  r99 %05X r97 %05X r98 %05X  r103 %05X r105 %05X r106 %05X  r73 %05X r74 %05X r75 %05X\n",
                s73_calls, r[107]._u32[0], (int32_t)r[83]._u32[0], r[100]._u32[0], r[99]._u32[0], r[97]._u32[0], r[98]._u32[0],
                r[103]._u32[0], r[105]._u32[0], r[106]._u32[0], r[73]._u32[0], r[74]._u32[0], r[75]._u32[0]);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    g73_run(ctx);
    s73_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_73e0_check_end(spu_context* ctx)
{
    g73_check_state* c = t73_chk;
    if (!c || !c->pending) return;
    s73_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s73_calls++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-73e0-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-73e0-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s73_bad++;
        fprintf(stderr, "[edge-73e0-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) g73_check_report();   /* a game run ends by kill: atexit never runs */
}

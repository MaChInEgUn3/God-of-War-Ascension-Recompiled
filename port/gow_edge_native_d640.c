/* gow_edge_native_d640.c -- native replacement for the two hot loops of the EDGE job module's (d935, lift
 * spu/jobmod_d93503647dd8_e6d98) function D640: its own loop at loc_0000D92C (exit loc_0000DB70) and the
 * loop at loc_0000D4DC in D250, the function D640 calls just before it. The d935 lift is built through
 * gen_edge_native_lift.py, which puts a switch before each loop; GOW_EDGE_D640 picks the path for both.
 * See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native.c (-O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1,
 * SPU_FP_CHECK_HOOK=0): the float helpers are the same inline helpers the lift uses. Runs inside
 * spu_run_with_halt, i.e. with MXCSR rounding toward zero.
 *
 * Clean-room: written from our own lift of the module and our own captures.
 *
 * GOW_EDGE_D640 (read once):
 *   0        lifted loops
 *   1/unset  native, packed (8 lanes, one range check per pair of iterations: "v3"); with
 *            GOW_EDGE_D640_V2=1 every pair runs the checked ops instead ("v2": the per-lane rule and the
 *            4-lane fallback on every float op)
 *   2        native v1 (each loop statement for statement, registers in locals)
 *   3        check the packed version (v3, or v2 with GOW_EDGE_D640_V2=1): run it on a copy of the state,
 *            then the lifted loop, and compare all GPRs and the whole LS at the loop exit (loc_DB70 / the
 *            fall-through after D4DC's back edge)
 *   4        check v1, the same way
 * GOW_EDGE_D640_FAULT=<n> (negative test, first native run of that loop only): 1 flips bit 0 of byte 15 of
 * the first quad the last D92C iteration stores, 2 bit 0 of r5 after D92C, 3 bit 0 of byte 15 of the first
 * quad the last D4DC iteration stores, 4 bit 0 of r24 after D4DC.
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
static inline __m128i g64_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 g64_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = g64_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void g64_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g64_bswap32x4(x));
}
#define LD(a)    g64_ld(ls, (a))
#define ST(a, v) g64_st(ls, (a), (v))

/* the lifted loops: GOW_D6A_* (D92C) and GOW_D6B_* (D4DC). Game code, so it is not in the repo:
 * gen_edge_d640_body.py writes it from the d935 lift at build time. */
#include "gow_edge_d640_body.h"

/* =====================================================================================================
 * v1: each loop statement for statement, registers in C locals, direct LS access. The control flow is the
 * lift's (the generator checks the lift has exactly this shape):
 *   D92C: BODY; if (r2 == 0) goto DB70; TAIL; goto D92C;
 *   D4DC: BODY; if (r3 != 0) goto D4DC;
 * ===================================================================================================== */
static void g64_a_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_D6A_REGS()
    GOW_D6A_LOAD()
    for (;;) {
        GOW_D6A_BODY()
        if (r2._u32[0] == 0) break;
        GOW_D6A_TAIL()
    }
    GOW_D6A_STORE()
}

static void g64_b_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_D6B_REGS()
    GOW_D6B_LOAD()
    do {
        GOW_D6B_BODY()
    } while (r3._u32[0] != 0);
    GOW_D6B_STORE()
}

/* ---- 8-lane building blocks: two SPU quadwords per vector (two loop iterations' values: the earlier one
 * in the low 128 bits, the later one in the high) ---- */
typedef __m256i v8;

static inline v8 g64_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline u128 g64_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 g64_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline v8 g64_bc(u128 a) { __m128i x; memcpy(&x, &a, 16); return _mm256_broadcastsi128_si256(x); }
static inline v8 g64_ld2(const uint8_t* ls, uint32_t lo, uint32_t hi)
{
    const __m128i a = _mm_loadu_si128((const __m128i*)(ls + (lo & (SPU_LS_MASK & ~0xFu))));
    const __m128i b = _mm_loadu_si128((const __m128i*)(ls + (hi & (SPU_LS_MASK & ~0xFu))));
    return _mm256_shuffle_epi8(_mm256_set_m128i(b, a),
                               _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline void g64_stlo(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g64_bswap32x4(_mm256_castsi256_si128(v))); }
static inline void g64_sthi(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g64_bswap32x4(_mm256_extracti128_si256(v, 1))); }
/* (prev.hi, cur.lo): the value one iteration back of each lane pair */
static inline v8 g64_back(v8 prev, v8 cur) { return _mm256_permute2x128_si256(prev, cur, 0x21); }

/* integer ops, exact by construction */
static inline v8 g64_and(v8 a, v8 b)  { return _mm256_and_si256(a, b); }
static inline v8 g64_or(v8 a, v8 b)   { return _mm256_or_si256(a, b); }
static inline v8 g64_xor(v8 a, v8 b)  { return _mm256_xor_si256(a, b); }
static inline v8 g64_selb(v8 a, v8 b, v8 m) { return _mm256_or_si256(_mm256_andnot_si256(m, a), _mm256_and_si256(m, b)); }
static inline v8 g64_ceq(v8 a, v8 b)  { return _mm256_cmpeq_epi32(a, b); }
/* fcgt: the toolkit's key compare (spu__sse_key: magnitude with denormals as 0, negated for negatives) */
static inline v8 g64_key(v8 x)
{
    const v8 m = _mm256_and_si256(x, _mm256_set1_epi32(0x7FFFFFFF));
    const v8 den = _mm256_cmpeq_epi32(_mm256_and_si256(x, _mm256_set1_epi32(0x7F800000)), _mm256_setzero_si256());
    const v8 mag = _mm256_andnot_si256(den, m);
    const v8 neg = _mm256_srai_epi32(x, 31);
    return _mm256_sub_epi32(_mm256_xor_si256(mag, neg), neg);
}
static inline v8 g64_fcgt(v8 a, v8 b) { return _mm256_cmpgt_epi32(g64_key(a), g64_key(b)); }
/* frest / fi / cflts: spu_helpers.h's AVX2 forms (integer work on the bit fields), eight lanes */
static inline v8 g64_frest(v8 x)
{
    const v8 exp = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(x, 18), _mm256_set1_epi32(0x1F));
    const v8 row = _mm256_srli_epi32(idx, 3);
    v8 f = _mm256_permutevar8x32_epi32(_mm256_loadu_si256((const __m256i*)spu_frest_fraction_lut), idx);
    for (int k = 1; k < 4; k++)
        f = _mm256_blendv_epi8(f, _mm256_permutevar8x32_epi32(_mm256_loadu_si256((const __m256i*)(spu_frest_fraction_lut + 8 * k)), idx),
                               _mm256_cmpeq_epi32(row, _mm256_set1_epi32(k)));
    f = _mm256_and_si256(f, _mm256_set1_epi32(0x7FFFFF));
    v8 rexp = _mm256_sub_epi32(_mm256_set1_epi32(253), exp);
    rexp = _mm256_andnot_si256(_mm256_cmpgt_epi32(exp, _mm256_set1_epi32(252)), rexp);          /* exp >= 253: 0 */
    rexp = _mm256_blendv_epi8(rexp, _mm256_set1_epi32(255), _mm256_cmpeq_epi32(exp, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_or_si256(_mm256_and_si256(x, _mm256_set1_epi32((int)0x80000000u)),
                                           _mm256_slli_epi32(rexp, 23)), f);
}
static inline v8 g64_fi(v8 a, v8 est)
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
static inline v8 g64_cflts(v8 x, int i8)   /* the 24-bit significand shifted by be - 150 + (173 - i8), saturated */
{
    const v8 be = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 m = _mm256_or_si256(_mm256_and_si256(x, _mm256_set1_epi32(0x7FFFFF)), _mm256_set1_epi32(0x800000));
    const v8 sh = _mm256_add_epi32(be, _mm256_set1_epi32(23 - (i8 & 0xFF)));
    const v8 lsh = _mm256_max_epi32(sh, _mm256_setzero_si256());
    const v8 rsh = _mm256_max_epi32(_mm256_sub_epi32(_mm256_setzero_si256(), sh), _mm256_setzero_si256());
    v8 v = _mm256_blendv_epi8(_mm256_srlv_epi32(m, rsh), _mm256_sllv_epi32(m, lsh), _mm256_cmpgt_epi32(sh, _mm256_set1_epi32(-1)));
    const v8 neg = _mm256_srai_epi32(x, 31), isz = _mm256_cmpeq_epi32(be, _mm256_setzero_si256());
    v = _mm256_sub_epi32(_mm256_xor_si256(v, neg), neg);
    const v8 sat = _mm256_xor_si256(_mm256_set1_epi32(0x7FFFFFFF), neg);
    v = _mm256_blendv_epi8(v, sat, _mm256_cmpgt_epi32(sh, _mm256_set1_epi32(7)));
    return _mm256_andnot_si256(isz, v);
}
/* gbh: bit (7 - H) of word 0 = LSB of SPU halfword H, which is host halfword H ^ 1; the other words 0 */
static uint8_t k64_gbh_lut[256];
static uint8_t k64_gb_lut[16];
static void g64_luts(void)
{
    for (int b = 0; b < 256; b++) {
        int v = 0;
        for (int j = 0; j < 8; j++) if (b >> j & 1) v |= 1 << (7 - (j ^ 1));
        k64_gbh_lut[b] = (uint8_t)v;
    }
    for (int b = 0; b < 16; b++)
        k64_gb_lut[b] = (uint8_t)(((b & 1) << 3) | ((b & 2) << 1) | ((b & 4) >> 1) | ((b & 8) >> 3));
}
static inline v8 g64_gbh(v8 x)
{
    const uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_slli_epi16(x, 7));   /* bit 2j: host halfword j's LSB */
    const uint32_t lo = k64_gbh_lut[_pext_u32(m, 0x5555u)], hi = k64_gbh_lut[_pext_u32(m >> 16, 0x5555u)];
    return _mm256_setr_epi32((int)lo, 0, 0, 0, (int)hi, 0, 0, 0);
}
/* gb: word 0 = LSBs of words 0..3 as bits 3..0; the other words 0 */
static inline v8 g64_gb(v8 x)
{
    const uint32_t m = (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_slli_epi32(x, 31)));
    return _mm256_setr_epi32(k64_gb_lut[m & 15], 0, 0, 0, k64_gb_lut[m >> 4], 0, 0, 0);
}

/* shufb with a loop-invariant control as pshufb (see gow_edge_native.c: built from the control's value
 * exactly as spu_shufb_ref reads it, special selectors included) */
typedef struct { v8 ca, cb, sp; } g64_shuf;
static g64_shuf g64_shuf_make(u128 c)
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
    g64_shuf r; u128 t;
    memcpy(t._u8, ca, 16); r.ca = g64_bc(t);
    memcpy(t._u8, cb, 16); r.cb = g64_bc(t);
    memcpy(t._u8, sp, 16); r.sp = g64_bc(t);
    return r;
}
static inline v8 g64_shuf2(const g64_shuf* s, v8 a, v8 b)   /* shufb(a, b, c) */
{
    return _mm256_or_si256(_mm256_or_si256(_mm256_shuffle_epi8(a, s->ca), _mm256_shuffle_epi8(b, s->cb)), s->sp);
}
/* the same on one quadword (the low halves of a g64_shuf) */
static inline u128 g64_shuf1(const g64_shuf* s, u128 a, u128 b)
{
    __m128i A, B; memcpy(&A, &a, 16); memcpy(&B, &b, 16);
    const __m128i r = _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(A, _mm256_castsi256_si128(s->ca)),
                                                _mm_shuffle_epi8(B, _mm256_castsi256_si128(s->cb))),
                                   _mm256_castsi256_si128(s->sp));
    u128 o; memcpy(&o, &r, 16); return o;
}
/* shufb with a control that changes (read from LS): spu_shufb's rule on the values as they are held (SPU
 * byte P at host byte P ^ 3), so selector s picks host byte (s & 15) ^ 3 of a or b, with no byte swaps */
static inline u128 g64_shufv(u128 a, u128 b, u128 c)
{
    __m128i A, B, C; memcpy(&A, &a, 16); memcpy(&B, &b, 16); memcpy(&C, &c, 16);
    const __m128i idx = _mm_xor_si128(_mm_and_si128(C, _mm_set1_epi8(0x0F)), _mm_set1_epi8(0x03));
    const __m128i fromb = _mm_cmpeq_epi8(_mm_and_si128(C, _mm_set1_epi8(0x10)), _mm_set1_epi8(0x10));
    __m128i r = _mm_blendv_epi8(_mm_shuffle_epi8(A, idx), _mm_shuffle_epi8(B, idx), fromb);
    const __m128i m80 = _mm_cmplt_epi8(C, _mm_setzero_si128());
    const __m128i mC0 = _mm_cmpeq_epi8(_mm_and_si128(C, _mm_set1_epi8((char)0xC0)), _mm_set1_epi8((char)0xC0));
    const __m128i mE0 = _mm_cmpeq_epi8(_mm_and_si128(C, _mm_set1_epi8((char)0xE0)), _mm_set1_epi8((char)0xE0));
    r = _mm_blendv_epi8(r, _mm_blendv_epi8(mC0, _mm_set1_epi8((char)0x80), mE0), m80);
    u128 o; memcpy(&o, &r, 16); return o;
}
/* shuffle structs built once per distinct control (the loops' controls are LS constants) */
typedef struct { u128 key; g64_shuf s; } g64_shc;
static void g64_shuf_cached(g64_shc* c, int* valid, u128 ctl, g64_shuf* out)
{
    if (!*valid || memcmp(&c->key, &ctl, 16)) { c->key = ctl; c->s = g64_shuf_make(ctl); *valid = 1; }
    *out = c->s;
}

/* fm / fma / fms / fnms / fs eight lanes at a time, with the per-lane rule of the SPU_FP_NATIVE helpers
 * (spu_helpers.h, spu__xn_*): the native op rounded toward zero is kept when every input is an ordinary
 * single or +-0 and the result is an ordinary single below the top binade, or a zero that is exact
 * (fm/fma/fms/fnms: a zero factor, and a zero addend; fs: any zero result), which loses its sign. If any
 * lane fails, the half it is in is redone by the 4-lane helper itself, which returns the same result the
 * lifted code gets. */
static inline v8 g64_bexp(v8 x) { return _mm256_srli_epi32(_mm256_slli_epi32(x, 1), 24); }
static inline v8 g64_zero(v8 x) { return _mm256_cmpeq_epi32(_mm256_slli_epi32(x, 1), _mm256_setzero_si256()); }
static inline v8 g64_plain(v8 x)
{
    const v8 e = g64_bexp(x);
    return _mm256_or_si256(_mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()),
                                            _mm256_cmpgt_epi32(_mm256_set1_epi32(255), e)), g64_zero(x));
}
static inline v8 g64_okr(v8 r)
{
    const v8 e = g64_bexp(r);
    return _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
}
static inline v8 g64_pos0(v8 r, v8 z) { return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r); }
static inline int g64_all(v8 m) { return _mm256_movemask_ps(_mm256_castsi256_ps(m)) == 0xFF; }
static inline v8 g64_f(__m256 x) { return _mm256_castps_si256(x); }
static inline __m256 g64_p(v8 x) { return _mm256_castsi256_ps(x); }

static long s64_fp_slow;   /* 8-lane ops redone by the 4-lane helpers (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) v8 g64_fm_slow(v8 a, v8 b)
{ s64_fp_slow++; return g64_v8(spu_fm(g64_lo(a), g64_lo(b)), spu_fm(g64_hi(a), g64_hi(b))); }
static __attribute__((noinline, cold)) v8 g64_fma_slow(v8 a, v8 b, v8 c)
{ s64_fp_slow++; return g64_v8(spu_fma(g64_lo(a), g64_lo(b), g64_lo(c)), spu_fma(g64_hi(a), g64_hi(b), g64_hi(c))); }
static __attribute__((noinline, cold)) v8 g64_fms_slow(v8 a, v8 b, v8 c)
{ s64_fp_slow++; return g64_v8(spu_fms(g64_lo(a), g64_lo(b), g64_lo(c)), spu_fms(g64_hi(a), g64_hi(b), g64_hi(c))); }
static __attribute__((noinline, cold)) v8 g64_fnms_slow(v8 a, v8 b, v8 c)
{ s64_fp_slow++; return g64_v8(spu_fnms(g64_lo(a), g64_lo(b), g64_lo(c)), spu_fnms(g64_hi(a), g64_hi(b), g64_hi(c))); }
static __attribute__((noinline, cold)) v8 g64_fs_slow(v8 a, v8 b)
{ s64_fp_slow++; return g64_v8(spu_fs(g64_lo(a), g64_lo(b)), spu_fs(g64_hi(a), g64_hi(b))); }

static inline v8 g64_fm(v8 a, v8 b)
{
    const v8 r = g64_f(_mm256_mul_ps(g64_p(a), g64_p(b)));
    const v8 z = _mm256_or_si256(g64_zero(a), g64_zero(b));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g64_plain(a), g64_plain(b)), _mm256_or_si256(g64_okr(r), z));
    if (__builtin_expect(!g64_all(ok), 0)) return g64_fm_slow(a, b);
    return g64_pos0(r, z);
}
static inline v8 g64_fmx(v8 x, v8 y, v8 w, int* bad)   /* x*y + w, the lane rule of spu__xn_fma */
{
    const v8 r = g64_f(_mm256_fmadd_ps(g64_p(x), g64_p(y), g64_p(w)));
    const v8 z = _mm256_and_si256(_mm256_or_si256(g64_zero(x), g64_zero(y)), g64_zero(w));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(_mm256_and_si256(g64_plain(x), g64_plain(y)), g64_plain(w)),
                                   _mm256_or_si256(g64_okr(r), z));
    *bad = !g64_all(ok);
    return g64_pos0(r, z);
}
static inline v8 g64_fma(v8 a, v8 b, v8 c)   /* a*b + c */
{
    int bad; const v8 r = g64_fmx(a, b, c, &bad);
    if (__builtin_expect(bad, 0)) return g64_fma_slow(a, b, c);
    return r;
}
static inline v8 g64_fms(v8 a, v8 b, v8 c)   /* a*b - c: spu__xn_fma with c's sign flipped */
{
    int bad; const v8 r = g64_fmx(a, b, _mm256_xor_si256(c, _mm256_set1_epi32((int)0x80000000u)), &bad);
    if (__builtin_expect(bad, 0)) return g64_fms_slow(a, b, c);
    return r;
}
static inline v8 g64_fnms(v8 a, v8 b, v8 c)  /* c - a*b: spu__xn_fma with a's sign flipped */
{
    int bad; const v8 r = g64_fmx(_mm256_xor_si256(a, _mm256_set1_epi32((int)0x80000000u)), b, c, &bad);
    if (__builtin_expect(bad, 0)) return g64_fnms_slow(a, b, c);
    return r;
}
static inline v8 g64_fs(v8 a, v8 b)          /* a - b: spu__xn_fa with b's sign flipped */
{
    const v8 y = _mm256_xor_si256(b, _mm256_set1_epi32((int)0x80000000u));
    const v8 r = g64_f(_mm256_add_ps(g64_p(a), g64_p(y)));
    const v8 z = g64_zero(r);
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g64_plain(a), g64_plain(y)), _mm256_or_si256(g64_okr(r), z));
    if (__builtin_expect(!g64_all(ok), 0)) return g64_fs_slow(a, b);
    return g64_pos0(r, z);
}

/* ---- one range check per pair of iterations (as gow_edge_native.c's v3 and gow_edge_native_73e0.c's) ----
 * A speculative pair runs the native ops unchecked and collects, in one running unsigned min and max, the
 * value shifted left by one (sign dropped) of every float input it takes from outside (LS, the previous
 * pair) and of every op result: s >= 0x01000000 is exponent >= 1, s <= 0xFDFFFFFF exponent <= 253.
 * Allowed zero lanes (fm/fma/fms/fnms: a zero factor, and a zero addend; fs: any zero result) are lifted out
 * of the min and become +0. The pair is kept only if every lane passed: then every value it computed was an
 * ordinary single or an allowed zero, so each op's result is the helper's, and the inputs of ops that are
 * results of earlier ops needed no check of their own. Otherwise the pair is recomputed with the checked
 * ops above before anything is stored. Exponent-254 inputs (allowed by the helpers) trip the range too:
 * that only costs a recompute. Loop invariants that feed float ops are checked once per loop entry (plain,
 * the helpers' input rule exactly), else that entry uses the checked ops throughout. */
typedef struct { v8 mn, mx; } g64_rng;
static inline void g64_rng_init(g64_rng* R) { R->mn = _mm256_set1_epi32(-1); R->mx = _mm256_setzero_si256(); }
static inline void g64_rng_add(g64_rng* R, v8 lo, v8 hi) { R->mn = _mm256_min_epu32(R->mn, lo); R->mx = _mm256_max_epu32(R->mx, hi); }
static inline v8 g64_chk_in(g64_rng* R, v8 x)
{
    const v8 s = _mm256_add_epi32(x, x);
    const v8 z = _mm256_cmpeq_epi32(s, _mm256_setzero_si256());
    g64_rng_add(R, _mm256_or_si256(s, z), s);
    return z;
}
static inline v8 g64_chk_out(g64_rng* R, v8 r, v8 zz)
{
    const v8 s = _mm256_add_epi32(r, r);
    g64_rng_add(R, _mm256_or_si256(s, zz), s);
    return _mm256_andnot_si256(zz, r);
}
static inline int g64_rng_ok(const g64_rng* R)
{
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(R->mn, _mm256_set1_epi32(0x01000000)), R->mn);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(R->mx, _mm256_set1_epi32((int)0xFDFFFFFFu)), R->mx);
    return g64_all(_mm256_and_si256(lo, hi));
}
static inline int g64_plain_all(v8 x) { return g64_all(g64_plain(x)); }

/* a float op of a pair: speculative (the raw native op, its result tracked; *z = its allowed zero lanes)
 * or checked (spec == 0: the per-lane rule with the 4-lane fallback; z unused) */
#define G64_AI static inline __attribute__((always_inline))
G64_AI v8 g64_xin(int spec, g64_rng* R, v8 x) { return spec ? g64_chk_in(R, x) : _mm256_setzero_si256(); }
G64_AI v8 g64_xfma(int spec, g64_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)
{
    if (!spec) { *z = _mm256_setzero_si256(); return g64_fma(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g64_chk_out(R, g64_f(_mm256_fmadd_ps(g64_p(a), g64_p(b), g64_p(c))), *z);
}
G64_AI v8 g64_xfms(int spec, g64_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)   /* a*b - c */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g64_fms(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g64_chk_out(R, g64_f(_mm256_fmsub_ps(g64_p(a), g64_p(b), g64_p(c))), *z);
}
G64_AI v8 g64_xfnms(int spec, g64_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)  /* c - a*b */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g64_fnms(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g64_chk_out(R, g64_f(_mm256_fnmadd_ps(g64_p(a), g64_p(b), g64_p(c))), *z);
}
G64_AI v8 g64_xfm(int spec, g64_rng* R, v8 a, v8 za, v8 b, v8 zb, v8* z)
{
    if (!spec) { *z = _mm256_setzero_si256(); return g64_fm(a, b); }
    *z = _mm256_or_si256(za, zb);
    return g64_chk_out(R, g64_f(_mm256_mul_ps(g64_p(a), g64_p(b))), *z);
}
G64_AI v8 g64_xfs(int spec, g64_rng* R, v8 a, v8 b, v8* z)   /* a - b */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g64_fs(a, b); }
    const v8 r = g64_f(_mm256_sub_ps(g64_p(a), g64_p(b)));
    *z = g64_zero(r);
    return g64_chk_out(R, r, *z);
}
/* a shufb control whose every result word is one aligned source word in order, or a zero word */
static int g64_ctl_words(u128 c)
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
static int g64_is_splat(u128 x, uint32_t v) { return x._u32[0] == v && x._u32[1] == v && x._u32[2] == v && x._u32[3] == v; }

/* do the 16-byte lines [a, a + la) and [b, b + lb) of LS overlap (or wrap past its end, counted as overlap) */
static int g64_overlap(uint32_t a, uint32_t la, uint32_t b, uint32_t lb)
{
    if (a + la > SPU_LS_SIZE || b + lb > SPU_LS_SIZE) return 1;
    return a < b + lb && b < a + la;
}

static int s64_v2;             /* GOW_EDGE_D640_V2=1: the packed loops run the checked ops on every pair */
static long s64_fb_v1[2];      /* entries the packed path left to v1 (per loop) */
static long s64_fb_chk[2];     /* entries that ran the checked ops throughout (invariants not plain) */
static long s64_pk_iters[2];   /* iterations run packed */
static long s64_redo[2];       /* speculative pairs recomputed with the checked ops */

/* =====================================================================================================
 * D4DC packed. What the loop computes (from the lift; see EDGE_NATIVE.md): per iteration one 64-byte input
 * record of 4 vertices (four quads at r39/r33/r32/r29 + r23), transposed to x, y, z (selb/shufb), times a
 * 3x4 matrix (12 fma, broadcast rows r55/r48/r47/r46, r54/r45/r44/r43, r53/r42/r41/r40, translation
 * r56/r52/r51/r49) into clip-space (X, Y, Z, W); then 1/W (frest, fi, fnms, fma), X/W Y/W Z/W with W's sign
 * folded back (xor), a viewport scale/offset (fma) and W > 0; then the screen values against six bounds
 * (fcgt), packed into outcodes (shufb, gbh) and stored with them as four quads at r66/r37/r35/r34 + r21
 * (r21 += r31 per iteration, r31 rotates). Software pipelined three deep: iteration k runs the transform
 * of record k (stage A), the divide of record k-1 (B) and the tests and stores of record k-2 (C).
 * A pair of iterations (k, k+1) runs A on records (k, k+1), B on (k-1, k) and C on (k-2, k-1), each
 * 8 lanes wide; a stage's input for the low half is the high half of the previous pair's (g64_back).
 * Every float result is the same op on the same operands as in the lift. Loads of record k+1 come before
 * the stores of iteration k (the lift has them after), so the pairs run only if the input records and
 * the output quads do not overlap; else v1. The last one or two iterations run the lifted body (v1's
 * macros) from the pairs' state, so every register the loop writes comes out exactly.
 * ===================================================================================================== */
typedef struct {
    g64_shuf s38, s59, s27, s78, s79, s80, s70, s75, s28, s25, s30, s69;
    v8 m50, r55, r56, r48, r52, r47, r51, r46, r49, r54, r45, r44, r43, r53, r42, r41, r40;
    v8 r77, r76, r73, r72, r68, r67, r64, r60, r57, r63, r62, r61, r58, r71, r74;
    v8 z55, z56, z48, z52, z47, z51, z46, z49, z54, z45, z44, z43, z53, z42, z41, z40;
    v8 z77, z76, z73, z72, z68, z67, z71;
    uint32_t i39, i33, i32, i29;
} g64_bk;
typedef struct { v8 W, E, X, Y, Z, B22, B6, B11, B12, o17, o10, o7, o18; } g64_bp;

G64_AI int g64_b_pair(const g64_bk* K, const uint8_t* ls, uint32_t k0, const g64_bp* pv, g64_bp* o, int spec)
{
    g64_rng R; g64_rng_init(&R);
    const uint32_t k1 = k0 + 64u;
    /* A: records (k, k+1) */
    const v8 i20 = g64_ld2(ls, K->i29 + k0, K->i29 + k1), i19 = g64_ld2(ls, K->i33 + k0, K->i33 + k1);
    const v8 i8 = g64_ld2(ls, K->i39 + k0, K->i39 + k1), i16 = g64_ld2(ls, K->i32 + k0, K->i32 + k1);
    const v8 t15 = g64_shuf2(&K->s38, i8, i19), t11 = g64_shuf2(&K->s78, i16, i20), t8 = g64_shuf2(&K->s79, i8, i19);
    const v8 x4 = g64_selb(t15, t11, K->m50), x14 = g64_shuf2(&K->s80, t11, t15);
    const v8 t16 = g64_shuf2(&K->s79, i16, i20), x20 = g64_selb(t8, t16, K->m50);
    const v8 z4 = g64_xin(spec, &R, x4), z14 = g64_xin(spec, &R, x14), z20 = g64_xin(spec, &R, x20);
    v8 za15, za12, za13, za4, zb15, za19, zb13, za14, zW, zX, zY, zZ;
    const v8 a15 = g64_xfma(spec, &R, x4, z4, K->r55, K->z55, K->r56, K->z56, &za15);
    const v8 a12 = g64_xfma(spec, &R, x4, z4, K->r48, K->z48, K->r52, K->z52, &za12);
    const v8 a13 = g64_xfma(spec, &R, x4, z4, K->r47, K->z47, K->r51, K->z51, &za13);
    const v8 a4 = g64_xfma(spec, &R, x4, z4, K->r46, K->z46, K->r49, K->z49, &za4);
    const v8 b15 = g64_xfma(spec, &R, x14, z14, K->r54, K->z54, a15, za15, &zb15);
    const v8 a19 = g64_xfma(spec, &R, x14, z14, K->r45, K->z45, a12, za12, &za19);
    const v8 b13 = g64_xfma(spec, &R, x14, z14, K->r44, K->z44, a13, za13, &zb13);
    const v8 a14 = g64_xfma(spec, &R, x14, z14, K->r43, K->z43, a4, za4, &za14);
    o->W = g64_xfma(spec, &R, x20, z20, K->r53, K->z53, b15, zb15, &zW);
    o->X = g64_xfma(spec, &R, x20, z20, K->r42, K->z42, a19, za19, &zX);
    o->E = g64_frest(o->W);
    o->Y = g64_xfma(spec, &R, x20, z20, K->r41, K->z41, b13, zb13, &zY);
    o->Z = g64_xfma(spec, &R, x20, z20, K->r40, K->z40, a14, za14, &zZ);
    /* B: records (k-1, k) */
    const v8 w = g64_back(pv->W, o->W), e = g64_back(pv->E, o->E);
    const v8 x = g64_back(pv->X, o->X), y = g64_back(pv->Y, o->Y), z = g64_back(pv->Z, o->Z);
    const v8 zw = g64_xin(spec, &R, w), zx = g64_xin(spec, &R, x), zy = g64_xin(spec, &R, y), zz = g64_xin(spec, &R, z);
    const v8 q10 = g64_fi(w, e);
    const v8 z10 = g64_xin(spec, &R, q10);
    v8 zq2, zq2b, zpx, zpy, zpz, zd;
    const v8 q2 = g64_xfnms(spec, &R, q10, z10, w, zw, K->r71, K->z71, &zq2);
    const v8 q9 = g64_and(w, K->r74);
    const v8 q2b = g64_xfma(spec, &R, q2, zq2, q10, z10, q10, z10, &zq2b);
    const v8 px = g64_xfm(spec, &R, x, zx, q2b, zq2b, &zpx);
    const v8 pz = g64_xfm(spec, &R, z, zz, q2b, zq2b, &zpz);
    const v8 py = g64_xfm(spec, &R, y, zy, q2b, zq2b, &zpy);
    o->B22 = g64_xfma(spec, &R, g64_xor(q9, px), zpx, K->r77, K->z77, K->r76, K->z76, &zd);
    o->B12 = g64_fcgt(w, K->r58);
    o->B6 = g64_xfma(spec, &R, g64_xor(q9, py), zpy, K->r73, K->z73, K->r72, K->z72, &zd);
    o->B11 = g64_xfma(spec, &R, g64_xor(q9, pz), zpz, K->r68, K->z68, K->r67, K->z67, &zd);
    if (spec && !g64_rng_ok(&R)) return 0;
    /* C: records (k-2, k-1); integer work only */
    const v8 c22 = g64_back(pv->B22, o->B22), c6 = g64_back(pv->B6, o->B6), c11 = g64_back(pv->B11, o->B11);
    const v8 c12 = g64_back(pv->B12, o->B12);
    const v8 f9 = g64_fcgt(K->r64, c11), f4 = g64_fcgt(c22, K->r60), f14 = g64_fcgt(c6, K->r57);
    const v8 f3 = g64_fcgt(c11, K->r63), f2 = g64_fcgt(K->r62, c22), f7 = g64_fcgt(K->r61, c6);
    const v8 o17a = g64_shuf2(&K->s38, c22, c6), o18a = g64_shuf2(&K->s59, c22, c6);
    const v8 h5 = g64_shuf2(&K->s27, f4, f2), h13 = g64_shuf2(&K->s27, f14, f7), h22 = g64_shuf2(&K->s27, f3, f9);
    const v8 h6 = g64_shuf2(&K->s38, h5, h13);
    const v8 h11 = g64_shuf2(&K->s70, h22, c12), h3 = g64_shuf2(&K->s75, h22, c12);
    const v8 h22b = g64_shuf2(&K->s59, h5, h13);
    const v8 h10 = g64_shuf2(&K->s28, h6, h11), h19 = g64_shuf2(&K->s25, h6, h11);
    const v8 h7 = g64_shuf2(&K->s25, h22b, h3), h5b = g64_shuf2(&K->s28, h22b, h3);
    o->o10 = g64_shuf2(&K->s30, o17a, g64_gbh(h10));
    o->o17 = g64_shuf2(&K->s25, o17a, g64_gbh(h19));
    o->o7 = g64_shuf2(&K->s25, o18a, g64_gbh(h7));
    o->o18 = g64_shuf2(&K->s30, o18a, g64_gbh(h5b));
    return 1;
}

static void g64_b_pk(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    const uint32_t r23w = g[23]._u32[0];
    const uint32_t n = (0u - r23w) / 64u;   /* iterations: r23 += 64 until 0 */
    if ((r23w & 63u) || r23w == 0 || n < 3 || n > SPU_LS_SIZE / 64) { s64_fb_v1[1]++; g64_b_v1(ctx); return; }
    /* the input records and the output quads must not overlap (see above) */
    {
        uint32_t ilo = ~0u, ihi = 0, olo = ~0u, ohi = 0;
        const int ib[4] = { 39, 33, 32, 29 }, ob[4] = { 66, 37, 35, 34 };
        for (int s = 0; s < 4; s++) {
            const uint32_t a = (g[ib[s]]._u32[0] + r23w) & (SPU_LS_MASK & ~0xFu);
            if (a + 64u * (n - 1) + 16u > SPU_LS_SIZE) { s64_fb_v1[1]++; g64_b_v1(ctx); return; }
            if (a < ilo) ilo = a;
            if (a + 64u * (n - 1) + 16u > ihi) ihi = a + 64u * (n - 1) + 16u;
        }
        /* r21 += r31 each iteration (word 0 is the store offset), r31 = shufb(r31, r31, r69). If r69 moves
         * whole words, every increment is one of r31's words at entry or 0: bound the offsets by that. */
        int64_t dlo = 0, dhi = 0;
        if (g64_ctl_words(g[69])) {
            int32_t mn = 0, mx = 0;
            for (int i = 0; i < 4; i++) { const int32_t v = (int32_t)g[31]._u32[i]; if (v < mn) mn = v; if (v > mx) mx = v; }
            dlo = (int64_t)mn * n; dhi = (int64_t)mx * n;
        } else {
            u128 r21 = g[21], r31 = g[31];
            int64_t acc = 0;
            for (uint32_t k = 0; k < n; k++) {
                r21 = spu_a(r21, r31);
                acc = (int32_t)(r21._u32[0] - g[21]._u32[0]);
                if (acc < dlo) dlo = acc;
                if (acc > dhi) dhi = acc;
                r31 = spu_shufb(r31, r31, g[69]);
            }
        }
        for (int s = 0; s < 4; s++) {
            const int64_t a0 = (int64_t)((g[ob[s]]._u32[0] + g[21]._u32[0]) & (SPU_LS_MASK & ~0xFu));
            if (a0 + dlo < 0 || a0 + dhi + 16 > SPU_LS_SIZE) { s64_fb_v1[1]++; g64_b_v1(ctx); return; }
            if ((uint32_t)(a0 + dlo) < olo) olo = (uint32_t)(a0 + dlo) & ~0xFu;
            if ((uint32_t)(a0 + dhi + 16) > ohi) ohi = (uint32_t)(a0 + dhi + 16);
        }
        if (g64_overlap(ilo, ihi - ilo, olo, ohi - olo)) { s64_fb_v1[1]++; g64_b_v1(ctx); return; }
    }
    g64_bk K;
    {
        static _Thread_local g64_shc sc[12];
        static _Thread_local int sv[12];
        g64_shuf_cached(&sc[0], &sv[0], g[38], &K.s38); g64_shuf_cached(&sc[1], &sv[1], g[59], &K.s59);
        g64_shuf_cached(&sc[2], &sv[2], g[27], &K.s27); g64_shuf_cached(&sc[3], &sv[3], g[78], &K.s78);
        g64_shuf_cached(&sc[4], &sv[4], g[79], &K.s79); g64_shuf_cached(&sc[5], &sv[5], g[80], &K.s80);
        g64_shuf_cached(&sc[6], &sv[6], g[70], &K.s70); g64_shuf_cached(&sc[7], &sv[7], g[75], &K.s75);
        g64_shuf_cached(&sc[8], &sv[8], g[28], &K.s28); g64_shuf_cached(&sc[9], &sv[9], g[25], &K.s25);
        g64_shuf_cached(&sc[10], &sv[10], g[30], &K.s30); g64_shuf_cached(&sc[11], &sv[11], g[69], &K.s69);
    }
    int spec = !s64_v2 && g64_is_splat(g[74], 0x80000000u);   /* q9 = W's sign: the xor keeps a value plain */
#define G64_BC(n) K.r##n = g64_bc(g[n]);
#define G64_BZ(n) K.r##n = g64_bc(g[n]); K.z##n = g64_zero(K.r##n); spec &= g64_plain_all(K.r##n);
    G64_BZ(55) G64_BZ(56) G64_BZ(48) G64_BZ(52) G64_BZ(47) G64_BZ(51) G64_BZ(46) G64_BZ(49) G64_BZ(54) G64_BZ(45)
    G64_BZ(44) G64_BZ(43) G64_BZ(53) G64_BZ(42) G64_BZ(41) G64_BZ(40) G64_BZ(77) G64_BZ(76) G64_BZ(73) G64_BZ(72)
    G64_BZ(68) G64_BZ(67) G64_BZ(71)
    G64_BC(64) G64_BC(60) G64_BC(57) G64_BC(63) G64_BC(62) G64_BC(61) G64_BC(58) G64_BC(74)
#undef G64_BC
#undef G64_BZ
    K.m50 = g64_bc(g[50]);
    K.i39 = g[39]._u32[0]; K.i33 = g[33]._u32[0]; K.i32 = g[32]._u32[0]; K.i29 = g[29]._u32[0];
    if (!spec && !s64_v2) s64_fb_chk[1]++;
    const uint32_t o66 = g[66]._u32[0], o37 = g[37]._u32[0], o35 = g[35]._u32[0], o34 = g[34]._u32[0];
    /* the previous pair's stage outputs; at entry their high halves are the registers the loop head set
     * (stage A of record -1: r24 r5 r65 r26 r36, stage B of record -2: r22 r6 r11 r12) */
    g64_bp pv, cu;
    pv.W = g64_bc(g[24]); pv.E = g64_bc(g[5]); pv.X = g64_bc(g[65]); pv.Y = g64_bc(g[26]); pv.Z = g64_bc(g[36]);
    pv.B22 = g64_bc(g[22]); pv.B6 = g64_bc(g[6]); pv.B11 = g64_bc(g[11]); pv.B12 = g64_bc(g[12]);
    u128 q21 = g[21], q31 = g[31], q23 = g[23];
    const uint32_t P = (n - 1) / 2;
    for (uint32_t p = 0; p < P; p++) {
        const uint32_t k0 = q23._u32[0];
        if (!spec || !g64_b_pair(&K, ls, k0, &pv, &cu, 1)) {
            if (spec) s64_redo[1]++;
            g64_b_pair(&K, ls, k0, &pv, &cu, 0);
        }
        /* the two iterations' stores, in the lift's order */
        q21 = spu_a(q21, q31);
        { const uint32_t q = q21._u32[0];
          g64_stlo(ls, o66 + q, cu.o17); g64_stlo(ls, o37 + q, cu.o10); g64_stlo(ls, o35 + q, cu.o7); g64_stlo(ls, o34 + q, cu.o18); }
        q31 = g64_shuf1(&K.s69, q31, q31);
        q21 = spu_a(q21, q31);
        { const uint32_t q = q21._u32[0];
          g64_sthi(ls, o66 + q, cu.o17); g64_sthi(ls, o37 + q, cu.o10); g64_sthi(ls, o35 + q, cu.o7); g64_sthi(ls, o34 + q, cu.o18); }
        q31 = g64_shuf1(&K.s69, q31, q31);
        q23 = spu_ai(spu_ai(q23, 64), 64);
        pv = cu;
    }
    s64_pk_iters[1] += 2 * (long)P;
    /* the rest through the lifted body, from the registers after iteration 2P - 1 */
    GOW_D6B_REGS()
    GOW_D6B_LOAD()
    r24 = g64_hi(pv.W); r5 = g64_hi(pv.E); r65 = g64_hi(pv.X); r26 = g64_hi(pv.Y); r36 = g64_hi(pv.Z);
    r22 = g64_hi(pv.B22); r6 = g64_hi(pv.B6); r11 = g64_hi(pv.B11); r12 = g64_hi(pv.B12);
    r21 = q21; r31 = q31; r23 = q23;
    do {
        GOW_D6B_BODY()
    } while (r3._u32[0] != 0);
    GOW_D6B_STORE()
}

/* =====================================================================================================
 * D92C packed. What the loop computes (from the lift; see EDGE_NATIVE.md): triangle culling with index
 * compaction, 4 triangles (12 halfword indices, 24 bytes) per iteration, software pipelined four deep.
 * Iteration j:
 *   S1  loads the index quads of the next group (r32 = LD(r59 + r47 + 24), r54 = LD(r58 + r47 + 24));
 *   S2  aligns the previous group's indices (shufb by r48, which alternates by xor r68 since 24-byte
 *       groups sit at alternating quad offsets), forms 12 vertex addresses r29 + 16 * index and gathers
 *       the 12 vertex quads; transposes them (shufb r37/r45/r49/r44/r36/r50) into the three vertices'
 *       x and y (r22/r42 r23/r40 r24/r43), the edges (fs: r17 r14 r16 r15) and the AND/OR of the
 *       vertices' outcode words (r53 = and & 2, r57 = and & 252, r56 = (or & 4) == 0);
 *   S3  (the group before) the signed area fms(r17, r15, fm(r16, r14)), its sign flipped by r63, > 0
 *       (fcgt); the six coordinates snapped (fma(x, r39, r38), cflts 173) and compared (ceq) for
 *       degenerate triangles; the outcode tests; a 4-bit cull mask (gb) | the tail mask r66 on the
 *       second-to-last iteration -> r5;
 *   S4  (the group before that) compacts the kept triangles' indices through LS shuffle tables indexed by
 *       the mask (r62/r60 + mask * 16) and the output alignment (r61 + (r41 << 4)), merges the first quad
 *       with what is in LS (LD(r82), selb mask from rotqmby(r71, -r41)) and stores three quads at the
 *       output position r33 (+ 6 bytes per kept triangle; past r65 the stores go to the scratch at r67);
 *       r46 counts the kept triangles. The first two iterations' S4 are masked (r31 = 0xF, 0xF, then 0).
 * Here S1, S2's address part, the gathers and S4 run per iteration exactly as the lift orders them (LS
 * reads and writes in the lift's order, the same helpers on the same operands); S2's compute and S3 run
 * 8 lanes wide over two iterations: a pair (j, j+1) runs S2 of (j, j+1) and S3 of (j+1, j+2), lane for
 * lane (S3 of iteration i reads S2 of i - 1), after both iterations' S4 (S4 of iteration i reads S3 of
 * i - 1, from the previous pair). The one fnms is cull-mask arithmetic on 0 / all-ones lanes (not plain
 * floats: the helpers take their exact per-lane path); its result is the helper's own for those operands,
 * computed once per loop entry. The last one or two iterations run the lifted body from the pairs' state.
 * ===================================================================================================== */
typedef struct {
    g64_shuf s37, s45, s49, s44, s36, s50, s48[2], s35;
    v8 r39, r38, z39, z38, r70, r63, k4, k252, k2;
    v8 nf[4];   /* fnms(a, r55, c) for a, c in {0, all ones}: [a * 2 + c] */
    u128 r35, r65, r66, r67, r68, r69, r71;
    uint32_t r29w, r58w, r59w, r60w, r61w, r62w;
} g64_ak;
typedef struct { u128 r3, r5, r8, r9, r10, r12, r30, r31, r32, r33, r34, r41, r46, r47, r48, r54, r64; int par; } g64_as;
typedef struct { u128 q[12]; u128 t12; } g64_ag;   /* one iteration's 12 gathered vertex quads (lift order), tail mask */

/* one iteration's S4, S1, S2 addresses and gathers: the lift's statements in its order (the numbers are
 * the statements' positions in the loop body) */
G64_AI void g64_a_serial(const g64_ak* K, uint8_t* ls, g64_as* s, g64_ag* o)
{
    const g64_shuf* s48 = &K->s48[s->par];                       /* r48 alternates between two values */
    const u128 r51s = spu_or(s->r31, s->r5);                     /* 0 */
    const u128 r8L = LD(K->r61w + s->r8._u32[0]);               /* 2 */
    const u128 r75 = spu_shli(r51s, 4);                          /* 3 */
    const u128 r51 = g64_shuf1(s48, s->r32, s->r32);             /* 4 */
    const u128 r82 = spu_a(s->r33, s->r9);                       /* 5 */
    const u128 r73 = spu_rotqmby(K->r71, s->r3);                 /* 6 */
    const u128 r52 = g64_shuf1(s48, s->r54, s->r32);             /* 7 */
    const u128 r77 = LD(r82._u32[0]);                            /* 8 */
    const u128 r28 = spu_cgt(r82, K->r65);                       /* 9 */
    const u128 r14 = LD(K->r62w + r75._u32[0]);                  /* 10 */
    const u128 r5m = spu_selb(K->r69, r8L, r73);                 /* 11 */
    const u128 r20 = LD(K->r60w + r75._u32[0]);                  /* 12 */
    const u128 r33t = spu_shlqbyi(s->r31, 4);                    /* 14 */
    const u128 r76 = spu_selb(r82, K->r67, r28);                 /* 15 */
    const u128 r32a = spu_ai(s->r47, 24);                        /* 16, 18: r47 */
    const u128 r78 = g64_shufv(s->r10, s->r12, r14);             /* 19 */
    const u128 r19 = g64_shufv(s->r12, s->r12, r20);             /* 21 */
    const u128 r18 = g64_shufv(r77, r78, r5m);                   /* 27 */
    const u128 r25 = g64_shufv(r78, r19, r8L);                   /* 31 */
    const u128 r12s = g64_shufv(r19, r19, r8L);                  /* 33 */
    s->r32 = LD(K->r59w + r32a._u32[0]);                         /* 37 */
    s->r54 = LD(K->r58w + r32a._u32[0]);                         /* 39 */
    ST(r82._u32[0], r18);                                        /* 41: ST(r33, r18) with r33 = r82 */
    ST(r76._u32[0] + 0x10, r25);                                 /* 43: ST(r4 + 0x10) with r4 = r76 */
    ST(r76._u32[1] + 0x20, r12s);                                /* 45: r23 = rotqbyi(r76, 4) */
    /* 49 .. 77, the gathers: the preferred words of r80 = roti(r52, 4) and r10 = roti(r51, 4) shifted
     * (shlqbyi 4/8/12: words 1/2/3) and rotated (roti 16), in the lift's order */
    {
        const uint32_t b = K->r29w;
        uint32_t w80[4], w10[2];
        for (int i = 0; i < 4; i++) w80[i] = (r52._u32[i] << 4) | (r52._u32[i] >> 28);
        for (int i = 0; i < 2; i++) w10[i] = (r51._u32[i] << 4) | (r51._u32[i] >> 28);
#define R16(x) (((x) << 16) | ((x) >> 16))
        o->q[0] = LD(b + w80[1]);       o->q[1] = LD(b + w80[3]);       o->q[2] = LD(b + R16(w80[0]));
        o->q[3] = LD(b + R16(w10[1]));  o->q[4] = LD(b + w10[1]);       o->q[5] = LD(b + R16(w80[3]));
        o->q[6] = LD(b + R16(w80[1]));  o->q[7] = LD(b + w80[0]);       o->q[8] = LD(b + w10[0]);
        o->q[9] = LD(b + w80[2]);       o->q[10] = LD(b + R16(w80[2])); o->q[11] = LD(b + R16(w10[0]));
#undef R16
    }
    const u128 r11 = g64_shuf1(&K->s35, spu_cntb(r75), K->r35); /* 50, 78 */
    s->r48 = spu_xor(s->r48, K->r68); s->par ^= 1;               /* 66 */
    const u128 r73k = spu_sfi(r11, 4);                           /* 100 */
    s->r9 = spu_mpyui(r73k, 6);                                  /* 110 */
    const u128 r2 = spu_ai(s->r34, -1);                          /* 112, 115: r34 */
    const u128 r78b = spu_a(s->r41, s->r9);                      /* 118 */
    const u128 r75b = spu_ceqi(r2, 1);                           /* 120 */
    s->r41 = spu_andi(r78b, 15);                                 /* 122 */
    o->t12 = spu_and(K->r66, r75b);                              /* 128 */
    s->r46 = spu_a(s->r46, r73k);                                /* 130, 133 */
    s->r8 = spu_shli(s->r41, 4);                                 /* 131 */
    s->r12 = s->r30; s->r10 = s->r64;                            /* 135, 136 */
    s->r3 = spu_sfi(s->r41, 0);                                  /* 138 */
    s->r64 = r52; s->r30 = r51;                                  /* 141, 142 */
    s->r31 = r33t; s->r33 = r82; s->r47 = r32a; s->r34 = r2;
}

/* S2's results for one or two iterations (the registers the lift leaves them in) */
typedef struct { v8 r22, r23, r24, r40, r42, r43, r14, r15, r16, r17, r53, r56, r57; } g64_a2;

/* S2 of (j, j+1) from their gathers (spec: speculative, the edges' results tracked in R) */
G64_AI void g64_a_s2(const g64_ak* K, const g64_ag* g0, const g64_ag* g1, g64_a2* o, g64_rng* R, int spec,
                     v8 zc[6], v8 ze[4])
{
#define G(k) g64_v8(g0->q[k], g1->q[k])
    const v8 g74 = G(0), g72 = G(1), g28 = G(2), g22 = G(3), g27 = G(4), g8 = G(5);
    const v8 g4 = G(6), g14 = G(7), g12 = G(8), g25 = G(9), g5 = G(10), g80 = G(11);
#undef G
    const v8 v7 = g64_shuf2(&K->s37, g28, g74), v26 = g64_shuf2(&K->s37, g72, g22);
    const v8 v23 = g64_shuf2(&K->s45, g72, g22), v18 = g64_shuf2(&K->s49, g28, g74);
    const v8 v2 = g64_shuf2(&K->s37, g8, g12), v43 = g64_shuf2(&K->s49, g4, g25);
    const v8 v24 = g64_shuf2(&K->s37, g14, g5), v40 = g64_shuf2(&K->s37, g4, g25);
    const v8 v21 = g64_shuf2(&K->s49, g14, g5), v79 = g64_shuf2(&K->s45, g8, g12);
    const v8 v6 = g64_shuf2(&K->s45, g80, g27), v78 = g64_shuf2(&K->s37, g80, g27);
    const v8 v80 = g64_shuf2(&K->s44, v21, v23), v79b = g64_shuf2(&K->s44, v18, v79);
    const v8 v76 = g64_shuf2(&K->s44, v43, v6);
    o->r43 = g64_shuf2(&K->s36, v24, v26); o->r24 = g64_shuf2(&K->s50, v24, v26);
    o->r22 = g64_shuf2(&K->s36, v7, v2);   o->r23 = g64_shuf2(&K->s50, v7, v2);
    o->r42 = g64_shuf2(&K->s36, v40, v78); o->r40 = g64_shuf2(&K->s50, v40, v78);
    const v8 v82 = g64_or(v79b, v80), v74 = g64_or(v76, v82), v77 = g64_and(v79b, v80);
    zc[0] = g64_xin(spec, R, o->r22); zc[1] = g64_xin(spec, R, o->r23); zc[2] = g64_xin(spec, R, o->r24);
    zc[3] = g64_xin(spec, R, o->r40); zc[4] = g64_xin(spec, R, o->r42); zc[5] = g64_xin(spec, R, o->r43);
    o->r17 = g64_xfs(spec, R, o->r24, o->r23, &ze[3]);
    o->r14 = g64_xfs(spec, R, o->r40, o->r23, &ze[0]);
    o->r16 = g64_xfs(spec, R, o->r43, o->r22, &ze[2]);
    o->r15 = g64_xfs(spec, R, o->r42, o->r22, &ze[1]);
    const v8 v27 = g64_and(v76, v77);
    o->r56 = g64_ceq(g64_and(v74, K->k4), _mm256_setzero_si256());
    o->r57 = g64_and(v27, K->k252);
    o->r53 = g64_and(v27, K->k2);
}

/* S3 of two iterations from S2 of the iterations before them (lane for lane) and their tail masks:
 * the cull masks r5. zc/ze: the zero lanes of S2's coordinates and edges (spec) */
G64_AI v8 g64_a_s3(const g64_ak* K, const g64_a2* a, v8 t12, g64_rng* R, int spec, const v8 zc[6], const v8 ze[4])
{
    v8 zm16, zm77, zd;
    const v8 m16 = g64_xfm(spec, R, a->r16, ze[2], a->r14, ze[0], &zm16);
    const v8 m77 = g64_xfms(spec, R, a->r17, ze[3], a->r15, ze[1], m16, zm16, &zm77);
    const v8 s17 = g64_xfma(spec, R, a->r23, zc[1], K->r39, K->z39, K->r38, K->z38, &zd);
    const v8 s78 = g64_xfma(spec, R, a->r40, zc[3], K->r39, K->z39, K->r38, K->z38, &zd);
    const v8 s20 = g64_xfma(spec, R, a->r24, zc[2], K->r39, K->z39, K->r38, K->z38, &zd);
    const v8 s15 = g64_xfma(spec, R, a->r22, zc[0], K->r39, K->z39, K->r38, K->z38, &zd);
    const v8 s19 = g64_xfma(spec, R, a->r43, zc[5], K->r39, K->z39, K->r38, K->z38, &zd);
    const v8 s76 = g64_xfma(spec, R, a->r42, zc[4], K->r39, K->z39, K->r38, K->z38, &zd);
    const v8 k42 = _mm256_cmpgt_epi32(a->r53, _mm256_setzero_si256());                      /* cgti 0 */
    const v8 k53 = _mm256_xor_si256(g64_ceq(a->r57, _mm256_setzero_si256()), _mm256_set1_epi32(-1));   /* clgti 0 */
    const v8 i82 = g64_cflts(s76, 173), i13 = g64_cflts(s78, 173), i73 = g64_cflts(s17, 173);
    const v8 i72 = g64_cflts(s19, 173), i74 = g64_cflts(s20, 173), i28 = g64_cflts(s15, 173);
    const v8 x16 = g64_xor(K->r63, m77);
    const v8 e77 = g64_ceq(i13, i73), e75 = g64_ceq(i82, i28), k25 = g64_and(a->r56, k42);
    const v8 e14 = g64_ceq(i74, i13), e12 = g64_ceq(i72, i82), f8 = g64_fcgt(x16, K->r70);
    const v8 e10 = g64_ceq(i73, i74), e56 = g64_ceq(i28, i72);
    const v8 a13 = g64_and(k25, f8);
    const v8 a20 = g64_and(g64_and(e77, e14), e10), a26 = g64_and(g64_and(e75, e12), e56);
    /* fnms(a13, r55, k53): a13 and k53 are compare masks (0 or all ones in every lane) */
    const v8 n19 = g64_selb(g64_selb(K->nf[0], K->nf[1], k53), g64_selb(K->nf[2], K->nf[3], k53), a13);
    const v8 a7 = g64_or(g64_and(g64_or(a20, a26), k25), n19);
    return g64_or(t12, g64_gb(a7));
}

/* S2 of (j, j+1) and S3 of (j+1, j+2); spec: speculative, 0 if the range check failed */
G64_AI int g64_a_pair(const g64_ak* K, const g64_ag* g0, const g64_ag* g1, v8 t12, g64_a2* o2, v8* r5, int spec)
{
    g64_rng R; g64_rng_init(&R);
    v8 zc[6], ze[4];
    g64_a_s2(K, g0, g1, o2, &R, spec, zc, ze);
    *r5 = g64_a_s3(K, o2, t12, &R, spec, zc, ze);
    return spec ? g64_rng_ok(&R) : 1;
}

static void g64_a_pk(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    const uint32_t n = g[34]._u32[0];   /* iterations: r2 = r34 - 1 until 0 */
    if (n < 3 || n > (1u << 20)) { s64_fb_v1[0]++; g64_a_v1(ctx); return; }
    g64_ak K;
    {
        static _Thread_local g64_shc sc[9];
        static _Thread_local int sv[9];
        g64_shuf_cached(&sc[0], &sv[0], g[37], &K.s37); g64_shuf_cached(&sc[1], &sv[1], g[45], &K.s45);
        g64_shuf_cached(&sc[2], &sv[2], g[49], &K.s49); g64_shuf_cached(&sc[3], &sv[3], g[44], &K.s44);
        g64_shuf_cached(&sc[4], &sv[4], g[36], &K.s36); g64_shuf_cached(&sc[5], &sv[5], g[50], &K.s50);
        g64_shuf_cached(&sc[6], &sv[6], g[48], &K.s48[0]);
        g64_shuf_cached(&sc[7], &sv[7], spu_xor(g[48], g[68]), &K.s48[1]);
        g64_shuf_cached(&sc[8], &sv[8], g[35], &K.s35);
    }
    K.r39 = g64_bc(g[39]); K.r38 = g64_bc(g[38]); K.z39 = g64_zero(K.r39); K.z38 = g64_zero(K.r38);
    K.r70 = g64_bc(g[70]); K.r63 = g64_bc(g[63]);
    K.k4 = _mm256_set1_epi32(4); K.k252 = _mm256_set1_epi32(252); K.k2 = _mm256_set1_epi32(2);
    K.r35 = g[35]; K.r65 = g[65]; K.r66 = g[66]; K.r67 = g[67]; K.r68 = g[68]; K.r69 = g[69]; K.r71 = g[71];
    K.r29w = g[29]._u32[0]; K.r58w = g[58]._u32[0]; K.r59w = g[59]._u32[0];
    K.r60w = g[60]._u32[0]; K.r61w = g[61]._u32[0]; K.r62w = g[62]._u32[0];
    /* the fnms table needs r55 to be 0 or all ones in every lane */
    for (int i = 0; i < 4; i++)
        if (g[55]._u32[i] != 0 && g[55]._u32[i] != 0xFFFFFFFFu) { s64_fb_v1[0]++; g64_a_v1(ctx); return; }
    {
        u128 z0, z1; memset(&z0, 0, 16); memset(&z1, 0xFF, 16);
        K.nf[0] = g64_bc(spu_fnms(z0, g[55], z0)); K.nf[1] = g64_bc(spu_fnms(z0, g[55], z1));
        K.nf[2] = g64_bc(spu_fnms(z1, g[55], z0)); K.nf[3] = g64_bc(spu_fnms(z1, g[55], z1));
    }
    const int spec = !s64_v2 && g64_plain_all(K.r39) && g64_plain_all(K.r38);
    if (!spec && !s64_v2) s64_fb_chk[0]++;
    g64_as S;
#define G64_S(n) S.r##n = g[n];
    G64_S(3) G64_S(5) G64_S(8) G64_S(9) G64_S(10) G64_S(12) G64_S(30) G64_S(31) G64_S(32) G64_S(33) G64_S(34)
    G64_S(41) G64_S(46) G64_S(47) G64_S(48) G64_S(54) G64_S(64)
#undef G64_S
    S.par = 0;
    /* S3 of iteration 0 reads the registers the loop head set (S2 of "iteration -1"): checked ops */
    v8 r5p;   /* low: S3 of iteration j - 1, high: of j */
    {
        g64_a2 a0;
        a0.r22 = g64_bc(g[22]); a0.r23 = g64_bc(g[23]); a0.r24 = g64_bc(g[24]); a0.r40 = g64_bc(g[40]);
        a0.r42 = g64_bc(g[42]); a0.r43 = g64_bc(g[43]); a0.r14 = g64_bc(g[14]); a0.r15 = g64_bc(g[15]);
        a0.r16 = g64_bc(g[16]); a0.r17 = g64_bc(g[17]); a0.r53 = g64_bc(g[53]); a0.r56 = g64_bc(g[56]);
        a0.r57 = g64_bc(g[57]);
        const v8 t12 = g64_bc(spu_and(K.r66, spu_ceqi(spu_ai(S.r34, -1), 1)));
        v8 zc[6], ze[4];
        for (int i = 0; i < 6; i++) zc[i] = _mm256_setzero_si256();
        for (int i = 0; i < 4; i++) ze[i] = _mm256_setzero_si256();
        const v8 r5_0 = g64_a_s3(&K, &a0, t12, NULL, 0, zc, ze);
        r5p = g64_v8(g[5], g64_lo(r5_0));
    }
    g64_a2 a2;
    const uint32_t P = (n - 1) / 2;
    for (uint32_t p = 0; p < P; p++) {
        g64_ag g0, g1;
        S.r5 = g64_lo(r5p); g64_a_serial(&K, ls, &S, &g0);
        S.r5 = g64_hi(r5p); g64_a_serial(&K, ls, &S, &g1);
        /* S3 of j+1 takes iteration j+1's tail mask, S3 of j+2 the one iteration j+2 will form */
        const v8 t12 = g64_v8(g1.t12, spu_and(K.r66, spu_ceqi(spu_ai(S.r34, -1), 1)));
        if (!spec || !g64_a_pair(&K, &g0, &g1, t12, &a2, &r5p, 1)) {
            if (spec) s64_redo[0]++;
            g64_a_pair(&K, &g0, &g1, t12, &a2, &r5p, 0);
        }
    }
    s64_pk_iters[0] += 2 * (long)P;
    /* the rest through the lifted body, from the registers after iteration 2P - 1 */
    GOW_D6A_REGS()
    GOW_D6A_LOAD()
#define G64_S(n) r##n = S.r##n;
    G64_S(3) G64_S(8) G64_S(9) G64_S(10) G64_S(12) G64_S(30) G64_S(31) G64_S(32) G64_S(33) G64_S(34)
    G64_S(41) G64_S(46) G64_S(47) G64_S(48) G64_S(54) G64_S(64)
#undef G64_S
    r5 = g64_lo(r5p);
#define G64_H(n) r##n = g64_hi(a2.r##n);
    G64_H(22) G64_H(23) G64_H(24) G64_H(40) G64_H(42) G64_H(43) G64_H(14) G64_H(15) G64_H(16) G64_H(17)
    G64_H(53) G64_H(56) G64_H(57)
#undef G64_H
    for (;;) {
        GOW_D6A_BODY()
        if (r2._u32[0] == 0) break;
        GOW_D6A_TAIL()
    }
    GOW_D6A_STORE()
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s64_mode = -1;

int gow_edge_d640_mode(void)
{
    if (s64_mode < 0) {
        const char* e = getenv("GOW_EDGE_D640");
        int m = e && *e ? atoi(e) : 1;   /* default: native packed v3 */
        if (m < 0 || m > 4) m = 1;
        const char* v = getenv("GOW_EDGE_D640_V2");
        s64_v2 = v && atoi(v) == 1;
        s64_mode = m;
        g64_luts();
        if (!m) fprintf(stderr, "[edge-d640] GOW_EDGE_D640=0 (lifted loops)\n");
        else    fprintf(stderr, "[edge-d640] GOW_EDGE_D640=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                        (m == 2 || m == 4) ? "v1" : s64_v2 ? "packed v2" : "packed v3");
    }
    return s64_mode;
}

static int s64_fault = -1;
static void g64_fault(spu_context* ctx, int region)   /* region 0: D92C, 1: D4DC */
{
    if (s64_fault < 0) { const char* f = getenv("GOW_EDGE_D640_FAULT"); s64_fault = f ? atoi(f) : 0; }
    if (s64_fault <= 0 || (s64_fault - 1) / 2 != region) return;
    const u128* r = ctx->gpr;
    switch (s64_fault) {
    case 1: ctx->ls[((r[33]._u32[0]) & (SPU_LS_MASK & ~0xFu)) + 15] ^= 1; break;                  /* ST(r33, r18) */
    case 2: ctx->gpr[5]._u32[0] ^= 1; break;
    case 3: ctx->ls[((r[66]._u32[0] + r[21]._u32[0]) & (SPU_LS_MASK & ~0xFu)) + 15] ^= 1; break;  /* ST(r66 + r21, r17) */
    case 4: ctx->gpr[24]._u32[0] ^= 1; break;
    }
    fprintf(stderr, "[edge-d640] fault %d injected (%s)\n", s64_fault, region ? "D4DC" : "D92C");
    s64_fault = 0;
}

static void g64_a_run(spu_context* ctx)
{
    if (s64_mode == 2 || s64_mode == 4) g64_a_v1(ctx);
    else                                g64_a_pk(ctx);
    if (__builtin_expect(s64_fault != 0, 0)) g64_fault(ctx, 0);
}
static void g64_b_run(spu_context* ctx)
{
    if (s64_mode == 2 || s64_mode == 4) g64_b_v1(ctx);
    else                                g64_b_pk(ctx);
    if (__builtin_expect(s64_fault != 0, 0)) g64_fault(ctx, 1);
}

/* Modes 1 and 2: replace the loop; the lift then jumps to its exit. */
void gow_edge_d640_d92c(spu_context* ctx) { g64_a_run(ctx); }
void gow_edge_d640_d4dc(spu_context* ctx) { g64_b_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. loc_DB70 is
 * also reached by the skip in the D6C0 block (r7 not 1, 2 or 3), hence `pending`. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} g64_check_state;
static _Thread_local g64_check_state* t64_chk[2];

static const char* const k64_name[2] = { "D92C", "D4DC" };
static long s64_calls[2], s64_bad[2], s64_iters[2];
static uint64_t s64_cyc_nat[2], s64_cyc_lift[2];
static int s64_atexit;

static void g64_check_report(void)
{
    for (int k = 0; k < 2; k++)
        fprintf(stderr, "[edge-d640-check] %s: %ld calls checked, %ld differ; %ld iterations; tsc: native %.1f M, "
                        "lifted %.1f M (%.1f vs %.1f per iteration)\n",
                k64_name[k], s64_calls[k], s64_bad[k], s64_iters[k], s64_cyc_nat[k] / 1e6, s64_cyc_lift[k] / 1e6,
                s64_iters[k] ? (double)s64_cyc_nat[k] / (double)s64_iters[k] : 0.0,
                s64_iters[k] ? (double)s64_cyc_lift[k] / (double)s64_iters[k] : 0.0);
    fprintf(stderr, "[edge-d640-check] packed: D92C %ld, D4DC %ld iterations; entries left to v1: D92C %ld, D4DC %ld; "
                    "entries on the checked ops (invariants not plain): D92C %ld, D4DC %ld; speculative pairs recomputed: "
                    "D92C %ld, D4DC %ld; 8-lane ops redone by the 4-lane helpers: %ld\n",
            s64_pk_iters[0], s64_pk_iters[1], s64_fb_v1[0], s64_fb_v1[1], s64_fb_chk[0], s64_fb_chk[1],
            s64_redo[0], s64_redo[1], s64_fp_slow);
}

static void g64_check_begin(spu_context* ctx, int k)
{
    g64_check_state* c = t64_chk[k];
    if (!c) {
        c = (g64_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t64_chk[k] = c;
    }
    if (!s64_atexit) { s64_atexit = 1; atexit(g64_check_report); }
    const u128* r = ctx->gpr;
    /* D92C: r34 counts down to 1 (r2 = r34 - 1 is the exit test); D4DC: r23 counts up to 0 by 64 */
    s64_iters[k] += k == 0 ? (long)r[34]._u32[0] : (long)((0u - r[23]._u32[0]) / 64u);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    if (k == 0) g64_a_run(ctx); else g64_b_run(ctx);
    s64_cyc_nat[k] += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

static void g64_check_end(spu_context* ctx, int k)
{
    g64_check_state* c = t64_chk[k];
    if (!c || !c->pending) return;
    s64_cyc_lift[k] += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s64_calls[k]++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-d640-check] %s call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    k64_name[k], call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-d640-check] %s call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                k64_name[k], call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s64_bad[k]++;
        fprintf(stderr, "[edge-d640-check] %s call %ld: %d GPRs and %ld LS bytes differ\n", k64_name[k], call, bad, nls);
    }
    if ((call & 4095) == 4095) g64_check_report();   /* a game run ends by kill: atexit never runs */
}

void gow_edge_d640_d92c_check_begin(spu_context* ctx) { g64_check_begin(ctx, 0); }
void gow_edge_d640_d92c_check_end(spu_context* ctx)   { g64_check_end(ctx, 0); }
void gow_edge_d640_d4dc_check_begin(spu_context* ctx) { g64_check_begin(ctx, 1); }
void gow_edge_d640_d4dc_check_end(spu_context* ctx)   { g64_check_end(ctx, 1); }

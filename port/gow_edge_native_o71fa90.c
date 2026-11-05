/* gow_edge_native_o71fa90.c -- native replacement for the loop of EDGE overlay blob 71fa90 (input decode,
 * family A in work/edgefrag_scope.md): the loop at blob+0x2E8, in every placement lift of the blob
 * (spu/edgefrag_71fa90_<lsa>; 46 placements, one lift each). gen_edge_frag_lift.py puts a switch before the
 * loop in each of them (checking that each placement's loop is the reference 12210's, statement for
 * statement); GOW_EDGE_O71FA90 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native_o719900.c (-O2 -march=x86-64-v3 -ffp-contract=off,
 * SPU_FP_NATIVE=1, SPU_FP_CHECK_HOOK=0). Runs inside spu_run_with_halt, i.e. with MXCSR rounding toward zero
 * and FTZ/DAZ, as the lift does.
 *
 * Clean-room: written from our own lift of the fragment and our own captures.
 *
 * What the loop does (a reading of the lift; 2 vertices per iteration, r26 a negative byte offset stepping by
 * 32 to 0, r71 = r26 + 32 the back-edge condition): input records of 18 bytes per vertex, a pair of them (36
 * bytes) per iteration at r35 (+36 per iteration; read three quads at a time through the unaligned-window
 * shufb with r62 + splat(r35 & 15)), software pipelined (iteration i loads pair i + 2, stores the position
 * slot of pair i and the two decoded slots of pair i - 1; the loop's exit tail stores the last pair's).
 *   - bytes 0-11 of each vertex (three floats) to the slot at r61 / r57 = r61 + 16 with w from r60 (selb r70);
 *   - bytes 12-14 and 15-17 of each vertex: two 24-bit packed vectors. Per vector, two 10-bit fields
 *     (rotm by r43, under r41) put under an exponent (selb r42) and scaled, fma(r55, f, r54), and a 4-bit
 *     table index (r41 word 0: the byte offset of a 16-byte entry of the table the prologue stores at r40 =
 *     r1 + 32). The two vectors of both vertices are decoded as [xA yA xB yB] per vector (shufb r64), squared
 *     (fm), summed with the next word (shlqbyi 4, fa), d = fs(r56, sum), z = fm(d, fi(d, frsqest(d))): sqrt(1
 *     - x^2 - y^2) to the estimate's precision. The table entry (shufb control words, sign bits and a w)
 *     places x, y and z: out = shufb(selb(z, T, r32), [xA yA xB yB], selb(r31, T, r30) (+ r52 for vertex B)),
 *     stored to the slots at r50 / r58 (first vector) and r51 / r59 (second).
 *
 * v1: the loop statement for statement in C locals (GOW_O71F_BODY, generated from the reference lift at
 * build time), as one C loop, with direct LS access. Every op is the lift's op on the same operands in the
 * same order, so it is exact for every input.
 *
 * v3 (packed): one pair of records (2 vertices, both vectors: 8 lanes [xA1 yA1 xB1 yB1 | xA2 yA2 xB2 yB2])
 * per step, computed straight from the record bytes. Integer work (field extraction, table lookup, selb,
 * shufb with its special selectors) is exact by construction; fma, fm, fa and fs are the native ops with the
 * helpers' lane rule (a pair with a failing lane is redone by the helpers), frsqest and fi are the helpers'
 * integer formulas eight lanes wide (compared with the helpers in the self-test). Iterations 0-2 run lifted
 * from the entry state (they store what depends on the prologue's registers), pairs 3 .. n-1 (positions)
 * and 2 .. n-2 (decoded) packed, then iterations n-4 .. n-2 lifted again with the pointers set back and no
 * stores (the state converges), and the last one lifted with its stores: every register the loop writes comes
 * out exactly. Entry checks, else v1 (each counted): 8 .. 8192 iterations (r26 a negative multiple of 32);
 * the controls and masks equal the captures' (GOW_O71F_CTL; the layout above rests on them; r54 r55 r56 are
 * used as they are); r57 r58 r59 at r61 r50 r51 + 16; the slot pointers 16-byte aligned; the record stream,
 * the table and the three slot arrays inside the LS without wrapping, and the slot arrays apart from each
 * other, from the stream and from the table.
 *
 * GOW_EDGE_O71FA90 (read once):
 *   unset/0  lifted loop (the default)
 *   1        native v3 (v1 for the entries it does not take)
 *   2        native v1
 *   3        check v3: run it on a copy of the state, then the lifted loop, and compare all GPRs and the
 *            whole LS at the loop exit; `[edge-o71fa90-check] N calls checked, M differ` every 4096 calls and
 *            at exit, with the calls per placement
 *   4        check v1, the same way
 * GOW_EDGE_O71FA90_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 3 of the quad the last iteration stores at r50 (the LSB of a decoded x), 3 bit 0 of byte 0 of that
 * quad, 2 bit 0 of r15 (the carried z the exit tail stores) after the loop, 4 bit 0 of byte 3 of pair 3's
 * quad at r51 (a packed one when v3 took the call).
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
static inline __m128i g7f_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 g7f_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = g7f_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void g7f_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g7f_bswap32x4(x));
}
#define LD(a)    g7f_ld(ls, (a))
#define ST(a, v) g7f_st(ls, (a), (v))

/* the lifted loop: GOW_O71F_*. Game code, so it is not in the repo: gen_edge_frag_lift.py --body writes it
 * from the reference lift at build time (no LS address of its own, straight code: checked there). */
#include "gow_edge_o71fa90_body.h"

/* v1: the whole loop (back edge on r71) */
static void g7f_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O71F_REGS()
    GOW_O71F_LOAD()
    do {
        GOW_O71F_BODY()
    } while (r71._u32[0] != 0);
    GOW_O71F_STORE()
}

/* the lifted body for `count` iterations, the first `nost` of them without their stores (v3: the first
 * three iterations with their stores; the warm-up, its registers only (the loads still read the LS), and
 * the last iteration with its stores). */
#undef ST
#define ST(a, v) do { if (i >= nost) g7f_st(ls, (a), (v)); } while (0)
static void g7f_iters(spu_context* ctx, uint32_t count, uint32_t nost)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O71F_REGS()
    GOW_O71F_LOAD()
    for (uint32_t i = 0; i < count; i++) {
        GOW_O71F_BODY()
    }
    GOW_O71F_STORE()
}
#undef ST
#define ST(a, v) g7f_st(ls, (a), (v))

/* =====================================================================================================
 * v3: one pair of records per step, 8 lanes: [xA1 yA1 xB1 yB1] (the first vector of both vertices, as the
 * lift's r72) in the low 128-bit lane, [xA2 yA2 xB2 yB2] (r73) in the high one.
 * ===================================================================================================== */
typedef __m256i v8;
#define G7F_AI static inline __attribute__((always_inline))

static inline v8 g7f_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 g7f_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(a); }
static inline u128 g7f_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 g7f_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline __m256 g7f_p(v8 x) { return _mm256_castsi256_ps(x); }
static inline v8 g7f_f(__m256 x) { return _mm256_castps_si256(x); }

/* The SPU_FP_NATIVE helpers' lane rule (spu_helpers.h spu__xn_fa/fm/fma with SPU_FP_DAZ): a lane is kept
 * when the native result has exponent 1..253 or is a zero (made +0); `ok` collects the lanes that pass. */
G7F_AI v8 g7f_keep(v8 r, v8* ok)
{
    const v8 r1 = _mm256_slli_epi32(r, 1);
    const v8 e = _mm256_srli_epi32(r1, 24), z = _mm256_cmpeq_epi32(r1, _mm256_setzero_si256());
    const v8 n = _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
    *ok = _mm256_and_si256(*ok, _mm256_or_si256(n, z));
    return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r);
}
G7F_AI v8 g7f_fma(v8 a, v8 b, v8 c, v8* ok) { return g7f_keep(g7f_f(_mm256_fmadd_ps(g7f_p(a), g7f_p(b), g7f_p(c))), ok); }
G7F_AI v8 g7f_fm(v8 a, v8 b, v8* ok)        { return g7f_keep(g7f_f(_mm256_mul_ps(g7f_p(a), g7f_p(b))), ok); }
G7F_AI v8 g7f_fa(v8 a, v8 b, v8* ok)        { return g7f_keep(g7f_f(_mm256_add_ps(g7f_p(a), g7f_p(b))), ok); }
G7F_AI v8 g7f_fs(v8 a, v8 b, v8* ok)
{
    return g7f_keep(g7f_f(_mm256_add_ps(g7f_p(a), g7f_p(_mm256_xor_si256(b, _mm256_set1_epi32((int)0x80000000u))))), ok);
}

/* frsqest, 8 lanes: the helper's integer formula (exponent 190 - (e + 1) / 2, 255 for e = 0; fraction from
 * its 64-entry table by bits 18-23; no sign) */
G7F_AI v8 g7f_frsqest(v8 x)
{
    const v8 e = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(x, 18), _mm256_set1_epi32(0x3F));
    const v8 frac = _mm256_and_si256(_mm256_i32gather_epi32((const int*)spu_frsqest_fraction_lut, idx, 4),
                                     _mm256_set1_epi32(0x7FFFFF));
    v8 re = _mm256_sub_epi32(_mm256_set1_epi32(190), _mm256_srli_epi32(_mm256_add_epi32(e, _mm256_set1_epi32(1)), 1));
    re = _mm256_blendv_epi8(re, _mm256_set1_epi32(255), _mm256_cmpeq_epi32(e, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_slli_epi32(re, 23), frac);
}
/* fi, 8 lanes: the helper's integer formula (spu_helpers.h spu_fi, AVX2 path) */
G7F_AI v8 g7f_fi(v8 a, v8 b)
{
    const v8 y = _mm256_and_si256(a, _mm256_set1_epi32(0x7FFFF));
    const v8 exp = _mm256_and_si256(_mm256_srli_epi32(b, 23), _mm256_set1_epi32(0xFF));
    const v8 b19 = _mm256_slli_epi32(_mm256_and_si256(_mm256_srli_epi32(b, 10), _mm256_set1_epi32(0x1FFF)), 19);
    const v8 sy = _mm256_mullo_epi32(_mm256_and_si256(b, _mm256_set1_epi32(0x3FF)), y);
    const v8 big = _mm256_cmpeq_epi32(_mm256_max_epu32(b19, sy), b19);
    const v8 d = _mm256_sub_epi32(b19, sy);
    const v8 frac = _mm256_and_si256(_mm256_blendv_epi8(_mm256_srli_epi32(d, 8), _mm256_srli_epi32(d, 9), big),
                                     _mm256_set1_epi32(0x7FFFFF));
    const v8 e2 = _mm256_sub_epi32(exp, _mm256_andnot_si256(big, _mm256_set1_epi32(1)));
    const v8 r = _mm256_or_si256(_mm256_or_si256(_mm256_and_si256(b, _mm256_set1_epi32((int)0x80000000u)),
                                                 _mm256_slli_epi32(e2, 23)), frac);
    const v8 zero = _mm256_or_si256(_mm256_cmpeq_epi32(exp, _mm256_setzero_si256()), _mm256_cmpeq_epi32(e2, _mm256_setzero_si256()));
    return _mm256_andnot_si256(zero, r);
}
/* shufb per 128-bit lane, host word order (the helper's way: control as it is, source index (c & 15) ^ 3;
 * special selectors 10x -> 0x00, 110 -> 0xFF, 111 -> 0x80) */
G7F_AI v8 g7f_shufb(v8 a, v8 b, v8 c)
{
    const v8 idx = _mm256_xor_si256(_mm256_and_si256(c, _mm256_set1_epi8(0x0F)), _mm256_set1_epi8(0x03));
    const v8 fromb = _mm256_cmpeq_epi8(_mm256_and_si256(c, _mm256_set1_epi8(0x10)), _mm256_set1_epi8(0x10));
    v8 r = _mm256_blendv_epi8(_mm256_shuffle_epi8(a, idx), _mm256_shuffle_epi8(b, idx), fromb);
    const v8 mC0 = _mm256_cmpeq_epi8(_mm256_and_si256(c, _mm256_set1_epi8((char)0xC0)), _mm256_set1_epi8((char)0xC0));
    const v8 mE0 = _mm256_cmpeq_epi8(_mm256_and_si256(c, _mm256_set1_epi8((char)0xE0)), _mm256_set1_epi8((char)0xE0));
    const v8 special = _mm256_blendv_epi8(mC0, _mm256_set1_epi8((char)0x80), mE0);
    return _mm256_blendv_epi8(r, special, c);   /* blendv picks by bit 7 of each control byte */
}
G7F_AI v8 g7f_selb(v8 a, v8 b, v8 m) { return _mm256_or_si256(_mm256_andnot_si256(m, a), _mm256_and_si256(b, m)); }

typedef struct g7f_inv_ {
    v8 c55, c54, c56;          /* fma operands and the 1.0 of fs (broadcast) */
    v8 sh, msk, ex;            /* per lane the field shifts (rotm counts), masks and exponent bits (r43 r41 r42 words 1, 2) */
    v8 m32, c31, m30, a52;     /* selb masks and the control base; a52 = [0 | r52] */
    uint32_t sh0, msk0, ex0;   /* the table index word (word 0 of r43 r41 r42) */
    uint32_t w60;              /* the position slot's w (r60 word 3) */
    uint32_t in;               /* LS address of pair 0 (r35 - 72 at entry) */
    uint32_t tab;              /* r40 */
    uint32_t o61, o57, o50, o58, o51, o59;   /* the slot pointers + the entry r26 */
} g7f_inv;

/* the table entry for a packed vector's index word (bytes b0 b0 t b0, as the lift's shufb builds it) */
G7F_AI u128 g7f_tab(const uint8_t* ls, const g7f_inv* I, uint8_t b0, uint8_t t)
{
    const uint32_t w = ((uint32_t)b0 << 24) | ((uint32_t)b0 << 16) | ((uint32_t)t << 8) | b0;
    const uint32_t off = (I->ex0 & ~I->msk0) | ((I->sh0 < 32 ? w >> I->sh0 : 0u) & I->msk0);
    return g7f_ld(ls, I->tab + off);
}

/* the helpers' path for one pair's floats (a lane failed the rule): the lift's ops on u128 */
static long s7f_fp_slow;   /* pairs whose floats went to the helpers (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) void g7f_floats_slow(const g7f_inv* I, v8 F, v8* V, v8* Z)
{
    s7f_fp_slow++;
    u128 v[2], z[2];
    const u128 c55 = g7f_lo(I->c55), c54 = g7f_lo(I->c54), c56 = g7f_lo(I->c56);
    for (int h = 0; h < 2; h++) {
        const u128 f = h ? g7f_hi(F) : g7f_lo(F);
        v[h] = spu_fma(c55, f, c54);
        const u128 sq = spu_fm(v[h], v[h]);
        const u128 d = spu_fs(c56, spu_fa(sq, spu_shlqbyi(sq, 4)));
        z[h] = spu_fm(d, spu_fi(d, spu_frsqest(d)));
    }
    *V = g7f_v8(v[0], v[1]);
    *Z = g7f_v8(z[0], z[1]);
}

static const uint8_t g7f_bsw_c[32] = { 3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12, 3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12 };

/* pair p: the position slot (pos) and the two decoded slots (dec); the floats checked before anything is
 * stored, and redone by the helpers if a lane fails */
G7F_AI void g7f_pair(uint8_t* ls, const g7f_inv* I, uint32_t p, int pos, int dec)
{
    const uint8_t* q = ls + I->in + 36u * p;
    const uint32_t o = 32u * p;
    if (pos) {
        const __m128i a = _mm_insert_epi32(_mm_loadu_si128((const __m128i*)q), (int)__builtin_bswap32(I->w60), 3);
        const __m128i b = _mm_insert_epi32(_mm_loadu_si128((const __m128i*)(q + 18)), (int)__builtin_bswap32(I->w60), 3);
        _mm_storeu_si128((__m128i*)(ls + I->o61 + o), a);
        _mm_storeu_si128((__m128i*)(ls + I->o57 + o), b);
    }
    if (!dec) return;
    /* the eight 10-bit fields: per vertex lane [x1 y1 x2 y2] as the 16-bit big-endian pairs (t0 t1, t1 t2) of
     * its two 3-byte groups, then the lift's rotm, mask and exponent, then [A1 B1 | A2 B2] */
    const v8 raw = _mm256_set_m128i(_mm_loadu_si128((const __m128i*)(q + 20)), _mm_loadu_si128((const __m128i*)(q + 4)));
    const v8 w = _mm256_shuffle_epi8(raw, _mm256_setr_epi8(9,8,-128,-128, 10,9,-128,-128, 12,11,-128,-128, 13,12,-128,-128,
                                                           11,10,-128,-128, 12,11,-128,-128, 14,13,-128,-128, 15,14,-128,-128));
    const v8 fv = _mm256_or_si256(I->ex, _mm256_and_si256(_mm256_srlv_epi32(w, I->sh), I->msk));
    const v8 F = _mm256_permutevar8x32_epi32(fv, _mm256_setr_epi32(0, 1, 4, 5, 2, 3, 6, 7));
    v8 ok = _mm256_set1_epi32(-1);
    v8 V = g7f_fma(I->c55, F, I->c54, &ok);
    const v8 sq = g7f_fm(V, V, &ok);
    const v8 d = g7f_fs(I->c56, g7f_fa(sq, _mm256_bsrli_epi128(sq, 4), &ok), &ok);
    v8 Z = g7f_fm(d, g7f_fi(d, g7f_frsqest(d)), &ok);
    if (__builtin_expect(_mm256_movemask_ps(g7f_p(ok)) != 0xFF, 0)) g7f_floats_slow(I, F, &V, &Z);
    /* the table entries: A1 (index byte 14, filler byte 0), B1 (32, 16), A2 (17, 0), B2 (35, 32) */
    const v8 T1 = g7f_v8(g7f_tab(ls, I, q[0], q[14]), g7f_tab(ls, I, q[16], q[32]));
    const v8 T2 = g7f_v8(g7f_tab(ls, I, q[0], q[17]), g7f_tab(ls, I, q[32], q[35]));
    const v8 V1 = _mm256_permute2x128_si256(V, V, 0x00), V2 = _mm256_permute2x128_si256(V, V, 0x11);
    const v8 Z1 = _mm256_permute2x128_si256(Z, Z, 0x00), Z2 = _mm256_permute2x128_si256(Z, Z, 0x11);
    const v8 C1 = _mm256_add_epi32(g7f_selb(I->c31, T1, I->m30), I->a52);
    const v8 C2 = _mm256_add_epi32(g7f_selb(I->c31, T2, I->m30), I->a52);
    const v8 bsw = _mm256_loadu_si256((const __m256i*)g7f_bsw_c);
    const v8 O1 = _mm256_shuffle_epi8(g7f_shufb(g7f_selb(Z1, T1, I->m32), V1, C1), bsw);
    const v8 O2 = _mm256_shuffle_epi8(g7f_shufb(g7f_selb(Z2, T2, I->m32), V2, C2), bsw);
    _mm_storeu_si128((__m128i*)(ls + I->o50 + o), _mm256_castsi256_si128(O1));
    _mm_storeu_si128((__m128i*)(ls + I->o58 + o), _mm256_extracti128_si256(O1, 1));
    _mm_storeu_si128((__m128i*)(ls + I->o51 + o), _mm256_castsi256_si128(O2));
    _mm_storeu_si128((__m128i*)(ls + I->o59 + o), _mm256_extracti128_si256(O2, 1));
}

/* the controls and masks the layout rests on: GOW_O71F_CTL, {register, its four SPU words} as the prologue
 * leaves them. Game data, so not in the repo: gen_edge_frag_lift.py --consts computes them at build time from
 * the reference lift's prologue and the blob's bytes in the EBOOT. They are: r62 the identity control (the
 * unaligned window) and r69 its splat control; r68 r65 r66 r67 the shufb controls that gather the 3-byte
 * groups into words; r43 r41 r42 the rotm counts, field masks and exponent bits; r64 the control that pairs
 * the fields as [xA yA xB yB]; r30 r31 r32 r52 the masks, control base and vertex-B offset that turn a table
 * entry into the output control; r60 r70 the position slot's w and its mask. GOW_O71F_FMA (r55 r54 r56) and
 * GOW_O71F_TAB (the table the prologue stores at r40) are used by the self-test only. */
#include "gow_edge_o71fa90_consts.h"
static const struct { uint8_t r; uint32_t w[4]; } g7f_ctl[GOW_O71F_NCTL] = GOW_O71F_CTL;
#define G7F_NCTL GOW_O71F_NCTL

#define G7F_NMIN 8u
#define G7F_NMAX 8192u
static long s7f_fb_short;    /* entries with fewer than 8 iterations (or an implausible count): v1 */
static long s7f_fb_ctl;      /* entries with other controls, masks or slot pointer pairs: v1 */
static long s7f_fb_rng;      /* entries with a range outside the LS or unaligned slot pointers: v1 */
static long s7f_fb_ovl;      /* entries whose slot arrays overlap each other, the stream or the table: v1 */
static long s7f_pk_pairs;    /* pairs decoded packed */
static long s7f_v3_calls;    /* entries v3 took */
static uint64_t s7f_cyc_pk;  /* tsc in the packed pairs (lab statistic) */
static uint32_t s7f_last_p3; /* LS address of the last packed call's pair-3 quad at r51 (fault 4), or ~0 */

static inline u128 g7f_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}
static inline int g7f_apart(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a1 <= b0 || b1 <= a0; }

static void g7f_v3(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    s7f_last_p3 = ~0u;
    const uint32_t w26 = g[26]._u32[0];
    const uint32_t nb = 0u - w26, n = nb / 32u;
    if (!w26 || nb % 32u || n < G7F_NMIN || n > G7F_NMAX) { s7f_fb_short++; g7f_v1(ctx); return; }
    for (int i = 0; i < G7F_NCTL; i++)
        if (memcmp(g[g7f_ctl[i].r]._u32, g7f_ctl[i].w, 16)) { s7f_fb_ctl++; g7f_v1(ctx); return; }
    const uint32_t p61 = g[61]._u32[0], p50 = g[50]._u32[0], p51 = g[51]._u32[0];
    if (g[57]._u32[0] != p61 + 16u || g[58]._u32[0] != p50 + 16u || g[59]._u32[0] != p51 + 16u) {
        s7f_fb_ctl++; g7f_v1(ctx); return;
    }
    /* slot arrays [p - 32n, p) (pairs 0 .. n-1; iteration 0's stores at r50/r51 + r7 come first in both
     * paths), the stream's loads [r35 & ~15, r35 + 36(n-1) + 64), the table [r40 & ~15, + 256) */
    const uint32_t r35 = g[35]._u32[0], tab = g[40]._u32[0];
    const uint64_t s0 = r35 & ~15u, s1 = (uint64_t)r35 + 36u * (uint64_t)(n - 1u) + 64u;
    const uint64_t t0 = tab & ~15u, t1 = t0 + 256u;
    const uint32_t ps[3] = { p61, p50, p51 };
    if (r35 < 72u || s1 > SPU_LS_SIZE || t1 > SPU_LS_SIZE) { s7f_fb_rng++; g7f_v1(ctx); return; }
    for (int i = 0; i < 3; i++)
        if ((ps[i] & 15u) || ps[i] < nb || ps[i] > SPU_LS_SIZE) { s7f_fb_rng++; g7f_v1(ctx); return; }
    for (int i = 0; i < 3; i++) {
        const uint64_t a0 = ps[i] - nb, a1 = ps[i];
        if (!g7f_apart(a0, a1, s0, s1) || !g7f_apart(a0, a1, t0, t1)) { s7f_fb_ovl++; g7f_v1(ctx); return; }
        for (int j = i + 1; j < 3; j++)
            if (!g7f_apart(a0, a1, ps[j] - nb, ps[j])) { s7f_fb_ovl++; g7f_v1(ctx); return; }
    }
    g7f_inv I;
    I.c55 = g7f_bc(g[55]); I.c54 = g7f_bc(g[54]); I.c56 = g7f_bc(g[56]);
    {
        uint32_t sh[2], mk[2], ex[2];
        for (int k = 0; k < 2; k++) {
            sh[k] = (0u - g[43]._u32[1 + k]) & 63u;
            mk[k] = g[41]._u32[1 + k];
            ex[k] = g[42]._u32[1 + k] & ~mk[k];
        }
        I.sh = _mm256_setr_epi32((int)sh[0], (int)sh[1], (int)sh[0], (int)sh[1], (int)sh[0], (int)sh[1], (int)sh[0], (int)sh[1]);
        I.msk = _mm256_setr_epi32((int)mk[0], (int)mk[1], (int)mk[0], (int)mk[1], (int)mk[0], (int)mk[1], (int)mk[0], (int)mk[1]);
        I.ex = _mm256_setr_epi32((int)ex[0], (int)ex[1], (int)ex[0], (int)ex[1], (int)ex[0], (int)ex[1], (int)ex[0], (int)ex[1]);
    }
    I.sh0 = (0u - g[43]._u32[0]) & 63u; I.msk0 = g[41]._u32[0]; I.ex0 = g[42]._u32[0];
    I.m32 = g7f_bc(g[32]); I.c31 = g7f_bc(g[31]); I.m30 = g7f_bc(g[30]);
    { u128 z; memset(&z, 0, sizeof z); I.a52 = g7f_v8(z, g[52]); }
    I.w60 = g[60]._u32[3];
    I.in = r35 - 72u;
    I.tab = tab;
    I.o61 = p61 - nb; I.o57 = I.o61 + 16u; I.o50 = p50 - nb; I.o58 = I.o50 + 16u; I.o51 = p51 - nb; I.o59 = I.o51 + 16u;
    s7f_v3_calls++;
    /* iterations 0-2 lifted, from the entry state (they store what comes from the prologue's registers) */
    g7f_iters(ctx, 3, 0);
    /* positions of pairs 3 .. n-1, decoded slots of pairs 2 .. n-2 */
    const uint64_t c0 = __rdtsc();
    g7f_pair(ls, &I, 2, 0, 1);
    for (uint32_t p = 3; p + 1 < n; p++) g7f_pair(ls, &I, p, 1, 1);
    g7f_pair(ls, &I, n - 1u, 1, 0);
    s7f_cyc_pk += __rdtsc() - c0;
    s7f_pk_pairs += (long)n - 3;
    s7f_last_p3 = I.o51 + 96u;
    /* the state before the last iteration: the pointers from iteration 3 to n-4, three lifted iterations
     * without stores, then the last one with its stores */
    const uint32_t back = n - 7u;   /* n >= 8 */
    g[26] = g7f_addw(g[26], 32u * back);
    g[7] = g7f_addw(g[7], 32u * back);
    g[35] = g7f_addw(g[35], 36u * back);
    g7f_iters(ctx, 4, 3);
}

/* ---- self-test at mode init: the pieces against the helpers, then whole loop runs, v3 against v1 (the
 * lifted statements) on a scratch context, all GPRs and the whole LS compared ---- */
static uint32_t s7f_rng = 0x6A09E667u;
static uint32_t g7f_rand(void) { s7f_rng ^= s7f_rng << 13; s7f_rng ^= s7f_rng >> 17; s7f_rng ^= s7f_rng << 5; return s7f_rng; }
static u128 g7f_q(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static u128 g7f_rq(void) { return g7f_q(g7f_rand(), g7f_rand(), g7f_rand(), g7f_rand()); }
/* a float word: ordinary values, zeros, denormals, exponent 1/253/254/255, any bits, values near 0 and 1 */
static uint32_t g7f_rword(void)
{
    const uint32_t r = g7f_rand(), s = g7f_rand() & 0x80000000u, m = g7f_rand() & 0x7FFFFFu;
    switch (g7f_rand() % 12u) {
    case 0: return s;
    case 1: return s | m;                                        /* exponent 0 */
    case 2: return r;                                            /* any bits */
    case 3: return s | 0x7F800000u | m;                          /* exponent 255 */
    case 4: return s | ((0xFDu + r % 2u) << 23) | m;             /* exponent 253/254 */
    case 5: return s | (1u << 23) | m;                           /* exponent 1 */
    case 6: return s | ((0x60u + r % 0x20u) << 23) | m;          /* small */
    default: return s | ((0x7Cu + r % 4u) << 23) | m;            /* 1/16 .. 1 */
    }
}
static int g7f_eq(u128 a, u128 b) { return !memcmp(&a, &b, 16); }
static void g7f_selftest(void)
{
    long bad_e = 0, bad_i = 0, bad_s = 0, bad_f = 0, kept = 0, runs = 0, bad_run = 0, took = 0;
    const long sh0 = s7f_fb_short, ct0 = s7f_fb_ctl, rg0 = s7f_fb_rng, ov0 = s7f_fb_ovl, pk0 = s7f_pk_pairs;
    const long fs0 = s7f_fp_slow, v30 = s7f_v3_calls;
    const uint64_t cp0 = s7f_cyc_pk;
    for (int it = 0; it < 20000; it++) {
        u128 a[2], b[2], c[2], e[2];
        for (int h = 0; h < 2; h++) {
            a[h] = g7f_q(g7f_rword(), g7f_rword(), g7f_rword(), g7f_rword());
            b[h] = g7f_q(g7f_rword(), g7f_rword(), g7f_rword(), g7f_rword());
            c[h] = g7f_q(g7f_rword(), g7f_rword(), g7f_rword(), g7f_rword());
            e[h] = (it & 1) ? spu_frsqest(b[h]) : b[h];   /* fi on an estimate, or on any bits */
        }
        /* frsqest and fi */
        const v8 r8 = g7f_frsqest(g7f_v8(a[0], a[1])), i8 = g7f_fi(g7f_v8(a[0], a[1]), g7f_v8(e[0], e[1]));
        for (int h = 0; h < 2; h++) {
            bad_e += !g7f_eq(spu_frsqest(a[h]), h ? g7f_hi(r8) : g7f_lo(r8));
            bad_i += !g7f_eq(spu_fi(a[h], e[h]), h ? g7f_hi(i8) : g7f_lo(i8));
        }
        /* shufb, controls with any bytes (special selectors included) */
        const u128 sc0 = g7f_rq(), sc1 = g7f_rq();
        const v8 s8 = g7f_shufb(g7f_v8(a[0], a[1]), g7f_v8(b[0], b[1]), g7f_v8(sc0, sc1));
        bad_s += !g7f_eq(spu_shufb(a[0], b[0], sc0), g7f_lo(s8));
        bad_s += !g7f_eq(spu_shufb(a[1], b[1], sc1), g7f_hi(s8));
        /* fma fm fa fs with the lane rule: a lane kept must be the helper's */
        for (int op = 0; op < 4; op++) {
            v8 ok = _mm256_set1_epi32(-1);
            const v8 A = g7f_v8(a[0], a[1]), B = g7f_v8(b[0], b[1]), C = g7f_v8(c[0], c[1]);
            const v8 r = op == 0 ? g7f_fma(A, B, C, &ok) : op == 1 ? g7f_fm(A, B, &ok) : op == 2 ? g7f_fa(A, B, &ok) : g7f_fs(A, B, &ok);
            for (int h = 0; h < 2; h++) {
                const u128 ref = op == 0 ? spu_fma(a[h], b[h], c[h]) : op == 1 ? spu_fm(a[h], b[h])
                               : op == 2 ? spu_fa(a[h], b[h]) : spu_fs(a[h], b[h]);
                const u128 got = h ? g7f_hi(r) : g7f_lo(r), okw = h ? g7f_hi(ok) : g7f_lo(ok);
                for (int k = 0; k < 4; k++)
                    if (okw._u32[k]) { kept++; bad_f += ref._u32[k] != got._u32[k]; }
            }
        }
    }
    spu_context* ctx = (spu_context*)calloc(1, sizeof(spu_context));
    uint8_t* pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    uint8_t* ref_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    u128* pre_g = (u128*)malloc(sizeof(ctx->gpr));
    u128* ref_g = (u128*)malloc(sizeof(ctx->gpr));
    static const struct { uint8_t r; uint32_t w[4]; } fma[] = GOW_O71F_FMA;   /* generated */
    static const uint32_t tabw[GOW_O71F_NTAB][4] = GOW_O71F_TAB;               /* generated */
    for (int it = 0; ctx && pre_ls && ref_ls && pre_g && ref_g && it < 600; it++) {
        ctx->ls = ctx->ls_store;
        memset(ctx->ls, 0, SPU_LS_SIZE);
        u128* gg = ctx->gpr;
        for (int r = 0; r < 128; r++) gg[r] = g7f_rq();
        for (int i = 0; i < G7F_NCTL; i++) memcpy(gg[g7f_ctl[i].r]._u32, g7f_ctl[i].w, 16);
        if (it % 50 == 49) gg[g7f_ctl[g7f_rand() % G7F_NCTL].r]._u32[g7f_rand() & 3] ^= 1u << (g7f_rand() & 31);
        if (it & 1) {   /* the captures' operands, else random ones */
            for (int i = 0; i < (int)(sizeof(fma) / sizeof(fma[0])); i++) memcpy(gg[fma[i].r]._u32, fma[i].w, 16);
        } else {
            for (int i = 0; i < 4; i++) { gg[55]._u32[i] = g7f_rword(); gg[54]._u32[i] = g7f_rword(); gg[56]._u32[i] = g7f_rword(); }
        }
        const uint32_t n = (it % 7 == 0) ? 1u + g7f_rand() % 9u : 1u + g7f_rand() % 70u;
        /* the table at 0x1000 + 16k (r40 = r1 + 32), the captures' entries in half the runs, else random;
         * the 4 entries past it and around it random */
        const uint32_t tab = 0x1000u + 16u * (g7f_rand() % 8u);
        for (uint32_t a = tab - 64u; a < tab + 320u; a++) ctx->ls[a] = (uint8_t)g7f_rand();
        if (it & 2)
            for (int k = 0; k < GOW_O71F_NTAB; k++)
                for (int j = 0; j < 4; j++) {
                    const uint32_t v = tabw[k][j], a = tab + 16u * (uint32_t)k + 4u * (uint32_t)j;
                    ctx->ls[a] = (uint8_t)(v >> 24); ctx->ls[a + 1] = (uint8_t)(v >> 16); ctx->ls[a + 2] = (uint8_t)(v >> 8); ctx->ls[a + 3] = (uint8_t)v;
                }
        gg[40] = g7f_q(tab, g7f_rand(), g7f_rand(), g7f_rand());
        /* the record stream at any alignment, random bytes past the n + 2 pairs it may read */
        const uint32_t S = 0x8000u + g7f_rand() % 64u;
        for (uint32_t a = S - 16u; a < S + 36u * (n + 4u); a++) ctx->ls[a] = (uint8_t)g7f_rand();
        gg[35] = g7f_q(S + 72u, g7f_rand(), g7f_rand(), g7f_rand());
        /* slot arrays (16-aligned; one in 40 runs not; one in 30 over the stream), surroundings random */
        uint32_t pp[3] = { 0x20000u + 32u * n, 0x28000u + 32u * n, 0x30000u + 32u * n };
        if (it % 40 == 7) pp[1] += 4u;
        if (it % 30 == 11) pp[2] = (S & ~15u) + 32u * n + 64u;
        for (int i = 0; i < 3; i++)
            for (uint32_t a = pp[i] - 32u * n - 64u; a < pp[i] + 128u; a++) ctx->ls[a & SPU_LS_MASK] = (uint8_t)g7f_rand();
        gg[61] = g7f_q(pp[0], g7f_rand(), g7f_rand(), g7f_rand());
        gg[50] = g7f_q(pp[1], g7f_rand(), g7f_rand(), g7f_rand());
        gg[51] = g7f_q(pp[2], g7f_rand(), g7f_rand(), g7f_rand());
        gg[57] = g7f_q(pp[0] + 16u, g7f_rand(), g7f_rand(), g7f_rand());
        gg[58] = g7f_q(pp[1] + 16u, g7f_rand(), g7f_rand(), g7f_rand());
        gg[59] = g7f_q(pp[2] + 16u, g7f_rand(), g7f_rand(), g7f_rand());
        if (it % 60 == 23) gg[58]._u32[0] += 16u;   /* a slot pointer pair off: v1 */
        gg[26] = g7f_q(0u - 32u * n, g7f_rand(), g7f_rand(), g7f_rand());
        gg[7] = (it & 4) ? g7f_q(0, 0, 0, 0) : g7f_q((g7f_rand() % 64u) * 16u, g7f_rand(), g7f_rand(), g7f_rand());
        memcpy(pre_ls, ctx->ls, SPU_LS_SIZE); memcpy(pre_g, gg, sizeof(ctx->gpr));
        g7f_v1(ctx);
        memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); memcpy(ref_g, gg, sizeof(ctx->gpr));
        memcpy(ctx->ls, pre_ls, SPU_LS_SIZE); memcpy(gg, pre_g, sizeof(ctx->gpr));
        const long t0 = s7f_v3_calls;
        g7f_v3(ctx);
        took += s7f_v3_calls - t0;
        runs++;
        const int bad = memcmp(ctx->ls, ref_ls, SPU_LS_SIZE) != 0 || memcmp(gg, ref_g, sizeof(ctx->gpr)) != 0;
        if (bad && bad_run < 3) {
            for (int r = 0; r < 128; r++)
                if (memcmp(&gg[r], &ref_g[r], 16))
                    fprintf(stderr, "[edge-o71fa90] self-test run %d (n %u): r%d v1 %08X v3 %08X\n", it, n, r, ref_g[r]._u32[0], gg[r]._u32[0]);
            for (long i = 0; i < SPU_LS_SIZE; i++)
                if (ctx->ls[i] != ref_ls[i]) {
                    fprintf(stderr, "[edge-o71fa90] self-test run %d: LS first differs at 0x%05lX (v1 %02X v3 %02X)\n", it, i, ref_ls[i], ctx->ls[i]);
                    break;
                }
        }
        bad_run += bad;
    }
    free(pre_ls); free(ref_ls); free(pre_g); free(ref_g); free(ctx);
    fprintf(stderr, "[edge-o71fa90] self-test: frsqest %ld of 40000 quads differ, fi %ld of 40000, shufb %ld of 40000; "
                    "fma/fm/fa/fs lanes kept %ld, %ld differ; whole loop runs, v3 against v1: %ld of %ld differ (%ld taken "
                    "by v3, %ld pairs packed, %ld pairs' floats by the helpers; left to v1: %ld short, %ld controls, %ld "
                    "range, %ld overlap)\n",
            bad_e, bad_i, bad_s, kept, bad_f, bad_run, runs, took, s7f_pk_pairs - pk0, s7f_fp_slow - fs0,
            s7f_fb_short - sh0, s7f_fb_ctl - ct0, s7f_fb_rng - rg0, s7f_fb_ovl - ov0);
    s7f_fb_short = sh0; s7f_fb_ctl = ct0; s7f_fb_rng = rg0; s7f_fb_ovl = ov0; s7f_pk_pairs = pk0;
    s7f_fp_slow = fs0; s7f_v3_calls = v30; s7f_cyc_pk = cp0;
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s7f_mode = -1;

int gow_edge_o71fa90_mode(void)
{
    if (s7f_mode < 0) {
        const char* e = getenv("GOW_EDGE_O71FA90");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        s7f_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o71fa90] GOW_EDGE_O71FA90=%s (lifted loop)\n", e); }
        else {
            fprintf(stderr, "[edge-o71fa90] GOW_EDGE_O71FA90=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : "v3");
            g7f_selftest();
        }
    }
    return s7f_mode;
}

static int s7f_fault = -1;
static long s7f_fault_at, s7f_runs;
static void g7f_fault(spu_context* ctx)
{
    if (s7f_fault < 0) {
        const char* f = getenv("GOW_EDGE_O71FA90_FAULT");
        s7f_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s7f_fault_at = k ? atol(k + 1) : 0;
    }
    if (s7f_fault <= 0 || s7f_runs++ != s7f_fault_at) return;
    /* the last iteration stores pair n-2's first decoded quad at r50 + r18 = r50 - 64 (r50 is not written) */
    const uint32_t q = (ctx->gpr[50]._u32[0] - 64u) & (SPU_LS_MASK & ~0xFu);
    uint32_t at = q;
    switch (s7f_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[15]._u32[0] ^= 1; break;
    case 4:
        at = s7f_last_p3 != ~0u ? s7f_last_p3 : 0u;
        ctx->ls[(at + 3u) & SPU_LS_MASK] ^= 1;
        break;
    default: break;
    }
    fprintf(stderr, "[edge-o71fa90] fault %d injected (native run %ld, LS 0x%05X%s)\n", s7f_fault, s7f_fault_at, at,
            s7f_fault == 4 && s7f_last_p3 == ~0u ? ": the call was not packed" : "");
    s7f_fault = 0;
}

static void g7f_run(spu_context* ctx)
{
    if (s7f_mode == 2 || s7f_mode == 4) { s7f_last_p3 = ~0u; g7f_v1(ctx); }
    else                                g7f_v3(ctx);
    if (__builtin_expect(s7f_fault != 0, 0)) g7f_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then runs the loop's exit. */
void gow_edge_o71fa90_loop(spu_context* ctx) { g7f_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} g7f_check_state;
static _Thread_local g7f_check_state* t7f_chk;

static long s7f_calls, s7f_bad, s7f_iters;
static uint64_t s7f_cyc_nat, s7f_cyc_lift;
static int s7f_atexit;

/* Calls checked per placement (the blob's LS address the generated switch passes). */
#define G7F_SITES 56
static unsigned s7f_site_lsa[G7F_SITES];
static long s7f_site_calls[G7F_SITES], s7f_site_bad[G7F_SITES];
static _Thread_local unsigned t7f_site;

static void g7f_check_report(void)
{
    char buf[G7F_SITES * 28] = "", *p = buf;
    for (int i = 0; i < G7F_SITES && s7f_site_calls[i]; i++)
        p += snprintf(p, sizeof(buf) - (size_t)(p - buf), " %X:%ld/%ld", s7f_site_lsa[i], s7f_site_calls[i],
                      s7f_site_bad[i]);
    fprintf(stderr, "[edge-o71fa90-check] calls by placement (blob LS address:calls/differ):%s\n", buf);
    fprintf(stderr, "[edge-o71fa90-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); tsc: "
                    "native %.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s7f_calls, s7f_bad, s7f_iters, s7f_cyc_nat / 1e6, s7f_cyc_lift / 1e6,
            s7f_iters ? (double)s7f_cyc_nat / (double)s7f_iters : 0.0,
            s7f_iters ? (double)s7f_cyc_lift / (double)s7f_iters : 0.0);
    fprintf(stderr, "[edge-o71fa90-check] v3 took %ld calls, %ld pairs packed; entries left to v1: %ld short, %ld "
                    "other controls or slot pointers, %ld outside the LS or unaligned, %ld overlapping; pairs whose "
                    "floats went to the helpers: %ld; tsc in the packed pairs %.1f M (%.1f per pair)\n",
            s7f_v3_calls, s7f_pk_pairs, s7f_fb_short, s7f_fb_ctl, s7f_fb_rng, s7f_fb_ovl, s7f_fp_slow,
            s7f_cyc_pk / 1e6, s7f_pk_pairs ? (double)s7f_cyc_pk / (double)s7f_pk_pairs : 0.0);
}

void gow_edge_o71fa90_check_begin_at(spu_context* ctx, unsigned site)
{
    g7f_check_state* c = t7f_chk;
    if (!c) {
        c = (g7f_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t7f_chk = c;
    }
    if (!s7f_atexit) { s7f_atexit = 1; atexit(g7f_check_report); }
    t7f_site = site;
    s7f_iters += (long)((0u - ctx->gpr[26]._u32[0]) / 32u);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    g7f_run(ctx);
    s7f_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_o71fa90_check_end(spu_context* ctx)
{
    g7f_check_state* c = t7f_chk;
    if (!c || !c->pending) return;
    s7f_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s7f_calls++;
    int site = -1;
    for (int i = 0; i < G7F_SITES; i++) {   /* matching or first free slot (not atomic, like s7f_calls) */
        if (s7f_site_calls[i] && s7f_site_lsa[i] != t7f_site) continue;
        s7f_site_lsa[i] = t7f_site;
        s7f_site_calls[i]++;
        site = i;
        break;
    }
    t7f_site = 0;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-o71fa90-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-o71fa90-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s7f_bad++;
        if (site >= 0) s7f_site_bad[site]++;
        fprintf(stderr, "[edge-o71fa90-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) g7f_check_report();   /* a game run ends by kill: atexit never runs */
}

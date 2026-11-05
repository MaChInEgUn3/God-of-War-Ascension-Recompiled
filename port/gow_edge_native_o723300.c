/* gow_edge_native_o723300.c -- native replacement for the loop of EDGE overlay blob 723300 (output packing,
 * family B in work/edgefrag_scope.md): the loop at blob+0x2B8, in every placement lift of the blob
 * (spu/edgefrag_723300_<lsa>; 19 placements, one lift each). gen_edge_frag_lift.py puts a switch before the
 * loop in each of them (checking that each placement's loop is the reference 11340's, statement for
 * statement); GOW_EDGE_O723300 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native_o719900.c (-O2 -march=x86-64-v3 -ffp-contract=off,
 * SPU_FP_NATIVE=1, SPU_FP_CHECK_HOOK=0): the float helpers (fma, cflts) are the same inline helpers the lift
 * uses, on the same operands in the same order, so they round the same way. Runs inside spu_run_with_halt,
 * i.e. with MXCSR rounding toward zero and FTZ/DAZ, as the lift does.
 *
 * Clean-room: written from our own lift of the fragment and our own captures.
 *
 * What the loop does (a reading of the lift; 2 vertices per iteration, 72 output bytes, r36 a negative byte
 * offset stepping by 72 to 0, r9 = r36 + 72 the back-edge condition, r21 its copy for the stores):
 *   - four input slot arrays (float4 per vertex; r30 r28 r31 r25, +32 each per iteration), loaded one
 *     iteration ahead (software pipelined);
 *   - r30's: fma(r51, x, r60) (scale and bias) -> cflts 157 (x 2^16, truncated, saturated) -> the high
 *     halfwords: s16 normalized, all four components;
 *   - r28's: fma(r62, x, r61) (scale/bias quads from the blob's pool) -> cflts 142 (x 2^31) -> rotm by r26
 *     (a shift per component) -> x y z merged into one word: shufb r72 puts z's and x's high halfwords
 *     side by side, shufb r59 y's top two bytes in the middle, selb r68 (a mask of bits 21..11) takes y's
 *     bits there: the 11/11/10 field;
 *   - r31's and r25's x y z passed through;
 *   - the fields are merged into a byte stream by shufb controls the prologue loaded, rotated to the output
 *     alignment (control r41 = r44 - splat((r64 + r36) & 15)) and stored as six aligned quads at r21 +
 *     {r63 r33 r34 r35 r52 r32}, the partial first quad read back (r53 + r21 after the stores, i.e. the
 *     next record's start) and merged.
 *
 * v1: the loop statement for statement in C locals (GOW_O723_BODY, generated from the reference lift at
 * build time), as one C loop (the lift hops through two functions per iteration), with direct LS access
 * and the loop-invariant shufb controls left to the compiler to hoist. Every op is the lift's op on the
 * same operands in the same order, so it is exact for every input.
 *
 * v3 (packed): the 72 bytes of a record computed straight from the inputs, one record (2 vertices, 8 lanes)
 * per step. What the lift computes, read by evaluating its body symbolically (byte by byte) over several
 * iterations with the controls of the captures (all the same in every call): iteration k stores record k at
 * S_k = r64 + r36 + 72k (the entry r36), contiguous (the merge keeps the bytes before S_k, the bytes after
 * the record are rewritten by the next one), and record k for k >= 3 is the input pair at P + 32(k - 3) of
 * each array (the pipeline is three iterations deep; records 0-2 come from what the prologue loaded). Per
 * vertex (36 bytes):
 *   r31 bytes 0-11 | s16(r30) x y z w | pk(r28) | r25 bytes 0-11
 * with s16 = cflts(fma(r51, x, r60), 157) >> 16 and pk = ((Z & 0xFFFF0000) | X >> 16) & ~r68 |
 * (Y >> 8) & r68, X Y Z = cflts(fma(r62, .., r61), 142) >> by r26 (rotm, words 0-2). Integer work is exact by
 * construction; fma is the native op with the helper's lane rule (a pair with a failing lane is redone by
 * the helper), cflts the helper's integer formula 8 lanes wide. The carried registers depend only on the
 * last three iterations' loads (and r23's bytes 8-15, which the loop only carries along; they are what the
 * last iteration leaves after the record), so the end state is rebuilt by the lifted body: iterations 0-2
 * run lifted from the entry state (with their stores), records 3 .. n-2 packed, then iterations n-4 .. n-2
 * again lifted with the pointers set back and no stores (the state converges), and the last iteration
 * lifted with its stores: all 45 registers the loop writes and the trailing bytes it leaves come out
 * exactly. Entry checks, else v1 (each counted): 8 .. 8192 iterations (r36 a negative multiple of 72, r21
 * equal to it); the controls and masks equal the captures' (the layout above rests on them; r62 r61 r51 r60
 * r26 are used as they are); the store pointers r63 r33 r34 r35 r52 r53 r32 at r64 + 0..80 as the prologue
 * sets them; the input pointers 16-byte aligned; inputs and output inside the LS without wrapping and apart
 * from each other.
 *
 * GOW_EDGE_O723300 (read once):
 *   unset/0  lifted loop (the default)
 *   1        native v3 (v1 for the entries it does not take)
 *   2        native v1
 *   3        check v3: run it on a copy of the state, then the lifted loop, and compare all GPRs and the
 *            whole LS at the loop exit; `[edge-o723300-check] N calls checked, M differ` every 4096 calls
 *            and at exit, with the calls per placement
 *   4        check v1, the same way
 * GOW_EDGE_O723300_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 3 of the quad the last iteration stores at r63 + r21, 3 bit 0 of byte 0 of that quad, 2 bit 0 of r31
 * (a register the loop writes) after the loop, 4 bit 0 of the first byte of record 3 (a packed one when v3
 * took the call).
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
static inline __m128i go72_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 go72_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = go72_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void go72_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), go72_bswap32x4(x));
}
#define LD(a)    go72_ld(ls, (a))
#define ST(a, v) go72_st(ls, (a), (v))

/* the lifted loop: GOW_O723_*. Game code, so it is not in the repo: gen_edge_frag_lift.py --body writes it
 * from the reference lift at build time (no LS address of its own, straight code: checked there). */
#include "gow_edge_o723300_body.h"

/* v1: the whole loop (back edge on r9) */
static void go72_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O723_REGS()
    GOW_O723_LOAD()
    do {
        GOW_O723_BODY()
    } while (r9._u32[0] != 0);
    GOW_O723_STORE()
}

/* the lifted body for `count` iterations, the first `nost` of them without their stores (v3: the first
 * three iterations with their stores; the warm-up, its registers only (the loads still read the LS), and
 * the last iteration with its stores). One copy of the body for both, so the second call finds it cached. */
#undef ST
#define ST(a, v) do { if (i >= nost) go72_st(ls, (a), (v)); } while (0)
static void go72_iters(spu_context* ctx, uint32_t count, uint32_t nost)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O723_REGS()
    GOW_O723_LOAD()
    for (uint32_t i = 0; i < count; i++) {
        GOW_O723_BODY()
    }
    GOW_O723_STORE()
}
#undef ST
#define ST(a, v) go72_st(ls, (a), (v))

/* =====================================================================================================
 * v3: one record (the iteration's two vertices: vertex A in the low 128-bit lane, B in the high) per step.
 * ===================================================================================================== */
typedef __m256i v8;
#define GO72_AI static inline __attribute__((always_inline))

static inline v8 go72_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 go72_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(a); }
static inline u128 go72_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 go72_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline __m256 go72_p(v8 x) { return _mm256_castsi256_ps(x); }
static inline v8 go72_f(__m256 x) { return _mm256_castps_si256(x); }
/* 32 bytes of LS in memory order, and as host words (the pair's two quads) */
GO72_AI v8 go72_raw(const uint8_t* ls, uint32_t a) { return _mm256_loadu_si256((const __m256i*)(ls + a)); }
GO72_AI v8 go72_bsw(v8 x)
{
    return _mm256_shuffle_epi8(x, _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                   3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}

/* fma: the native op, kept when every lane passes the SPU_FP_NATIVE helper's rule (SPU_FP_DAZ: the
 * result's exponent 1..253, or a zero; spu_helpers.h spu__xn_fma). A zero keeps its sign here (the helper
 * makes it +0), which cflts, the only reader, maps to 0 either way. Otherwise the pair is redone by the
 * helper itself. */
static long s72_fma_slow;   /* records whose fma went to the helper (lab statistic; racy across threads) */
static inline v8 go72_fma_slow(v8 a, v8 b, v8 c)
{
    return go72_v8(spu_fma(go72_lo(a), go72_lo(b), go72_lo(c)), spu_fma(go72_hi(a), go72_hi(b), go72_hi(c)));
}
GO72_AI v8 go72_fma_ok(v8 r)
{
    const v8 e = _mm256_srli_epi32(_mm256_slli_epi32(r, 1), 24);
    const v8 n = _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
    return _mm256_or_si256(n, _mm256_cmpeq_epi32(_mm256_slli_epi32(r, 1), _mm256_setzero_si256()));
}

/* cflts, 8 lanes: the SPU_FP_NATIVE helper's integer formula (spu__xe_cfl, signed) as it is */
GO72_AI v8 go72_cflts(v8 x, int i8)
{
    const v8 z = _mm256_setzero_si256();
    const v8 be = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 m = _mm256_or_si256(_mm256_and_si256(x, _mm256_set1_epi32(0x7FFFFF)), _mm256_set1_epi32(0x800000));
    const v8 sh = _mm256_add_epi32(be, _mm256_set1_epi32(23 - (i8 & 0xFF)));
    const v8 lsh = _mm256_max_epi32(sh, z);
    const v8 rsh = _mm256_max_epi32(_mm256_sub_epi32(z, sh), z);
    v8 v = _mm256_blendv_epi8(_mm256_srlv_epi32(m, rsh), _mm256_sllv_epi32(m, lsh), _mm256_cmpgt_epi32(sh, _mm256_set1_epi32(-1)));
    const v8 neg = _mm256_srai_epi32(x, 31), isz = _mm256_cmpeq_epi32(be, z);
    v = _mm256_sub_epi32(_mm256_xor_si256(v, neg), neg);
    const v8 sat = _mm256_xor_si256(_mm256_set1_epi32(0x7FFFFFFF), neg);
    v = _mm256_blendv_epi8(v, sat, _mm256_cmpgt_epi32(sh, _mm256_set1_epi32(7)));
    return _mm256_andnot_si256(isz, v);
}
/* the same for lanes that passed the fma rule (exponent 1..253, or a zero): x * 2^(173 - i8) is exact (an
 * overflow rounds toward zero to the largest single, still >= 2^31), cvtt truncates whatever the MXCSR,
 * gives 0x80000000 at or beyond +-2^31, which is the SPU's saturation for the negative lanes and is flipped
 * to 0x7FFFFFFF for the positive ones. Compared with the helper in the self-test. */
GO72_AI v8 go72_cflts_ok(v8 x, float scale)
{
    const __m256 s = _mm256_mul_ps(go72_p(x), _mm256_set1_ps(scale));
    return _mm256_xor_si256(_mm256_cvttps_epi32(s), go72_f(_mm256_cmp_ps(s, _mm256_set1_ps(2147483648.0f), _CMP_GE_OQ)));
}

typedef struct go72_inv_ {
    v8 c51, c60, c62, c61, n26;   /* fma operands (broadcast) and the rotm counts (0 - r26) & 63 */
    v8 m68, k68;                  /* the 11/11/10 merge mask and its complement */
    uint32_t p30, p28, p31, p25;
} go72_inv;

/* the byte shuffles that place the fields (per lane; 0x80 = zero) */
#define GO72_M(...) _mm256_setr_epi8(__VA_ARGS__, __VA_ARGS__)
#define Z4 -128,-128,-128,-128

/* record k = input pair j = k - 3, written at s (exactly its 72 bytes). slow = 0: the native fma, and 0
 * returned, before anything is stored, if a lane fails the rule; slow = 1: the helper's fma
 * (go72_record_slow, out of the hot loop). */
GO72_AI int go72_record(uint8_t* ls, const go72_inv* I, uint32_t j, uint32_t s, int slow)
{
    const uint32_t o = 32u * j;
    const v8 x30 = go72_bsw(go72_raw(ls, I->p30 + o)), x28 = go72_bsw(go72_raw(ls, I->p28 + o));
    v8 f30, f28;
    if (slow) {
        f30 = go72_fma_slow(I->c51, x30, I->c60);
        f28 = go72_fma_slow(I->c62, x28, I->c61);
    } else {
        f30 = go72_f(_mm256_fmadd_ps(go72_p(I->c51), go72_p(x30), go72_p(I->c60)));
        f28 = go72_f(_mm256_fmadd_ps(go72_p(I->c62), go72_p(x28), go72_p(I->c61)));
        if (__builtin_expect(_mm256_movemask_ps(go72_p(_mm256_and_si256(go72_fma_ok(f30), go72_fma_ok(f28)))) != 0xFF, 0))
            return 0;
    }
    const v8 r31 = go72_raw(ls, I->p31 + o), r25 = go72_raw(ls, I->p25 + o);
    const v8 c30 = slow ? go72_cflts(f30, 157) : go72_cflts_ok(f30, 65536.0f);
    const v8 c28 = slow ? go72_cflts(f28, 142) : go72_cflts_ok(f28, 2147483648.0f);
    const v8 q = _mm256_srlv_epi32(c28, I->n26);                              /* rotm (>31: 0) */
    const v8 X = _mm256_shuffle_epi32(q, 0x00), Y = _mm256_shuffle_epi32(q, 0x55), Z = _mm256_shuffle_epi32(q, 0xAA);
    const v8 K = _mm256_or_si256(
        _mm256_and_si256(_mm256_or_si256(_mm256_and_si256(Z, _mm256_set1_epi32((int)0xFFFF0000u)), _mm256_srli_epi32(X, 16)),
                         I->k68),
        _mm256_and_si256(_mm256_srli_epi32(Y, 8), I->m68));
    /* bytes 0-15: r31 x y z | s16 x y; 16-19: s16 z w; 20-35: pk | r25 x y z */
    const v8 a = _mm256_blend_epi32(r31, _mm256_shuffle_epi8(c30, GO72_M(Z4, Z4, Z4, 3,2,7,6)), 0x88);
    const v8 b = _mm256_shuffle_epi8(c30, GO72_M(11,10,15,14, Z4, Z4, Z4));
    const v8 c = _mm256_or_si256(_mm256_shuffle_epi8(K, GO72_M(3,2,1,0, Z4, Z4, Z4)), _mm256_bslli_epi128(r25, 4));
    uint8_t* const d = ls + s;
    _mm_storeu_si128((__m128i*)(d + 0), _mm256_castsi256_si128(a));
    _mm_storeu_si128((__m128i*)(d + 16), _mm256_castsi256_si128(b));
    _mm_storeu_si128((__m128i*)(d + 20), _mm256_castsi256_si128(c));
    _mm_storeu_si128((__m128i*)(d + 36), _mm256_extracti128_si256(a, 1));
    _mm_storeu_si128((__m128i*)(d + 52), _mm256_extracti128_si256(b, 1));
    _mm_storeu_si128((__m128i*)(d + 56), _mm256_extracti128_si256(c, 1));
    return 1;
}
static __attribute__((noinline, cold)) void go72_record_slow(uint8_t* ls, const go72_inv* I, uint32_t j, uint32_t s)
{
    s72_fma_slow++;
    go72_record(ls, I, j, s, 1);
}
#undef GO72_M
#undef Z4

/* the controls and masks the layout rests on: GOW_O723_CTL, {register, its four SPU words} as the prologue
 * leaves them (the same in every capture call). Game data, so not in the repo: gen_edge_frag_lift.py
 * --consts computes them at build time from the reference lift's prologue and the blob's bytes in the
 * EBOOT. They are: r44 the output rotation base; r59 r72 the 11/11/10 field's byte pickers and r68 its bit
 * mask; r71 the splat of the alignment byte; r54-r58 r65-r67 r69 r70 the shufb controls that merge the
 * fields into the stream; r73 a word mask. */
#include "gow_edge_o723300_consts.h"
static const struct { uint8_t r; uint32_t w[4]; } go72_ctl[GOW_O723_NCTL] = GOW_O723_CTL;
#define GO72_NCTL GOW_O723_NCTL
/* the carried pointers: the four input arrays (+32 per iteration) and r36 (+72; r21 follows it) */
static const uint8_t go72_ptr[4] = { 30, 28, 31, 25 };
/* the store pointers: r64 + this (word 0) */
static const struct { uint8_t r; uint32_t d; } go72_out[] = {
    { 63, 0 }, { 33, 16 }, { 34, 32 }, { 35, 48 }, { 52, 64 }, { 53, 72 }, { 32, 80 },
};

#define GO72_NMIN 8u
#define GO72_NMAX 8192u
static long s72_fb_short;    /* entries with fewer than 8 iterations (or an implausible count): v1 */
static long s72_fb_ctl;      /* entries with other controls, masks or store pointers: v1 */
static long s72_fb_rng;      /* entries with inputs or output outside the LS or unaligned input pointers: v1 */
static long s72_fb_ovl;      /* entries whose output overlaps an input: v1 */
static long s72_pk_recs;     /* records written packed */
static long s72_v3_calls;    /* entries v3 took */
static uint64_t s72_cyc_pk;  /* tsc in the packed records (lab statistic) */
static uint32_t s72_last_s3; /* LS address of the last packed call's record 3 (fault 4), or ~0 */

static inline u128 go72_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}

static void go72_v3(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    s72_last_s3 = ~0u;
    const uint32_t w36 = g[36]._u32[0];
    const uint32_t nb = 0u - w36, n = nb / 72u;
    if (!w36 || nb % 72u || n < GO72_NMIN || n > GO72_NMAX || g[21]._u32[0] != w36) { s72_fb_short++; go72_v1(ctx); return; }
    for (int i = 0; i < GO72_NCTL; i++)
        if (memcmp(g[go72_ctl[i].r]._u32, go72_ctl[i].w, 16)) { s72_fb_ctl++; go72_v1(ctx); return; }
    const uint32_t o = g[64]._u32[0];
    for (int i = 0; i < (int)(sizeof(go72_out) / sizeof(go72_out[0])); i++)
        if (g[go72_out[i].r]._u32[0] != o + go72_out[i].d) { s72_fb_ctl++; go72_v1(ctx); return; }
    /* output: the stores of all n iterations, from the quad of S_0 to the quad after S_{n-1} + 80 */
    const uint32_t s0 = o + w36;
    const uint64_t olo = s0 & ~0xFu, ohi = (uint64_t)s0 + 72u * (uint64_t)n + 32u;
    if (s0 >= SPU_LS_SIZE || ohi > SPU_LS_SIZE) { s72_fb_rng++; go72_v1(ctx); return; }
    go72_inv I;
    uint32_t* const pp[4] = { &I.p30, &I.p28, &I.p31, &I.p25 };
    for (int i = 0; i < 4; i++) {
        const uint32_t p = g[go72_ptr[i]]._u32[0];
        const uint64_t phi = (uint64_t)p + 32u * (uint64_t)n + 32u;   /* every pair the n iterations load */
        if ((p & 15u) || p >= SPU_LS_SIZE || phi > SPU_LS_SIZE) { s72_fb_rng++; go72_v1(ctx); return; }
        if (p < ohi && olo < phi) { s72_fb_ovl++; go72_v1(ctx); return; }
        *pp[i] = p;
    }
    I.c51 = go72_bc(g[51]); I.c60 = go72_bc(g[60]); I.c62 = go72_bc(g[62]); I.c61 = go72_bc(g[61]);
    I.n26 = _mm256_and_si256(_mm256_sub_epi32(_mm256_setzero_si256(), go72_bc(g[26])), _mm256_set1_epi32(0x3F));
    I.m68 = _mm256_set1_epi32((int)g[68]._u32[0]);
    I.k68 = _mm256_set1_epi32((int)~g[68]._u32[0]);
    s72_v3_calls++;
    /* iterations 0-2 lifted, from the entry state (records 0-2 come from what the prologue loaded) */
    go72_iters(ctx, 3, 0);
    /* records 3 .. n-2 */
    const uint64_t t0 = __rdtsc();
    for (uint32_t k = 3; k + 1 < n; k++)
        if (!go72_record(ls, &I, k - 3, s0 + 72u * k, 0)) go72_record_slow(ls, &I, k - 3, s0 + 72u * k);
    s72_cyc_pk += __rdtsc() - t0;
    s72_pk_recs += (long)n - 4;
    s72_last_s3 = s0 + 216u;
    /* the state before the last iteration: the pointers back to iteration n-4 (they are at 3), then three
     * lifted iterations without stores (every carried register then depends only on those iterations'
     * loads, but r23's bytes 8-15, which the loop carries unchanged), then the last one with its stores */
    const uint32_t back = n - 7u;   /* n >= 8 */
    for (int i = 0; i < 4; i++) g[go72_ptr[i]] = go72_addw(g[go72_ptr[i]], 32u * back);
    g[36] = go72_addw(g[36], 72u * back);
    g[21] = g[36];
    go72_iters(ctx, 4, 3);
}

/* ---- self-test at mode init: cflts against the helper, then whole loop runs, v3 against v1 (the lifted
 * statements) on a scratch context, all GPRs and the whole LS compared ---- */
static uint32_t s72_rng = 0x2545F491u;
static uint32_t go72_rand(void) { s72_rng ^= s72_rng << 13; s72_rng ^= s72_rng >> 17; s72_rng ^= s72_rng << 5; return s72_rng; }
static u128 go72_q(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static u128 go72_rq(void) { return go72_q(go72_rand(), go72_rand(), go72_rand(), go72_rand()); }
static uint32_t go72_fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
/* an input word: mostly ordinary values, plus zeros, denormals, huge, Inf/NaN, random bits, edges */
static uint32_t go72_rword(void)
{
    const uint32_t r = go72_rand(), s = go72_rand() & 0x80000000u;
    switch (go72_rand() % 16u) {
    case 0: return s;
    case 1: return s | (r & 0x7FFFFFu);                                  /* exponent 0 */
    case 2: return r;                                                    /* any bits */
    case 3: return s | 0x7F800000u | (r & 0x7FFFFFu);                    /* exponent 255 */
    case 4: return s | ((0xFDu + r % 2u) << 23) | (r & 0x7FFFFFu);       /* exponent 253/254 */
    case 5: return s | ((0x3F800000u + (r % 0x400u) - 0x200u));          /* |x| near 1 */
    case 6: return s | ((0x3A000000u + (r % 0x40000u) - 0x20000u));      /* |x| near 2^-11 */
    case 7: return s | ((0x60u + r % 0x40u) << 23) | (go72_rand() & 0x7FFFFFu);
    default: return go72_fbits(((float)(int32_t)(r >> 1) / 1073741824.0f) * 1.2f);   /* +-1.2 */
    }
}
static void go72_selftest(void)
{
    long bad_c = 0, bad_k = 0, bad_run = 0, runs = 0, took = 0;
    const long sh0 = s72_fb_short, ct0 = s72_fb_ctl, rg0 = s72_fb_rng, ov0 = s72_fb_ovl, pk0 = s72_pk_recs;
    const long fs0 = s72_fma_slow, v30 = s72_v3_calls;
    const uint64_t cp0 = s72_cyc_pk;
    for (int it = 0; it < 20000; it++) {
        u128 a[2]; for (int h = 0; h < 2; h++) for (int i = 0; i < 4; i++) a[h]._u32[i] = go72_rword();
        const int i8 = (it & 1) ? 157 : 142;
        const v8 x = go72_v8(a[0], a[1]), c = (it & 1) ? go72_cflts(x, 157) : go72_cflts(x, 142);
        const u128 r0 = spu_cflts(a[0], i8), r1 = spu_cflts(a[1], i8);
        const u128 c0 = go72_lo(c), c1 = go72_hi(c);
        bad_c += memcmp(&r0, &c0, 16) != 0;
        bad_c += memcmp(&r1, &c1, 16) != 0;
        /* the fast conversion on lanes the fma rule keeps: exponent 1..253 or a zero */
        u128 b[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                const uint32_t r = go72_rand();
                b[h]._u32[i] = (r & 15u) == 0 ? (r & 0x80000000u) : (r & 0x807FFFFFu) | ((1u + go72_rand() % 253u) << 23);
            }
        const v8 y = go72_v8(b[0], b[1]);
        const v8 d = (it & 1) ? go72_cflts_ok(y, 65536.0f) : go72_cflts_ok(y, 2147483648.0f);
        const u128 q0 = spu_cflts(b[0], i8), q1 = spu_cflts(b[1], i8), d0 = go72_lo(d), d1 = go72_hi(d);
        bad_k += memcmp(&q0, &d0, 16) != 0;
        bad_k += memcmp(&q1, &d1, 16) != 0;
    }
    spu_context* ctx = (spu_context*)calloc(1, sizeof(spu_context));
    uint8_t* pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    uint8_t* ref_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    u128* pre_g = (u128*)malloc(sizeof(ctx->gpr));
    u128* ref_g = (u128*)malloc(sizeof(ctx->gpr));
    for (int it = 0; ctx && pre_ls && ref_ls && pre_g && ref_g && it < 600; it++) {
        ctx->ls = ctx->ls_store;
        memset(ctx->ls, 0, SPU_LS_SIZE);
        u128* gg = ctx->gpr;
        for (int r = 0; r < 128; r++) gg[r] = go72_rq();
        for (int i = 0; i < GO72_NCTL; i++) memcpy(gg[go72_ctl[i].r]._u32, go72_ctl[i].w, 16);
        if (it % 50 == 49) gg[go72_ctl[go72_rand() % GO72_NCTL].r]._u32[go72_rand() & 3] ^= 1u << (go72_rand() & 31);
        if (it & 1) {   /* the captures' fma operands and shifts, else random ones */
            static const struct { uint8_t r; uint32_t w[4]; } fma[] = GOW_O723_FMA;   /* generated */
            for (int i = 0; i < (int)(sizeof(fma) / sizeof(fma[0])); i++) memcpy(gg[fma[i].r]._u32, fma[i].w, 16);
        } else {
            for (int i = 0; i < 4; i++) {
                gg[51]._u32[i] = go72_rword(); gg[60]._u32[i] = go72_rword();
                gg[62]._u32[i] = go72_rword(); gg[61]._u32[i] = go72_rword();
                gg[26]._u32[i] = (it & 2) ? go72_rand() : 0u - (go72_rand() % 40u);
            }
        }
        const uint32_t n = (it % 7 == 0) ? 1u + go72_rand() % 9u : 1u + go72_rand() % 70u;
        /* inputs at 0x8000 + 0x2400 i (16-aligned; one in 40 runs not), filled past the n pairs */
        for (int i = 0; i < 4; i++) {
            const uint32_t p = 0x8000u + 0x2400u * (uint32_t)i + ((it % 40 == 7 && i == 2) ? 4u : 0u);
            gg[go72_ptr[i]] = go72_q(p, go72_rand(), go72_rand(), go72_rand());
            for (uint32_t k = 0; k < 8u * (n + 8u); k++) {
                const uint32_t v = go72_rword(), a = (p & ~3u) + 4u * k;
                ctx->ls[a] = (uint8_t)(v >> 24); ctx->ls[a + 1] = (uint8_t)(v >> 16); ctx->ls[a + 2] = (uint8_t)(v >> 8); ctx->ls[a + 3] = (uint8_t)v;
            }
        }
        /* output stream at any alignment, its surroundings random; one in 30 runs over an input */
        const uint32_t s0 = (it % 30 == 11) ? 0x8000u + 0x2400u * 2u + 8u : 0x30000u + go72_rand() % 64u;
        for (uint32_t a = (s0 & ~15u) - 64u; a < s0 + 72u * n + 256u && a < SPU_LS_SIZE; a++) ctx->ls[a] = (uint8_t)go72_rand();
        const uint32_t o = s0 + 72u * n;
        gg[64] = go72_q(o, go72_rand(), go72_rand(), go72_rand());
        for (int i = 0; i < (int)(sizeof(go72_out) / sizeof(go72_out[0])); i++)
            gg[go72_out[i].r] = go72_q(o + go72_out[i].d, go72_rand(), go72_rand(), go72_rand());
        if (it % 60 == 23) gg[34]._u32[0] += 16u;   /* a store pointer off: v1 */
        gg[36] = go72_q(0u - 72u * n, go72_rand(), go72_rand(), go72_rand());
        gg[21] = (it & 4) ? gg[36] : go72_q(0u - 72u * n, 0, 0, 0);
        if (it % 70 == 31) gg[21]._u32[0] += 72u;   /* r21 off: v1 */
        memcpy(pre_ls, ctx->ls, SPU_LS_SIZE); memcpy(pre_g, gg, sizeof(ctx->gpr));
        go72_v1(ctx);
        memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); memcpy(ref_g, gg, sizeof(ctx->gpr));
        memcpy(ctx->ls, pre_ls, SPU_LS_SIZE); memcpy(gg, pre_g, sizeof(ctx->gpr));
        const long t0 = s72_v3_calls;
        go72_v3(ctx);
        took += s72_v3_calls - t0;
        runs++;
        bad_run += memcmp(ctx->ls, ref_ls, SPU_LS_SIZE) != 0 || memcmp(gg, ref_g, sizeof(ctx->gpr)) != 0;
    }
    free(pre_ls); free(ref_ls); free(pre_g); free(ref_g); free(ctx);
    fprintf(stderr, "[edge-o723300] self-test: cflts %ld of 40000 quads differ, the fast conversion %ld of 40000; "
                    "whole loop runs, v3 against v1: %ld of %ld differ (%ld taken by v3, %ld records packed, %ld records' fma by the helper; left to v1: %ld "
                    "short, %ld controls, %ld range, %ld overlap)\n",
            bad_c, bad_k, bad_run, runs, took, s72_pk_recs - pk0, s72_fma_slow - fs0, s72_fb_short - sh0, s72_fb_ctl - ct0,
            s72_fb_rng - rg0, s72_fb_ovl - ov0);
    s72_fb_short = sh0; s72_fb_ctl = ct0; s72_fb_rng = rg0; s72_fb_ovl = ov0; s72_pk_recs = pk0;
    s72_fma_slow = fs0; s72_v3_calls = v30; s72_cyc_pk = cp0;
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s72_mode = -1;

int gow_edge_o723300_mode(void)
{
    if (s72_mode < 0) {
        const char* e = getenv("GOW_EDGE_O723300");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        s72_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o723300] GOW_EDGE_O723300=%s (lifted loop)\n", e); }
        else {
            fprintf(stderr, "[edge-o723300] GOW_EDGE_O723300=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : "v3");
            go72_selftest();
        }
    }
    return s72_mode;
}

static int s72_fault = -1;
static long s72_fault_at, s72_runs;
static void go72_fault(spu_context* ctx)
{
    if (s72_fault < 0) {
        const char* f = getenv("GOW_EDGE_O723300_FAULT");
        s72_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s72_fault_at = k ? atol(k + 1) : 0;
    }
    if (s72_fault <= 0 || s72_runs++ != s72_fault_at) return;
    const u128* r = ctx->gpr;
    /* the last iteration stores at r63 + r21 with r21 = -72 (r21 = r9 = 0 after the loop) */
    const uint32_t q = (r[63]._u32[0] - 72u) & (SPU_LS_MASK & ~0xFu);
    uint32_t at = q;
    switch (s72_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[31]._u32[0] ^= 1; break;
    case 4:   /* record 3 (r64 + r36 + 216 at entry; r64 is not written) */
        at = s72_last_s3 != ~0u ? s72_last_s3 : 0u;
        ctx->ls[at & SPU_LS_MASK] ^= 1;
        break;
    default: break;
    }
    fprintf(stderr, "[edge-o723300] fault %d injected (native run %ld, LS 0x%05X%s)\n", s72_fault, s72_fault_at, at,
            s72_fault == 4 && s72_last_s3 == ~0u ? ": the call was not packed" : "");
    s72_fault = 0;
}

static void go72_run(spu_context* ctx)
{
    if (s72_mode == 2 || s72_mode == 4) { s72_last_s3 = ~0u; go72_v1(ctx); }
    else                                go72_v3(ctx);
    if (__builtin_expect(s72_fault != 0, 0)) go72_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then runs the loop's exit. */
void gow_edge_o723300_loop(spu_context* ctx) { go72_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} go72_check_state;
static _Thread_local go72_check_state* t72_chk;

static long s72_calls, s72_bad, s72_iters;
static uint64_t s72_cyc_nat, s72_cyc_lift;
static int s72_atexit;

/* Calls checked per placement (the blob's LS address the generated switch passes). */
#define GO72_SITES 32
static unsigned s72_site_lsa[GO72_SITES];
static long s72_site_calls[GO72_SITES], s72_site_bad[GO72_SITES];
static _Thread_local unsigned t72_site;

static void go72_check_report(void)
{
    char buf[GO72_SITES * 28] = "", *p = buf;
    for (int i = 0; i < GO72_SITES && s72_site_calls[i]; i++)
        p += snprintf(p, sizeof(buf) - (size_t)(p - buf), " %X:%ld/%ld", s72_site_lsa[i], s72_site_calls[i],
                      s72_site_bad[i]);
    fprintf(stderr, "[edge-o723300-check] calls by placement (blob LS address:calls/differ):%s\n", buf);
    fprintf(stderr, "[edge-o723300-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); tsc: "
                    "native %.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s72_calls, s72_bad, s72_iters, s72_cyc_nat / 1e6, s72_cyc_lift / 1e6,
            s72_iters ? (double)s72_cyc_nat / (double)s72_iters : 0.0,
            s72_iters ? (double)s72_cyc_lift / (double)s72_iters : 0.0);
    fprintf(stderr, "[edge-o723300-check] v3 took %ld calls, %ld records packed; entries left to v1: %ld short, %ld "
                    "other controls or store pointers, %ld outside the LS or unaligned, %ld overlapping; records whose "
                    "fma went to the helper: %ld; tsc in the packed records %.1f M (%.1f per record)\n",
            s72_v3_calls, s72_pk_recs, s72_fb_short, s72_fb_ctl, s72_fb_rng, s72_fb_ovl, s72_fma_slow,
            s72_cyc_pk / 1e6, s72_pk_recs ? (double)s72_cyc_pk / (double)s72_pk_recs : 0.0);
}

void gow_edge_o723300_check_begin_at(spu_context* ctx, unsigned site)
{
    go72_check_state* c = t72_chk;
    if (!c) {
        c = (go72_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t72_chk = c;
    }
    if (!s72_atexit) { s72_atexit = 1; atexit(go72_check_report); }
    t72_site = site;
    s72_iters += (long)((0u - ctx->gpr[36]._u32[0]) / 72u);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    go72_run(ctx);
    s72_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_o723300_check_end(spu_context* ctx)
{
    go72_check_state* c = t72_chk;
    if (!c || !c->pending) return;
    s72_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s72_calls++;
    int site = -1;
    for (int i = 0; i < GO72_SITES; i++) {   /* matching or first free slot (not atomic, like s72_calls) */
        if (s72_site_calls[i] && s72_site_lsa[i] != t72_site) continue;
        s72_site_lsa[i] = t72_site;
        s72_site_calls[i]++;
        site = i;
        break;
    }
    t72_site = 0;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-o723300-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-o723300-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s72_bad++;
        if (site >= 0) s72_site_bad[site]++;
        fprintf(stderr, "[edge-o723300-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) go72_check_report();   /* a game run ends by kill: atexit never runs */
}

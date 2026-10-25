/* gow_edge_native.c -- native replacements for hot loops of the EDGE geometry job module
 * (SPU job module d935, lift spu/jobmod_d93503647dd8_e6d98). Lab only: the production lift
 * never calls into this file; a lab lift does (analysis/edge_native_lift.py), behind the env
 * GOW_EDGE_NATIVE, so one lab exe A/Bs itself. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source (CMakeLists: -O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1,
 * SPU_FP_CHECK_HOOK=0): the float helpers below are the same inline helpers the lift uses, so
 * each float result comes from the same operation on the same operands as in the lifted code.
 * Runs inside spu_run_with_halt, i.e. with MXCSR rounding toward zero.
 *
 * Clean-room: written from our own lift of the module and our own captures.
 *
 * GOW_EDGE_NATIVE (read once):
 *   0/unset  lifted loop
 *   1        native v3 (8-wide, one lane check per iteration); with GOW_EDGE_V2=1: v2 (8-wide,
 *            a lane check per op)
 *   2        native v1 (statement-for-statement transliteration)
 *   3        check (v3, or v2 with GOW_EDGE_V2=1): run it on a copy of the state, then the lifted
 *            loop, and compare all GPRs and the whole LS at loc_EDE8
 *   4        check v1, the same way
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
static inline __m128i gow_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 gow_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = gow_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void gow_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), gow_bswap32x4(x));
}
#define LD(a)    gow_ld(ls, (a))
#define ST(a, v) gow_st(ls, (a), (v))

/* =====================================================================================
 * E550 loop at loc_0000EC08 .. fall-through to loc_0000EDE8
 *
 * Entered only by falling through from the block before it (the back edge is the only
 * jump to EC08), and always runs at least once: do { r48 -= 2; ... } while (r48 != 0).
 * Inputs: r3 r4 r6 r7 r8 r9 r11 r12 r33 r34..r38 r40..r55 r69 r70 r77 r81 r82 r84 r86 r91
 * r93 r94 r95 r97 r102. It writes 57 registers (listed in gow_e550_ec08_v1's stores); every
 * one is left with the lifted loop's final value, since the job replay compares all GPRs at
 * job end.
 * ===================================================================================== */

/* ---- the lifted loop body: GOW_E550_REGS/LOAD_INPUTS/STORE_OUTPUTS/BODY. Game code, so it is not in
 * the repo: gen_edge_e550_body.py writes it from the d935 lift at build time (ctx->gpr[N] -> rN,
 * spu_ls_read128/write128 -> LD/ST). */
#include "gow_edge_e550_body.h"

/* v1: the lifted loop, statement for statement, with the SPU registers in C locals and
 * direct LS access. Same helpers, same operands, same order; only the register file and
 * the LS accessor change. v2 uses it for the last iteration and as its fallback. */
static void gow_e550_ec08_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_E550_REGS()
    GOW_E550_LOAD_INPUTS()
    do {
        GOW_E550_BODY()
    } while (r48._u32[0] != 0);
    GOW_E550_STORE_OUTPUTS()
}

/* ---- v2 building blocks ------------------------------------------------------------- */

typedef __m256i v8;   /* two SPU quadwords: vertex A in the low 128 bits, vertex B in the high */

static inline v8 gow_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline u128 gow_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 gow_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline v8 gow_bcast(u128 a) { __m128i x; memcpy(&x, &a, 16); return _mm256_broadcastsi128_si256(x); }
/* two LS quadwords (each address wrapped and aligned on its own), byte-swapped to register order */
static inline v8 gow_ld2(const uint8_t* ls, uint32_t lo, uint32_t hi)
{
    const __m128i a = _mm_loadu_si128((const __m128i*)(ls + (lo & (SPU_LS_MASK & ~0xFu))));
    const __m128i b = _mm_loadu_si128((const __m128i*)(ls + (hi & (SPU_LS_MASK & ~0xFu))));
    return _mm256_shuffle_epi8(_mm256_set_m128i(b, a),
                               _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline void gow_st1(uint8_t* ls, uint32_t lsa, __m128i v)
{
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), gow_bswap32x4(v));
}

/* shufb with a loop-invariant control, as one pshufb per operand (both 128-bit halves
 * use the same control). Built from the control register's value exactly as
 * spu_shufb_ref reads it: result SPU byte t takes control byte s = c[SPU_W(t)];
 * s & 0xC0 == 0x80 -> 0x00, s & 0xE0 == 0xC0 -> 0xFF, s & 0xE0 == 0xE0 -> 0x80, else
 * byte s & 0x1F of the concatenation {a, b}. In host order SPU byte P is byte SPU_W(P). */
typedef struct { v8 ca, cb, sp; } gow_shuf;
static gow_shuf gow_shuf_make(u128 c)
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
    gow_shuf r;
    u128 t;
    memcpy(t._u8, ca, 16); r.ca = gow_bcast(t);
    memcpy(t._u8, cb, 16); r.cb = gow_bcast(t);
    memcpy(t._u8, sp, 16); r.sp = gow_bcast(t);
    return r;
}
/* shufb(a, b, c), a and b varying */
static inline v8 gow_shuf2(const gow_shuf* s, v8 a, v8 b)
{
    return _mm256_or_si256(_mm256_or_si256(_mm256_shuffle_epi8(a, s->ca), _mm256_shuffle_epi8(b, s->cb)), s->sp);
}
/* shufb(a, K, c) with K loop-invariant too: K's bytes fold into the constant */
static inline gow_shuf gow_shuf_fold(gow_shuf s, u128 k)
{
    s.sp = _mm256_or_si256(s.sp, _mm256_shuffle_epi8(gow_bcast(k), s.cb));
    s.cb = _mm256_set1_epi8((char)0x80);
    return s;
}
static inline v8 gow_shuf1(const gow_shuf* s, v8 a)
{
    return _mm256_or_si256(_mm256_shuffle_epi8(a, s->ca), s->sp);
}

/* fm / fma eight lanes at a time, with the per-lane rule of the SPU_FP_NATIVE helpers
 * (spu__xn_ok, spu__x8_plain0, spu__x8_zero, spu__xn_pos0): the native op rounded toward
 * zero is kept when every input is an ordinary single or +-0 and the result is an
 * ordinary single below the top binade or an exact zero (sign cleared). If any lane fails,
 * the half it is in is redone by the 4-lane helper itself (spu_fm / spu_fma), which returns
 * the native lanes unchanged and fixes the others: the same result the lifted code gets. */
static inline v8 gow_bexp8(v8 x) { return _mm256_srli_epi32(_mm256_slli_epi32(x, 1), 24); }
static inline v8 gow_zero8(v8 x) { return _mm256_cmpeq_epi32(_mm256_slli_epi32(x, 1), _mm256_setzero_si256()); }
static inline v8 gow_plain8(v8 x)
{
    const v8 e = gow_bexp8(x);
    return _mm256_or_si256(_mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()),
                                            _mm256_cmpgt_epi32(_mm256_set1_epi32(255), e)),
                           gow_zero8(x));
}
static inline v8 gow_okr8(v8 r)
{
    const v8 e = gow_bexp8(r);
    return _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
}
static inline v8 gow_pos0_8(v8 r, v8 z) { return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r); }
static inline int gow_all8(v8 m) { return _mm256_movemask_ps(_mm256_castsi256_ps(m)) == 0xFF; }

static long s_fp_slow;   /* ops that took the 4-lane path (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) v8 gow_fm8_slow(v8 a, v8 b)
{
    s_fp_slow++;
    return gow_v8(spu_fm(gow_lo(a), gow_lo(b)), spu_fm(gow_hi(a), gow_hi(b)));
}
static __attribute__((noinline, cold)) v8 gow_fma8_slow(v8 a, v8 b, v8 c)
{
    s_fp_slow++;
    return gow_v8(spu_fma(gow_lo(a), gow_lo(b), gow_lo(c)), spu_fma(gow_hi(a), gow_hi(b), gow_hi(c)));
}
static inline v8 gow_fm8(v8 a, v8 b)
{
    const v8 r = _mm256_castps_si256(_mm256_mul_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)));
    const v8 z = _mm256_or_si256(gow_zero8(a), gow_zero8(b));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(gow_plain8(a), gow_plain8(b)), _mm256_or_si256(gow_okr8(r), z));
    if (__builtin_expect(!gow_all8(ok), 0)) return gow_fm8_slow(a, b);
    return gow_pos0_8(r, z);
}
static inline v8 gow_fma8(v8 a, v8 b, v8 c)   /* a*b + c */
{
    const v8 r = _mm256_castps_si256(_mm256_fmadd_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b), _mm256_castsi256_ps(c)));
    const v8 z = _mm256_and_si256(_mm256_or_si256(gow_zero8(a), gow_zero8(b)), gow_zero8(c));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(_mm256_and_si256(gow_plain8(a), gow_plain8(b)), gow_plain8(c)),
                                   _mm256_or_si256(gow_okr8(r), z));
    if (__builtin_expect(!gow_all8(ok), 0)) return gow_fma8_slow(a, b, c);
    return gow_pos0_8(r, z);
}

/* ---- v2 --------------------------------------------------------------------------------
 * What one iteration does (2 vertices, A and B; from the lift, with the loop constants seen in
 * all 606 loop runs of cap_3 -- see EDGE_NATIVE.md):
 *   - 4 bone offsets per vertex (oA, oB below; bone index * 48) pick 3 rows of 16 bytes at r37,
 *     r35, r34 (0x144C0/D0/E0: one 48-byte matrix per bone, rows in that order; R2/R1/R0 below
 *     are memory rows 0/1/2). Each row is the weighted sum w0*M[o0] + w1*M[o1] + w2*M[o2] +
 *     w3*M[o3] (fm then three fma, in that order). The 3 rows go to r49 (+0x00/0x10/0x20 for A,
 *     +0x30/0x40/0x50 for B) in palette order, after selb with r84 (lanes 0-2 kept; lane 3 is
 *     r97 = 1.0 in memory row 0 and 0 in rows 1 and 2);
 *   - the vertex's 16-byte input at r50 (A) / r50+16 (B), X = (x,y,z,w) and its rotations
 *     Y = (y,z,x,w), Z = (z,x,y,w) (r36, r82), times those rows: P = X*R2m + Y*R1m + Z*R0m + T,
 *     T = the rows' lane 3 (R2.w, R1.w, R0.w, 0) by r70/r69, accumulated in that order; P goes
 *     to r86 (the input's own address one iteration later: in place);
 *   - software pipelined: the iteration also loads the next pair's bone quadword (r51: 8
 *     halfwords, byte 0 = weight, byte 1 = index; A = halfwords 0-3, B = 4-7) and inputs, and
 *     turns them into the next offsets, weights (cuflt(byte, 155) * r91 = byte/255, splat by
 *     shufb) and input rotations.
 * All iterations but the last run below on packed state; the last one runs the lifted body
 * (GOW_E550_BODY) from that state, so the 57 registers the loop writes end exactly as in
 * the lifted loop. Every float op is the same op on the same operands as in the lift. */
static long s_fb_v1, s_fb_v2;   /* loop entries sent to v1 (odd or < 4 count) / to v2 (unexpected controls); racy lab statistics */
static void gow_e550_ec08_v2(spu_context* ctx)
{
    u128* const g = ctx->gpr;
    const uint32_t n0 = g[48]._u32[0];
    /* the loop runs n0/2 times; an odd or zero count wraps (as on the SPU) -- leave that to v1 */
    if (n0 < 4 || (n0 & 1)) { s_fb_v1++; gow_e550_ec08_v1(ctx); return; }
    uint8_t* const ls = ctx->ls;
    const uint32_t niter = n0 / 2;

    const uint32_t b34 = g[34]._u32[0], b35 = g[35]._u32[0], b37 = g[37]._u32[0];
    const v8 m84 = gow_bcast(g[84]), m97 = gow_bcast(g[97]), k91 = gow_bcast(g[91]);
    const gow_shuf s70 = gow_shuf_make(g[70]), s69 = gow_shuf_make(g[69]);
    const gow_shuf s77 = gow_shuf_fold(gow_shuf_make(g[77]), g[81]);
    const gow_shuf s102 = gow_shuf_fold(gow_shuf_make(g[102]), g[81]);
    const gow_shuf s81 = gow_shuf_fold(gow_shuf_make(g[81]), g[81]);
    const gow_shuf s93 = gow_shuf_fold(gow_shuf_make(g[93]), g[81]);
    const gow_shuf s94 = gow_shuf_fold(gow_shuf_make(g[94]), g[81]);
    const gow_shuf s95 = gow_shuf_fold(gow_shuf_make(g[95]), g[81]);
    const gow_shuf s36 = gow_shuf_fold(gow_shuf_make(g[36]), g[81]);
    const gow_shuf s82 = gow_shuf_fold(gow_shuf_make(g[82]), g[81]);

    /* packed loop state (the registers the next iteration reads) */
    uint32_t oA[4] = { g[4]._u32[0], g[6]._u32[0], g[12]._u32[0], g[11]._u32[0] };
    uint32_t oB[4] = { g[3]._u32[0], g[7]._u32[0], g[9]._u32[0], g[8]._u32[0] };
    v8 W0 = gow_v8(g[41], g[33]), W1 = gow_v8(g[42], g[40]), W2 = gow_v8(g[43], g[38]), W3 = gow_v8(g[45], g[44]);
    v8 X = gow_v8(g[46], g[47]), Y = gow_v8(g[54], g[55]), Z = gow_v8(g[53], g[52]);
    uint32_t p49 = g[49]._u32[0], p50 = g[50]._u32[0], p51 = g[51]._u32[0], p86 = g[86]._u32[0];
    u128 raw = g[6];   /* placeholder; set by the first iteration */

    for (uint32_t it = 1; it < niter; it++) {
        /* the blended rows, A low / B high */
        v8 R0, R1, R2;
        {
            const v8 M00 = gow_ld2(ls, b34 + oA[0], b34 + oB[0]), M01 = gow_ld2(ls, b34 + oA[1], b34 + oB[1]);
            const v8 M02 = gow_ld2(ls, b34 + oA[2], b34 + oB[2]), M03 = gow_ld2(ls, b34 + oA[3], b34 + oB[3]);
            const v8 M10 = gow_ld2(ls, b35 + oA[0], b35 + oB[0]), M11 = gow_ld2(ls, b35 + oA[1], b35 + oB[1]);
            const v8 M12 = gow_ld2(ls, b35 + oA[2], b35 + oB[2]), M13 = gow_ld2(ls, b35 + oA[3], b35 + oB[3]);
            const v8 M20 = gow_ld2(ls, b37 + oA[0], b37 + oB[0]), M21 = gow_ld2(ls, b37 + oA[1], b37 + oB[1]);
            const v8 M22 = gow_ld2(ls, b37 + oA[2], b37 + oB[2]), M23 = gow_ld2(ls, b37 + oA[3], b37 + oB[3]);
            R0 = gow_fma8(W3, M03, gow_fma8(W2, M02, gow_fma8(W1, M01, gow_fm8(W0, M00))));
            R1 = gow_fma8(W3, M13, gow_fma8(W2, M12, gow_fma8(W1, M11, gow_fm8(W0, M10))));
            R2 = gow_fma8(W3, M23, gow_fma8(W2, M22, gow_fma8(W1, M21, gow_fm8(W0, M20))));
        }
        /* next pair's inputs: all loads of an iteration come before its stores */
        raw = gow_ld(ls, p51);
        const v8 Xn = gow_ld2(ls, p50, p50 + 0x10);

        const v8 R0m = _mm256_and_si256(R0, m84);                                   /* r15 | r14 */
        const v8 R1m = _mm256_and_si256(R1, m84);                                   /* r41 | r38 */
        const v8 R2m = _mm256_or_si256(_mm256_andnot_si256(m84, m97), _mm256_and_si256(R2, m84));  /* r43 | r42 */
        const v8 T = gow_shuf2(&s69, gow_shuf2(&s70, R1, R0), R2);                 /* r45 | r44 */
        const v8 P = gow_fma8(Z, R0m, gow_fma8(Y, R1m, gow_fma8(X, R2m, T)));      /* r21 | r83 */

        /* next weights: cuflt(byte, 155) * r91, splat per bone */
        const v8 rw = gow_bcast(raw);
        const v8 wf = gow_fm8(gow_v8(spu_cuflt(gow_lo(gow_shuf1(&s77, rw)), 155),
                                     spu_cuflt(gow_lo(gow_shuf1(&s102, rw)), 155)), k91);   /* r20 | r19 */
        W0 = gow_shuf1(&s81, wf); W1 = gow_shuf1(&s95, wf); W2 = gow_shuf1(&s94, wf); W3 = gow_shuf1(&s93, wf);
        X = Xn; Y = gow_shuf1(&s36, Xn); Z = gow_shuf1(&s82, Xn);
        /* next offsets: (halfword & 0xFF) * 48 per word (andhi, shli 4 + shli 5), and rotated by 16 */
        {
            __m128i m; memcpy(&m, &raw, 16);
            m = _mm_and_si128(m, _mm_set1_epi32(0x00FF00FF));
            const __m128i w = _mm_add_epi32(_mm_slli_epi32(m, 5), _mm_slli_epi32(m, 4));
            const __m128i r = _mm_or_si128(_mm_slli_epi32(w, 16), _mm_srli_epi32(w, 16));
            oA[0] = (uint32_t)_mm_extract_epi32(r, 0); oA[1] = (uint32_t)_mm_extract_epi32(w, 0);
            oA[2] = (uint32_t)_mm_extract_epi32(r, 1); oA[3] = (uint32_t)_mm_extract_epi32(w, 1);
            oB[0] = (uint32_t)_mm_extract_epi32(r, 2); oB[1] = (uint32_t)_mm_extract_epi32(w, 2);
            oB[2] = (uint32_t)_mm_extract_epi32(r, 3); oB[3] = (uint32_t)_mm_extract_epi32(w, 3);
        }

        /* stores, in the lifted order: the rows at r49, then the two results at r86 */
        gow_st1(ls, p49 + 0x00, _mm256_castsi256_si128(R2m));
        gow_st1(ls, p49 + 0x10, _mm256_castsi256_si128(R1m));
        gow_st1(ls, p49 + 0x30, _mm256_extracti128_si256(R2m, 1));
        gow_st1(ls, p49 + 0x40, _mm256_extracti128_si256(R1m, 1));
        gow_st1(ls, p49 + 0x20, _mm256_castsi256_si128(R0m));
        gow_st1(ls, p49 + 0x50, _mm256_extracti128_si256(R0m, 1));
        gow_st1(ls, p86 + 0x00, _mm256_castsi256_si128(P));
        gow_st1(ls, p86 + 0x10, _mm256_extracti128_si256(P, 1));
        p49 += 96; p50 += 32; p51 += 16; p86 += 32;
    }

    /* the last iteration: rebuild the registers it reads and run the lifted body */
    GOW_E550_REGS()
    GOW_E550_LOAD_INPUTS()
    {
        const uint32_t k = niter - 1;
        const __m128i d48 = _mm_set1_epi32((int)(0u - 2u * k)), d49 = _mm_set1_epi32((int)(96u * k));
        const __m128i d50 = _mm_set1_epi32((int)(32u * k)), d51 = _mm_set1_epi32((int)(16u * k));
        __m128i t;
        memcpy(&t, &r48, 16); t = _mm_add_epi32(t, d48); memcpy(&r48, &t, 16);
        memcpy(&t, &r49, 16); t = _mm_add_epi32(t, d49); memcpy(&r49, &t, 16);
        memcpy(&t, &r50, 16); t = _mm_add_epi32(t, d50); memcpy(&r50, &t, 16);
        memcpy(&t, &r51, 16); t = _mm_add_epi32(t, d51); memcpy(&r51, &t, 16);
        memcpy(&t, &r86, 16); t = _mm_add_epi32(t, d50); memcpy(&r86, &t, 16);
        /* the offset registers, from the last bone quadword exactly as the body forms them */
        r33 = spu_andhi(raw, 255);
        r16 = spu_shli(r33, 4);
        r13 = spu_shli(r33, 5);
        r6 = spu_a(r13, r16);
        r11 = spu_shlqbyi(r6, 4);
        r7 = spu_shlqbyi(r6, 8);
        r8 = spu_shlqbyi(r6, 12);
        r4 = spu_roti(r6, 16);
        r12 = spu_roti(r11, 16);
        r3 = spu_roti(r7, 16);
        r9 = spu_roti(r8, 16);
        r41 = gow_lo(W0); r33 = gow_hi(W0); r42 = gow_lo(W1); r40 = gow_hi(W1);
        r43 = gow_lo(W2); r38 = gow_hi(W2); r45 = gow_lo(W3); r44 = gow_hi(W3);
        r46 = gow_lo(X); r47 = gow_hi(X); r54 = gow_lo(Y); r55 = gow_hi(Y); r53 = gow_lo(Z); r52 = gow_hi(Z);
    }
    GOW_E550_BODY()
    GOW_E550_STORE_OUTPUTS()
}

/* ---- v3: v2 with the float-lane checks done once per iteration ----------------------
 * v2 checks every lane of every op and branches per op; that check was ~3/4 of its
 * instructions. v3 computes an iteration speculatively and keeps it only if every lane of
 * every op passes the helpers' rule; otherwise it recomputes that iteration with v2's
 * exact ops (nothing is stored before the decision, and no store sits between the loads
 * and the recompute, so the inputs are the same). The rule per op stays exactly the
 * helpers': inputs ordinary or +-0, and the result ordinary below the top binade
 * (exponent 1..253) or a zero from a zero factor (fma: and a zero addend), which then
 * becomes +0. What changes is how it is established:
 *   - LS data (the 12 matrix rows) and the cuflt results are checked as loaded;
 *   - an op's result is ordinary or zero whenever the iteration passes, so it is not
 *     rechecked where it feeds the next op, and its zero lanes are the op's zero-factor
 *     lanes;
 *   - lanes moved by a shufb whose control picks whole aligned words or zero words, or
 *     selected by a word mask (r84), or taken from a checked constant (r97, r91), are as
 *     ordinary as their source. The controls and masks are verified at entry; if one is
 *     not of that form v2 runs instead;
 *   - the weights and inputs carried into an iteration are ordinary if the previous one
 *     passed (the next inputs are checked as loaded, the weights are op results); after an
 *     exact recompute they are checked, and if they fail the next iteration is exact too.
 *     At entry they come from registers and are checked there.
 * The range tests use the value shifted left by one (sign dropped): s >= 0x01000000 means
 * exponent >= 1, s < 0xFE000000 exponent <= 253. One running unsigned min and max per
 * iteration collect them; zero lanes that are allowed are lifted out of the min. A matrix
 * row with exponent 254 (allowed as an input) or exactly +-2^-126 trips them too: that only
 * sends the iteration to the exact ops. */
static long s_v3_redo;   /* iterations recomputed exactly (lab statistic; racy across threads) */

typedef struct { v8 mn, mx; } gow_rng;   /* running unsigned min / max of shifted values */
static inline void gow_rng_add(gow_rng* g, v8 lo, v8 hi)
{
    g->mn = _mm256_min_epu32(g->mn, lo);
    g->mx = _mm256_max_epu32(g->mx, hi);
}
/* an LS operand: track its range, return its zero lanes */
static inline v8 gow_chk_in(gow_rng* g, v8 x)
{
    const v8 s = _mm256_add_epi32(x, x);
    const v8 z = _mm256_cmpeq_epi32(s, _mm256_setzero_si256());
    gow_rng_add(g, _mm256_or_si256(s, z), s);
    return z;
}
/* zero lanes of a value known to be ordinary or zero */
static inline v8 gow_zf8(v8 x) { return _mm256_castps_si256(_mm256_cmp_ps(_mm256_castsi256_ps(x), _mm256_setzero_ps(), _CMP_EQ_OQ)); }
/* an op result r with zero-factor lanes zz: track its range (zz lanes out of the min), return it with +0 there */
static inline v8 gow_chk_out(gow_rng* g, v8 r, v8 zz)
{
    const v8 s = _mm256_add_epi32(r, r);
    gow_rng_add(g, _mm256_or_si256(s, zz), s);
    return _mm256_andnot_si256(zz, r);
}
static inline v8 gow_sfm(gow_rng* g, v8 a, v8 za, v8 b, v8 zb, v8* z)
{
    const v8 r = _mm256_castps_si256(_mm256_mul_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)));
    *z = _mm256_or_si256(za, zb);
    return gow_chk_out(g, r, *z);
}
static inline v8 gow_sfma(gow_rng* g, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)
{
    const v8 r = _mm256_castps_si256(_mm256_fmadd_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b), _mm256_castsi256_ps(c)));
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return gow_chk_out(g, r, *z);
}
static inline int gow_rng_ok(const gow_rng* g)
{
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(g->mn, _mm256_set1_epi32(0x01000000)), g->mn);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(g->mx, _mm256_set1_epi32((int)0xFDFFFFFFu)), g->mx);
    return _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_and_si256(lo, hi))) == 0xFF;
}
/* every lane ordinary or +-0 (the helpers' input rule, exactly) */
static inline int gow_plain_all8(v8 x)
{
    const v8 s = _mm256_add_epi32(x, x);
    const v8 z = _mm256_cmpeq_epi32(s, _mm256_setzero_si256());
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(s, _mm256_set1_epi32(0x01000000)), s);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(s, _mm256_set1_epi32((int)0xFEFFFFFFu)), s);
    return _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_or_si256(_mm256_and_si256(lo, hi), z))) == 0xFF;
}

/* a shufb control whose every result word is one aligned source word in order (from a,
 * or from b when allow_b) or a zero word */
static int gow_ctl_words(u128 c, int allow_b)
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
        if (base >= 16 && !allow_b) return 0;
    }
    return 1;
}
static int gow_plain4(u128 x)
{
    for (int i = 0; i < 4; i++) {
        const uint32_t e = (x._u32[i] >> 23) & 0xFF;
        if (e == 255 || (e == 0 && (x._u32[i] << 1) != 0)) return 0;
    }
    return 1;
}
/* a shufb control whose every result word has a zero top byte (SPU byte 0 of the word is a
 * 0x00 selector), so the word is below 2^24: cuflt(x, 155) of it is the exact conversion */
static int gow_ctl_small(u128 c)
{
    for (int w = 0; w < 4; w++) if ((c._u8[SPU_W(4 * w)] & 0xC0) != 0x80) return 0;
    return 1;
}
static int gow_wordmask(u128 m)
{
    for (int i = 0; i < 4; i++) if (m._u32[i] != 0 && m._u32[i] != 0xFFFFFFFFu) return 0;
    return 1;
}

static void gow_e550_ec08_v3(spu_context* ctx)
{
    u128* const g = ctx->gpr;
    const uint32_t n0 = g[48]._u32[0];
    if (n0 < 4 || (n0 & 1)) { s_fb_v1++; gow_e550_ec08_v1(ctx); return; }
    if (!gow_ctl_words(g[36], 0) || !gow_ctl_words(g[81], 0) || !gow_ctl_words(g[82], 0) ||
        !gow_ctl_words(g[93], 0) || !gow_ctl_words(g[94], 0) || !gow_ctl_words(g[95], 0) ||
        !gow_ctl_words(g[69], 1) || !gow_ctl_words(g[70], 1) || !gow_wordmask(g[84]) ||
        !gow_plain4(g[97]) || !gow_plain4(g[91]) || !gow_ctl_small(g[77]) || !gow_ctl_small(g[102]))
        { s_fb_v2++; gow_e550_ec08_v2(ctx); return; }
    uint8_t* const ls = ctx->ls;
    const uint32_t niter = n0 / 2;

    const uint32_t b34 = g[34]._u32[0], b35 = g[35]._u32[0], b37 = g[37]._u32[0];
    const v8 m84 = gow_bcast(g[84]), m97 = gow_bcast(g[97]), k91 = gow_bcast(g[91]);
    const v8 z91 = gow_zf8(k91), m97o = _mm256_andnot_si256(m84, m97);
    const gow_shuf s70 = gow_shuf_make(g[70]), s69 = gow_shuf_make(g[69]);
    /* the weight bytes: shufb(raw, r81, r77) for vertex A (low half), r102 for B (high half) */
    gow_shuf swb;
    {
        const gow_shuf s77 = gow_shuf_fold(gow_shuf_make(g[77]), g[81]);
        const gow_shuf s102 = gow_shuf_fold(gow_shuf_make(g[102]), g[81]);
        swb.ca = _mm256_permute2x128_si256(s77.ca, s102.ca, 0x20);
        swb.sp = _mm256_permute2x128_si256(s77.sp, s102.sp, 0x20);
        swb.cb = s77.cb;
    }
    const gow_shuf s81 = gow_shuf_fold(gow_shuf_make(g[81]), g[81]);
    const gow_shuf s93 = gow_shuf_fold(gow_shuf_make(g[93]), g[81]);
    const gow_shuf s94 = gow_shuf_fold(gow_shuf_make(g[94]), g[81]);
    const gow_shuf s95 = gow_shuf_fold(gow_shuf_make(g[95]), g[81]);
    const gow_shuf s36 = gow_shuf_fold(gow_shuf_make(g[36]), g[81]);
    const gow_shuf s82 = gow_shuf_fold(gow_shuf_make(g[82]), g[81]);

    uint32_t oA[4] = { g[4]._u32[0], g[6]._u32[0], g[12]._u32[0], g[11]._u32[0] };
    uint32_t oB[4] = { g[3]._u32[0], g[7]._u32[0], g[9]._u32[0], g[8]._u32[0] };
    v8 W0 = gow_v8(g[41], g[33]), W1 = gow_v8(g[42], g[40]), W2 = gow_v8(g[43], g[38]), W3 = gow_v8(g[45], g[44]);
    v8 X = gow_v8(g[46], g[47]), Y = gow_v8(g[54], g[55]), Z = gow_v8(g[53], g[52]);
    uint32_t p49 = g[49]._u32[0], p50 = g[50]._u32[0], p51 = g[51]._u32[0], p86 = g[86]._u32[0];
    u128 raw = g[6];
    /* may the next iteration run speculatively: its carried inputs are ordinary or zero */
    int spec = gow_plain4(g[41]) && gow_plain4(g[33]) && gow_plain4(g[42]) && gow_plain4(g[40]) &&
               gow_plain4(g[43]) && gow_plain4(g[38]) && gow_plain4(g[45]) && gow_plain4(g[44]) &&
               gow_plain4(g[46]) && gow_plain4(g[47]) && gow_plain4(g[54]) && gow_plain4(g[55]) &&
               gow_plain4(g[53]) && gow_plain4(g[52]);

#define GOW_ROWLD(b, k) gow_ld2(ls, (b) + oA[k], (b) + oB[k])
    for (uint32_t it = 1; it < niter; it++) {
        raw = gow_ld(ls, p51);
        const v8 Xn = gow_ld2(ls, p50, p50 + 0x10);
        const v8 rw = gow_bcast(raw);
        /* cuflt(x, 155) of a word below 2^24 (checked at entry) is the plain conversion, exact in any rounding mode */
        const v8 cuf = _mm256_castps_si256(_mm256_cvtepi32_ps(gow_shuf1(&swb, rw)));

        v8 R0m, R1m, R2m, P, wf;
        int done = 0;
        if (spec) {
            gow_rng rg = { _mm256_set1_epi32(-1), _mm256_setzero_si256() };
            const v8 zw0 = gow_zf8(W0), zw1 = gow_zf8(W1), zw2 = gow_zf8(W2), zw3 = gow_zf8(W3);
            v8 z, zm, M, R0, R1, R2, t;
#define GOW_SROW(R, b)                                                                  \
            M = GOW_ROWLD(b, 0); zm = gow_chk_in(&rg, M); R = gow_sfm(&rg, W0, zw0, M, zm, &z);           \
            M = GOW_ROWLD(b, 1); zm = gow_chk_in(&rg, M); R = gow_sfma(&rg, W1, zw1, M, zm, R, z, &z);    \
            M = GOW_ROWLD(b, 2); zm = gow_chk_in(&rg, M); R = gow_sfma(&rg, W2, zw2, M, zm, R, z, &z);    \
            M = GOW_ROWLD(b, 3); zm = gow_chk_in(&rg, M); R = gow_sfma(&rg, W3, zw3, M, zm, R, z, &z);
            GOW_SROW(R0, b34)
            GOW_SROW(R1, b35)
            GOW_SROW(R2, b37)
#undef GOW_SROW
            R0m = _mm256_and_si256(R0, m84);
            R1m = _mm256_and_si256(R1, m84);
            R2m = _mm256_or_si256(m97o, _mm256_and_si256(R2, m84));
            const v8 T = gow_shuf2(&s69, gow_shuf2(&s70, R1, R0), R2);
            P = gow_sfma(&rg, X, gow_zf8(X), R2m, gow_zf8(R2m), T, gow_zf8(T), &z);
            P = gow_sfma(&rg, Y, gow_zf8(Y), R1m, gow_zf8(R1m), P, z, &z);
            P = gow_sfma(&rg, Z, gow_zf8(Z), R0m, gow_zf8(R0m), P, z, &z);
            const v8 zc = gow_chk_in(&rg, cuf);
            wf = gow_sfm(&rg, cuf, zc, k91, z91, &t);
            done = gow_rng_ok(&rg);
        }
        int wf_plain = 1;
        if (!done) {   /* the exact ops, as v2 */
            if (spec) s_v3_redo++;
            const v8 R0 = gow_fma8(W3, GOW_ROWLD(b34, 3), gow_fma8(W2, GOW_ROWLD(b34, 2),
                          gow_fma8(W1, GOW_ROWLD(b34, 1), gow_fm8(W0, GOW_ROWLD(b34, 0)))));
            const v8 R1 = gow_fma8(W3, GOW_ROWLD(b35, 3), gow_fma8(W2, GOW_ROWLD(b35, 2),
                          gow_fma8(W1, GOW_ROWLD(b35, 1), gow_fm8(W0, GOW_ROWLD(b35, 0)))));
            const v8 R2 = gow_fma8(W3, GOW_ROWLD(b37, 3), gow_fma8(W2, GOW_ROWLD(b37, 2),
                          gow_fma8(W1, GOW_ROWLD(b37, 1), gow_fm8(W0, GOW_ROWLD(b37, 0)))));
            R0m = _mm256_and_si256(R0, m84);
            R1m = _mm256_and_si256(R1, m84);
            R2m = _mm256_or_si256(_mm256_andnot_si256(m84, m97), _mm256_and_si256(R2, m84));
            const v8 T = gow_shuf2(&s69, gow_shuf2(&s70, R1, R0), R2);
            P = gow_fma8(Z, R0m, gow_fma8(Y, R1m, gow_fma8(X, R2m, T)));
            wf = gow_fm8(cuf, k91);
            wf_plain = gow_plain_all8(wf);
        }
        spec = wf_plain && gow_plain_all8(Xn);

        W0 = gow_shuf1(&s81, wf); W1 = gow_shuf1(&s95, wf); W2 = gow_shuf1(&s94, wf); W3 = gow_shuf1(&s93, wf);
        X = Xn; Y = gow_shuf1(&s36, Xn); Z = gow_shuf1(&s82, Xn);
        {
            __m128i m; memcpy(&m, &raw, 16);
            m = _mm_and_si128(m, _mm_set1_epi32(0x00FF00FF));
            const __m128i w = _mm_add_epi32(_mm_slli_epi32(m, 5), _mm_slli_epi32(m, 4));
            const __m128i r = _mm_or_si128(_mm_slli_epi32(w, 16), _mm_srli_epi32(w, 16));
            oA[0] = (uint32_t)_mm_extract_epi32(r, 0); oA[1] = (uint32_t)_mm_extract_epi32(w, 0);
            oA[2] = (uint32_t)_mm_extract_epi32(r, 1); oA[3] = (uint32_t)_mm_extract_epi32(w, 1);
            oB[0] = (uint32_t)_mm_extract_epi32(r, 2); oB[1] = (uint32_t)_mm_extract_epi32(w, 2);
            oB[2] = (uint32_t)_mm_extract_epi32(r, 3); oB[3] = (uint32_t)_mm_extract_epi32(w, 3);
        }
        gow_st1(ls, p49 + 0x00, _mm256_castsi256_si128(R2m));
        gow_st1(ls, p49 + 0x10, _mm256_castsi256_si128(R1m));
        gow_st1(ls, p49 + 0x30, _mm256_extracti128_si256(R2m, 1));
        gow_st1(ls, p49 + 0x40, _mm256_extracti128_si256(R1m, 1));
        gow_st1(ls, p49 + 0x20, _mm256_castsi256_si128(R0m));
        gow_st1(ls, p49 + 0x50, _mm256_extracti128_si256(R0m, 1));
        gow_st1(ls, p86 + 0x00, _mm256_castsi256_si128(P));
        gow_st1(ls, p86 + 0x10, _mm256_extracti128_si256(P, 1));
        p49 += 96; p50 += 32; p51 += 16; p86 += 32;
    }
#undef GOW_ROWLD

    GOW_E550_REGS()
    GOW_E550_LOAD_INPUTS()
    {
        const uint32_t k = niter - 1;
        const __m128i d48 = _mm_set1_epi32((int)(0u - 2u * k)), d49 = _mm_set1_epi32((int)(96u * k));
        const __m128i d50 = _mm_set1_epi32((int)(32u * k)), d51 = _mm_set1_epi32((int)(16u * k));
        __m128i t;
        memcpy(&t, &r48, 16); t = _mm_add_epi32(t, d48); memcpy(&r48, &t, 16);
        memcpy(&t, &r49, 16); t = _mm_add_epi32(t, d49); memcpy(&r49, &t, 16);
        memcpy(&t, &r50, 16); t = _mm_add_epi32(t, d50); memcpy(&r50, &t, 16);
        memcpy(&t, &r51, 16); t = _mm_add_epi32(t, d51); memcpy(&r51, &t, 16);
        memcpy(&t, &r86, 16); t = _mm_add_epi32(t, d50); memcpy(&r86, &t, 16);
        r33 = spu_andhi(raw, 255);
        r16 = spu_shli(r33, 4);
        r13 = spu_shli(r33, 5);
        r6 = spu_a(r13, r16);
        r11 = spu_shlqbyi(r6, 4);
        r7 = spu_shlqbyi(r6, 8);
        r8 = spu_shlqbyi(r6, 12);
        r4 = spu_roti(r6, 16);
        r12 = spu_roti(r11, 16);
        r3 = spu_roti(r7, 16);
        r9 = spu_roti(r8, 16);
        r41 = gow_lo(W0); r33 = gow_hi(W0); r42 = gow_lo(W1); r40 = gow_hi(W1);
        r43 = gow_lo(W2); r38 = gow_hi(W2); r45 = gow_lo(W3); r44 = gow_hi(W3);
        r46 = gow_lo(X); r47 = gow_hi(X); r54 = gow_lo(Y); r55 = gow_hi(Y); r53 = gow_lo(Z); r52 = gow_hi(Z);
    }
    GOW_E550_BODY()
    GOW_E550_STORE_OUTPUTS()
}

/* ---- entry points the lab lift calls ---------------------------------------------- */
static int s_edge_mode = -1;
static int s_edge_v2;   /* GOW_EDGE_V2=1: modes 1 and 3 use v2 instead of v3 */

int gow_edge_native_mode(void)
{
    if (s_edge_mode < 0) {
        const char* e = getenv("GOW_EDGE_NATIVE");
        int m = e && *e ? atoi(e) : 1;   /* default: native v3 */
        if (m < 0 || m > 4) m = 1;
        const char* v = getenv("GOW_EDGE_V2");
        s_edge_v2 = v && atoi(v) == 1;
        s_edge_mode = m;
        if (!m) fprintf(stderr, "[edge-native] GOW_EDGE_NATIVE=0 (lifted loop)\n");
        else fprintf(stderr, "[edge-native] GOW_EDGE_NATIVE=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                     (m == 2 || m == 4) ? "v1" : s_edge_v2 ? "v2" : "v3");
    }
    return s_edge_mode;
}

/* GOW_EDGE_FAULT (negative test of the gates and of check mode): 1 flips bit 0 of an LS byte
 * the first native run stores (the last pair's A result, at r86-32+15), 2 flips bit 0 of r5. */
static int s_edge_fault = -1;
static void gow_e550_ec08_run(spu_context* ctx)
{
    if (s_edge_mode == 2 || s_edge_mode == 4) gow_e550_ec08_v1(ctx);
    else if (s_edge_v2)                       gow_e550_ec08_v2(ctx);
    else                                      gow_e550_ec08_v3(ctx);
    if (__builtin_expect(s_edge_fault != 0, 0)) {
        if (s_edge_fault < 0) { const char* f = getenv("GOW_EDGE_FAULT"); s_edge_fault = f ? atoi(f) : 0; }
        if (s_edge_fault == 1) ctx->ls[(ctx->gpr[86]._u32[0] - 32u + 15u) & SPU_LS_MASK] ^= 1;
        if (s_edge_fault == 2) ctx->gpr[5]._u32[0] ^= 1;
        if (s_edge_fault > 0) { fprintf(stderr, "[edge-native] fault %d injected\n", s_edge_fault); s_edge_fault = 0; }
    }
}

/* Modes 1 and 2: replaces the whole loop; the lift then jumps to loc_0000EDE8. */
void gow_edge_e550_ec08(spu_context* ctx)
{
    gow_e550_ec08_run(ctx);
}

/* Modes 3 and 4 (check): at loc_EC08 run the native loop on the live state, keep its
 * result, put the state back and let the lifted loop run; at loc_EDE8 compare all 128
 * GPRs and the whole LS. EDE8 is also reached from the loop at EA7C, hence `pending`. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} gow_check_state;
static _Thread_local gow_check_state* t_chk;

static long s_chk_calls, s_chk_bad, s_chk_iters, s_chk_const_diff;
static uint64_t s_cyc_nat, s_cyc_lift;   /* rdtsc around each loop (check modes) */
static long s_chk_rows48;                 /* loop entries with r35 = r37 + 16 and r34 = r37 + 32 */
static long s_chk_inplace;                /* loop entries with r86 = r50 - 32 */
static u128 s_chk_const[13];
static const int k_const_regs[13] = { 36, 69, 70, 77, 81, 82, 84, 91, 93, 94, 95, 97, 102 };

static void gow_check_report(void)
{
    fprintf(stderr, "[edge-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); "
                    "loop constants differ from the first call in %ld calls\n",
            s_chk_calls, s_chk_bad, s_chk_iters, s_chk_const_diff);
    fprintf(stderr, "[edge-check] r35 = r37 + 16 and r34 = r37 + 32 in %ld of %ld calls; r86 = r50 - 32 in %ld\n",
            s_chk_rows48, s_chk_calls, s_chk_inplace);
    fprintf(stderr, "[edge-check] fallbacks: %ld loop entries to v1 (odd or < 4 count), %ld to v2 (unexpected controls)\n",
            s_fb_v1, s_fb_v2);
    fprintf(stderr, "[edge-check] v3 iterations recomputed exactly: %ld; 8-lane ops redone by the 4-lane helpers: %ld; "
                    "tsc: native %.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s_v3_redo, s_fp_slow, s_cyc_nat / 1e6, s_cyc_lift / 1e6,
            s_chk_iters ? (double)s_cyc_nat / (double)s_chk_iters : 0.0,
            s_chk_iters ? (double)s_cyc_lift / (double)s_chk_iters : 0.0);
    if (s_chk_calls) {
        for (int i = 0; i < 13; i++)
            fprintf(stderr, "[edge-check]   r%-3d = %08X %08X %08X %08X\n", k_const_regs[i],
                    s_chk_const[i]._u32[0], s_chk_const[i]._u32[1], s_chk_const[i]._u32[2], s_chk_const[i]._u32[3]);
    }
}

void gow_edge_e550_check_begin(spu_context* ctx)
{
    gow_check_state* c = t_chk;
    if (!c) {
        c = (gow_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t_chk = c;
    }
    if (s_chk_calls == 0) {
        for (int i = 0; i < 13; i++) s_chk_const[i] = ctx->gpr[k_const_regs[i]];
        atexit(gow_check_report);
    } else {
        for (int i = 0; i < 13; i++)
            if (memcmp(&s_chk_const[i], &ctx->gpr[k_const_regs[i]], 16)) { s_chk_const_diff++; break; }
    }
    s_chk_iters += ctx->gpr[48]._u32[0] / 2;
    {   /* layout facts for the doc: the three row bases and the streams */
        const u128* r = ctx->gpr;
        if (r[35]._u32[0] == r[37]._u32[0] + 16 && r[34]._u32[0] == r[37]._u32[0] + 32) s_chk_rows48++;
        if (r[86]._u32[0] == r[50]._u32[0] - 32) s_chk_inplace++;
        if (s_chk_calls < 4)
            fprintf(stderr, "[edge-check] call %ld: r34 %05X r35 %05X r37 %05X  r48 %u  r49 %05X r50 %05X r51 %05X r86 %05X\n",
                    s_chk_calls, r[34]._u32[0], r[35]._u32[0], r[37]._u32[0], r[48]._u32[0], r[49]._u32[0],
                    r[50]._u32[0], r[51]._u32[0], r[86]._u32[0]);
    }
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    gow_e550_ec08_run(ctx);
    s_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_e550_check_end(spu_context* ctx)
{
    gow_check_state* c = t_chk;
    if (!c || !c->pending) return;
    s_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s_chk_calls++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s_chk_bad++;
        fprintf(stderr, "[edge-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) gow_check_report();   /* a game run ends by kill: atexit never runs */
}

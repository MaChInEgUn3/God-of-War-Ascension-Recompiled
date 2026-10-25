/* gow_edge_native_o11430.c -- native replacement for the loop of function 11430 in ONE EDGE overlay fragment
 * lift: edgefrag_7228c0_11150 (overlay 0x7228C0, entry 0x11150; spu/edgefrag_7228c0_11150), the loop at
 * loc_00011460 (exit: the fall-through after its back edge, then 11430's return). The fragment's lift is
 * built through gen_edge_frag_lift.py, which puts a switch before the loop; GOW_EDGE_O11430 picks the path.
 * See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native.c (-O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1,
 * SPU_FP_CHECK_HOOK=0): the float helpers are the same inline helpers the lift uses. Runs inside
 * spu_run_with_halt, i.e. with MXCSR rounding toward zero.
 *
 * Clean-room: written from our own lift of the fragment and our own captures.
 *
 * GOW_EDGE_O11430 (read once):
 *   unset/0  lifted loop (the default)
 *   1        native, packed (the two records of an iteration in one vector, one range check per iteration:
 *            "v3"); with GOW_EDGE_O11430_V2=1 every iteration runs the checked fa instead ("v2": the
 *            per-lane rule and the 4-lane fallback)
 *   2        native v1 (the loop statement for statement, registers in locals)
 *   3        check the packed version (v3, or v2 with GOW_EDGE_O11430_V2=1): run it on a copy of the state,
 *            then the lifted loop, and compare all GPRs and the whole LS at the loop exit
 *   4        check v1, the same way
 * GOW_EDGE_O11430_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 3 of the first quad the last iteration stores (the LSB of its x), 3 bit 0 of byte 1 of that quad
 * (x's mantissa bit 16), 2 bit 0 of r9 after the loop.
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
static inline __m128i go14_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 go14_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = go14_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void go14_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), go14_bswap32x4(x));
}
#define LD(a)    go14_ld(ls, (a))
#define ST(a, v) go14_st(ls, (a), (v))

/* the lifted loop: GOW_O1430_*. Game code, so it is not in the repo: gen_edge_o11430_body.py writes it from
 * the fragment's lift at build time (and checks its shape, its inputs and its dataflow). */
#include "gow_edge_o11430_body.h"

/* =====================================================================================================
 * v1: the loop statement for statement, registers in C locals, direct LS access. The control flow is the
 * lift's: 11460: BODY; if (r19 != 0) goto 11460;
 * ===================================================================================================== */
static void go14_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O1430_REGS()
    GOW_O1430_LOAD()
    do {
        GOW_O1430_BODY()
    } while (r19._u32[0] != 0);
    GOW_O1430_STORE()
}

/* ---- 8-lane building blocks (here: the iteration's two records, the r9 one in the low half, r5's in the high) */
typedef __m256i v8;

static inline v8 go14_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 go14_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(a); }
static inline u128 go14_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 go14_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline void go14_stlo(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), go14_bswap32x4(_mm256_castsi256_si128(v))); }
static inline void go14_sthi(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), go14_bswap32x4(_mm256_extracti128_si256(v, 1))); }
/* a quad's bytes in SPU (memory) order, in both halves */
static inline v8 go14_raw_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(go14_bswap32x4(a)); }
static inline v8 go14_raw_ls(const uint8_t* ls, uint32_t a) { return _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i*)(ls + a))); }
static inline v8 go14_f(__m256 x) { return _mm256_castps_si256(x); }
static inline __m256 go14_p(v8 x) { return _mm256_castsi256_ps(x); }

/* fa eight lanes at a time, with the per-lane rule of the SPU_FP_NATIVE helper (spu_helpers.h,
 * spu__xn_fa): the native add rounded toward zero is kept when both inputs are ordinary singles (exponent
 * 1..254) or +-0 and the result is an ordinary single below the top binade, or zero (a sum of singles never
 * rounds to zero), which loses its sign. If any lane fails, the half it is in is redone by the 4-lane
 * helper itself, which returns the same result the lifted code gets (the helpers work lane by lane). */
static inline v8 go14_bexp(v8 x) { return _mm256_srli_epi32(_mm256_slli_epi32(x, 1), 24); }
static inline v8 go14_zero(v8 x) { return _mm256_cmpeq_epi32(_mm256_slli_epi32(x, 1), _mm256_setzero_si256()); }
static inline v8 go14_plain(v8 x)
{
    const v8 e = go14_bexp(x);
    return _mm256_or_si256(_mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()),
                                            _mm256_cmpgt_epi32(_mm256_set1_epi32(255), e)), go14_zero(x));
}
static inline v8 go14_okr(v8 r)
{
    const v8 e = go14_bexp(r);
    return _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
}
static inline v8 go14_pos0(v8 r, v8 z) { return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r); }
static inline int go14_all(v8 m) { return _mm256_movemask_ps(_mm256_castsi256_ps(m)) == 0xFF; }

static long s14_fp_slow;   /* 8-lane ops redone by the 4-lane helpers (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) v8 go14_fa_slow(v8 a, v8 b)
{ s14_fp_slow++; return go14_v8(spu_fa(go14_lo(a), go14_lo(b)), spu_fa(go14_hi(a), go14_hi(b))); }

static inline v8 go14_fa(v8 a, v8 b)
{
    const v8 r = go14_f(_mm256_add_ps(go14_p(a), go14_p(b)));
    const v8 z = go14_zero(r);
    const v8 ok = _mm256_and_si256(_mm256_and_si256(go14_plain(a), go14_plain(b)), _mm256_or_si256(go14_okr(r), z));
    if (__builtin_expect(!go14_all(ok), 0)) return go14_fa_slow(a, b);
    return go14_pos0(r, z);
}

/* The two conversions, exact on every lane (no lane rule needed; the self-test compares them with the
 * helpers). The SPU truncates to 24 significant bits, as the native conversion does under the MXCSR's
 * round toward zero:
 *   csflt(i, 155) = (float)i
 *   cuflt(f, 123) = (float)f * 2^-32, unsigned: f = hi * 2^16 + lo with hi, lo < 2^16 converted exactly,
 *                   one rounding in the sum, then the exact scale (2^-32 times a nonzero u32 is normal) */
static inline v8 go14_csflt(v8 i) { return go14_f(_mm256_cvtepi32_ps(i)); }
static inline v8 go14_cuflt(v8 f)
{
    const __m256 hi = _mm256_cvtepi32_ps(_mm256_srli_epi32(f, 16));
    const __m256 lo = _mm256_cvtepi32_ps(_mm256_and_si256(f, _mm256_set1_epi32(0xFFFF)));
    const __m256 s = _mm256_add_ps(_mm256_mul_ps(hi, _mm256_set1_ps(65536.0f)), lo);
    return go14_f(_mm256_mul_ps(s, _mm256_castsi256_ps(_mm256_set1_epi32(0x2F800000))));   /* 2^-32 */
}

/* ---- one range check per iteration (as the other stages' v3) ----
 * A speculative iteration runs fa unchecked and collects, in one running unsigned min and max, the value
 * shifted left by one (sign dropped) of its inputs (the two conversions) and of its result: s >= 0x01000000
 * is exponent >= 1, s <= 0xFDFFFFFF exponent <= 253. Zero lanes (fa: any zero result; zero inputs) are
 * lifted out of the min and become +0. The iteration is kept only if every lane passed: then fa's result
 * is the helper's. Otherwise it is recomputed with the checked fa above before anything is stored. */
typedef struct { v8 mn, mx; } go14_rng;
#define GO14_AI static inline __attribute__((always_inline))
GO14_AI void go14_rng_add(go14_rng* R, v8 lo, v8 hi) { R->mn = _mm256_min_epu32(R->mn, lo); R->mx = _mm256_max_epu32(R->mx, hi); }
GO14_AI void go14_xin(go14_rng* R, v8 x)
{
    const v8 s = _mm256_add_epi32(x, x);
    go14_rng_add(R, _mm256_or_si256(s, _mm256_cmpeq_epi32(s, _mm256_setzero_si256())), s);
}
GO14_AI v8 go14_chk_out(go14_rng* R, v8 r, v8 zz)
{
    const v8 s = _mm256_add_epi32(r, r);
    go14_rng_add(R, _mm256_or_si256(s, zz), s);
    return _mm256_andnot_si256(zz, r);
}
static inline int go14_rng_ok(const go14_rng* R)
{
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(R->mn, _mm256_set1_epi32(0x01000000)), R->mn);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(R->mx, _mm256_set1_epi32((int)0xFDFFFFFFu)), R->mx);
    return go14_all(_mm256_and_si256(lo, hi));
}
GO14_AI v8 go14_xfa(int spec, go14_rng* R, v8 a, v8 b)
{
    if (!spec) return go14_fa(a, b);
    go14_xin(R, a); go14_xin(R, b);
    const v8 r = go14_f(_mm256_add_ps(go14_p(a), go14_p(b)));
    return go14_chk_out(R, r, go14_zero(r));
}

/* =====================================================================================================
 * Packed. What the loop computes (from the lift; checked by gen_edge_o11430_body.py, see EDGE_NATIVE.md):
 * a stream of 24-byte record pairs at r27 (r27 += 24), each record 12 bytes. Per iteration:
 *   - the two field quads of the previous iteration (r9, r5) are decoded, per word:
 *       y = (p & r33) + r31,  out = fa(csflt(rotma(y, r17), 155), cuflt(shl(y, r34), 123)),
 *     word 3 replaced by r35 (selb with r38), and stored at r36 + r18 (r9's) and r29 + r18 (r5's);
 *   - the next pair's 32 bytes (r13:r2) give the next field quads: per record a window
 *     shufb(r13, r2, r25 / r39), shifted right four ways (rotqmbi then rotqmbybi by r24/r24, r26/r26,
 *     r23/r23, r23/r28), word w of the result taken from the w-th way (selb with r32, r37);
 *   - the pair after that is loaded: r13:r2 = the 32 bytes at r27 (shufb of three aligned quads with
 *     r67 = r41 + splat(r27 & 15)); r18 = r19 += 32 until 0.
 * Software pipelined two records deep: iteration i stores pair i, extracts pair i + 1, loads pair i + 2.
 * Here the two records share one vector. The extraction is integer work, done by byte gathers composed at
 * entry from the live controls and shift counts: word w of a window shifted right by 8B + b bits is
 *   (cur >> b) | (prev << (32 - b)),  cur = window bytes 4w - B .. 4w - B + 3,  prev = the four before,
 * bytes before the window zero, with the window's bytes looked up through the shufb control (special
 * selectors included) in the record pair's 32 bytes: two pshufb per gather, the two shifts per lane. The
 * float ops are the lift's: the two conversions (exact) and fa with the lane rule. Each iteration stores
 * before it loads, as in the lift, so the LS sees the same stores in the same order (in place included).
 * The last iteration runs the lifted body from the carried registers (r9 r5 r13 r2 r27 r19 r18, the only
 * ones the loop reads before writing that it also writes), so all 49 registers the loop writes come out
 * exactly. Entry checks, else v1: 2 .. 8192 iterations (r19 a negative multiple of 32), r41 the identity
 * control and r40 = 0x03 bytes (so r13:r2 are the 32 bytes at r27), r32 = fsmbi(0x0F0F), r37 =
 * fsmbi(0x00FF) word masks, and the record stream inside the LS without wrapping.
 * ===================================================================================================== */
typedef struct {
    v8 ca, cb, ka;   /* cur gather: bytes from the pair's first 16, from its second 16, constant bytes */
    v8 da, db, kd;   /* prev gather, the same */
    v8 sb, sl;       /* per word: the bit shift b and 32 - b */
    v8 m33, b31, s34, t17, c35, m38;
} go14_inv;

static void go14_inv_init(const u128* g, go14_inv* I)
{
    static const int BX[4] = { 24, 26, 23, 23 }, BY[4] = { 24, 26, 23, 28 };   /* rotqmbi / rotqmbybi count per word */
    int B[4]; uint32_t b[4];
    for (int w = 0; w < 4; w++) {
        b[w] = (0u - g[BX[w]]._u32[0]) & 7u;                              /* spu_rotqmbi */
        B[w] = (0 - ((int32_t)g[BY[w]]._u32[0] >> 3)) & 0x1F;            /* spu_rotqmbybi; >= 16: all zero */
    }
    uint8_t c[2][3][32];   /* [cur/prev][from first 16 / from second 16 / constant][host byte] */
    for (int h = 0; h < 2; h++) {
        const u128 ctl = g[h ? 39 : 25];
        for (int p = 0; p < 16; p++) {
            const int t = p ^ 3, w = t >> 2, m = t & 3;       /* host byte p = SPU byte t of word w */
            for (int pv = 0; pv < 2; pv++) {
                const int k = 4 * w + m - B[w] - 4 * pv;      /* the window byte (SPU order) */
                uint8_t fa = 0x80, fb = 0x80, kc = 0;
                if (k >= 0) {
                    const uint8_t s = ctl._u8[k ^ 3];           /* control SPU byte k */
                    if ((s & 0xE0) == 0xE0)      kc = 0x80;
                    else if ((s & 0xC0) == 0xC0) kc = 0xFF;
                    else if ((s & 0xC0) == 0x80) kc = 0x00;
                    else if ((s & 0x1F) < 16)    fa = s & 0x0F;
                    else                         fb = s & 0x0F;
                }
                c[pv][0][16 * h + p] = fa; c[pv][1][16 * h + p] = fb; c[pv][2][16 * h + p] = kc;
            }
        }
    }
    v8* dst[2][3] = { { &I->ca, &I->cb, &I->ka }, { &I->da, &I->db, &I->kd } };
    for (int pv = 0; pv < 2; pv++)
        for (int j = 0; j < 3; j++) *dst[pv][j] = _mm256_loadu_si256((const __m256i*)c[pv][j]);
    I->sb = _mm256_setr_epi32((int)b[0], (int)b[1], (int)b[2], (int)b[3], (int)b[0], (int)b[1], (int)b[2], (int)b[3]);
    I->sl = _mm256_sub_epi32(_mm256_set1_epi32(32), I->sb);
    I->m33 = go14_bc(g[33]); I->b31 = go14_bc(g[31]);
    I->s34 = _mm256_and_si256(go14_bc(g[34]), _mm256_set1_epi32(0x3F));                               /* spu_shl */
    I->t17 = _mm256_and_si256(_mm256_sub_epi32(_mm256_setzero_si256(), go14_bc(g[17])), _mm256_set1_epi32(0x3F));   /* spu_rotma */
    I->c35 = go14_bc(g[35]); I->m38 = go14_bc(g[38]);
}

/* the two records' field quads (r9' | r5') from the pair's bytes (xa: bytes 0-15, xb: 16-31, SPU order) */
GO14_AI v8 go14_extract(const go14_inv* I, v8 xa, v8 xb)
{
    const v8 cur = _mm256_or_si256(_mm256_or_si256(_mm256_shuffle_epi8(xa, I->ca), _mm256_shuffle_epi8(xb, I->cb)), I->ka);
    const v8 prv = _mm256_or_si256(_mm256_or_si256(_mm256_shuffle_epi8(xa, I->da), _mm256_shuffle_epi8(xb, I->db)), I->kd);
    return _mm256_or_si256(_mm256_srlv_epi32(cur, I->sb), _mm256_sllv_epi32(prv, I->sl));   /* sllv by 32: 0 */
}

/* the decode of field quads P (r9 | r5) into the two output quads; returns 0 if a speculative run failed */
GO14_AI int go14_iter(const go14_inv* I, v8 P, v8* out, int spec)
{
    go14_rng R; R.mn = _mm256_set1_epi32(-1); R.mx = _mm256_setzero_si256();
    const v8 y = _mm256_add_epi32(_mm256_and_si256(P, I->m33), I->b31);          /* and, a */
    const v8 ci = go14_csflt(_mm256_srav_epi32(y, I->t17));                        /* rotma (>31: sign), csflt */
    const v8 cf = go14_cuflt(_mm256_sllv_epi32(y, I->s34));                        /* shl (>31: 0), cuflt */
    const v8 s = go14_xfa(spec, &R, ci, cf);
    if (spec && !go14_rng_ok(&R)) return 0;
    *out = _mm256_or_si256(_mm256_andnot_si256(I->m38, s), _mm256_and_si256(I->m38, I->c35));   /* selb */
    return 1;
}

static int s14_v2;             /* GOW_EDGE_O11430_V2=1: the packed loop runs the checked fa on every iteration */
static long s14_fb_short;      /* entries with fewer than 2 iterations (or an implausible count): v1 */
static long s14_fb_ctl;        /* entries with other controls or masks (r41 r40 r32 r37): v1 */
static long s14_fb_rng;        /* entries whose record stream is not inside the LS: v1 */
static long s14_pk_iters;      /* iterations run packed */
static long s14_redo;          /* speculative iterations recomputed with the checked fa */
static uint32_t s14_st_force;  /* self-test only: 1 = treat every odd iteration's speculative run as failed */

static inline u128 go14_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}
static inline int go14_is(u128 x, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return x._u32[0] == a && x._u32[1] == b && x._u32[2] == c && x._u32[3] == d;
}

static void go14_pk(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    const uint32_t w19 = g[19]._u32[0];                 /* r19 += 32 until 0 */
    const uint32_t n = (0u - w19) >> 5;
    if ((w19 & 31u) || n < 2 || n > 8192) { s14_fb_short++; go14_v1(ctx); return; }
    if (!go14_is(g[41], 0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu) ||
        !go14_is(g[40], 0x03030303u, 0x03030303u, 0x03030303u, 0x03030303u) ||
        !go14_is(g[32], 0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu) || !go14_is(g[37], 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu)) {
        s14_fb_ctl++; go14_v1(ctx); return;
    }
    const uint32_t a27 = g[27]._u32[0];
    if (a27 >= SPU_LS_SIZE || a27 + 24u * n + 48u > SPU_LS_SIZE) { s14_fb_rng++; go14_v1(ctx); return; }
    go14_inv I;
    go14_inv_init(g, &I);
    const int spec = !s14_v2;
    const uint32_t o36 = g[36]._u32[0], o29 = g[29]._u32[0];
    v8 P = go14_v8(g[9], g[5]);
    v8 xa = go14_raw_bc(g[13]), xb = go14_raw_bc(g[2]);   /* the pair the entry block loaded */
    for (uint32_t i = 0; i + 1 < n; i++) {
        const v8 Pn = go14_extract(&I, xa, xb);
        v8 F;
        if (!spec || (s14_st_force & i) || !go14_iter(&I, P, &F, 1)) {
            if (spec) s14_redo++;
            go14_iter(&I, P, &F, 0);
        }
        const uint32_t w18 = i ? w19 + 32u * i : g[18]._u32[0];
        go14_stlo(ls, o36 + w18, F); go14_sthi(ls, o29 + w18, F);
        const uint32_t a = a27 + 24u * i;                 /* the pair the lift loads at the end of iteration i */
        xa = go14_raw_ls(ls, a); xb = go14_raw_ls(ls, a + 16u);
        P = Pn;
    }
    s14_pk_iters += (long)n - 1;
    /* the last iteration through the lifted body, from the carried registers after iteration n - 2 */
    GOW_O1430_REGS()
    GOW_O1430_LOAD()
    {   /* r13:r2 as the lift loaded them in iteration n - 2 (its own ops) */
        const u128 q27 = go14_addw(g[27], 24u * (n - 2));
        const u128 z = spu_andbi(q27, 15);
        const u128 c67 = spu_a(g[41], spu_shufb(z, z, g[40]));
        const u128 q0 = LD(q27._u32[0] + 0x0), q1 = LD(q27._u32[0] + 0x10), q2 = LD(q27._u32[0] + 0x20);
        r13 = spu_shufb(q0, q1, c67); r2 = spu_shufb(q1, q2, c67);
    }
    r9 = go14_lo(P); r5 = go14_hi(P);
    r27 = go14_addw(g[27], 24u * (n - 1));
    r19 = go14_addw(g[19], 32u * (n - 1)); r18 = r19;
    do {
        GOW_O1430_BODY()
    } while (r19._u32[0] != 0);
    GOW_O1430_STORE()
}

/* ---- self-test at mode init: the extraction (any controls and shift counts) against the lift's
 * shufb/rotqmbi/rotqmbybi/selb, the decode (the conversions and the checked fa, fallback included) against
 * the helpers, then whole loop runs (speculative and checked) against v1, i.e. the lifted statements, on a
 * scratch context (all GPRs and the whole LS compared) ---- */
static uint32_t s14_rng = 0x2545F491u;
static uint32_t go14_rand(void) { s14_rng ^= s14_rng << 13; s14_rng ^= s14_rng >> 17; s14_rng ^= s14_rng << 5; return s14_rng; }
static uint32_t go14_rword(void)
{
    uint32_t x = go14_rand();
    switch (go14_rand() % 23) {
    case 0: x = 0; break;
    case 1: x &= 0x807FFFFFu; break;                     /* exponent 0 */
    case 2: x |= 0x7F800000u; break;                     /* exponent 255 */
    case 3: x = 0x7F000000u | (x & 0x807FFFFFu); break;  /* exponent 254 */
    default: break;
    }
    return x;
}
static int go14_eq(u128 a, u128 b) { return !memcmp(&a, &b, 16); }
static u128 go14_q(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static u128 go14_rq(void) { return go14_q(go14_rand(), go14_rand(), go14_rand(), go14_rand()); }
/* the lift's field extraction of one record (reference) */
static u128 go14_ref_fields(const u128* g, u128 r13, u128 r2, int ctl)
{
    const u128 w = spu_shufb(r13, r2, g[ctl]);
#define GO14_SH(b, B) spu_rotqmbybi(spu_rotqmbi(w, g[b]), g[B])
    return spu_selb(spu_selb(GO14_SH(24, 24), GO14_SH(26, 26), g[32]), spu_selb(GO14_SH(23, 23), GO14_SH(23, 28), g[32]), g[37]);
#undef GO14_SH
}
static u128 go14_ref_conv(const u128* g, u128 p)
{
    const u128 y = spu_a(spu_and(p, g[33]), g[31]);
    return spu_selb(spu_fa(spu_csflt(spu_rotma(y, g[17]), 155), spu_cuflt(spu_shl(y, g[34]), 123)), g[35], g[38]);
}
/* invariants: as cap_3 (mode 0) or random (any controls, counts, masks, constants) */
static void go14_rand_inv(u128* g, int mode)
{
    g[41] = go14_q(0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu);
    g[40] = go14_q(0x03030303u, 0x03030303u, 0x03030303u, 0x03030303u);
    g[32] = go14_q(0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu); g[37] = go14_q(0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu);
    if (mode == 0) {
        g[25] = go14_q(0x00010203u, 0x04050607u, 0x08090A0Bu, 0xC0C0C0C0u);
        g[39] = go14_q(0x0C0D0E0Fu, 0x10111213u, 0x14151617u, 0xC0C0C0C0u);
        g[24] = go14_q(0xFFFFFFF0u, 0xFFFFFFE0u, 0xFFFFFFD0u, 0xFFFFFFB7u);
        g[26] = go14_q(0xFFFFFFE0u, 0xFFFFFFD0u, 0xFFFFFFB7u, 0);
        g[23] = go14_q(0xFFFFFFD0u, 0xFFFFFFB7u, 0, 0); g[28] = go14_q(0xFFFFFFB7u, 0, 0, 0);
        g[33] = go14_q(0xFFFFu, 0xFFFFu, 0xFFFFu, 0);
        const uint32_t k = 16u + go14_rand() % 4u;
        g[34] = go14_q(k, k, k - (go14_rand() & 1u), 0x20u);
        g[17] = go14_q(0u - k, 0u - k, 0u - (k - 1u), 0);
        g[31] = go14_q(0u - (go14_rand() & 0xFFFFu), 0u - (go14_rand() & 0xFFFu), go14_rand() & 0x7FFFu, 0);
        g[35] = go14_q(0, 0, 0, 0x3F800000u); g[38] = go14_q(0, 0, 0, 0xFFFFFFFFu);
        return;
    }
    for (int i = 0; i < 4; i++) {
        g[25]._u32[i] = (mode & 1) ? go14_rand() : (go14_rand() & 0x1F1F1F1Fu);
        g[39]._u32[i] = (mode & 1) ? go14_rand() : (go14_rand() & 0x1F1F1F1Fu);
    }
    const int* regs = (const int[]){ 23, 24, 26, 28 };
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) g[regs[j]]._u32[i] = (mode & 2) ? go14_rand() : 0u - (go14_rand() % 140u);
    for (int i = 0; i < 4; i++) {
        g[33]._u32[i] = (mode & 4) ? go14_rand() : 0xFFFFu >> (go14_rand() % 8u);
        g[31]._u32[i] = (mode & 4) ? go14_rand() : 0u - (go14_rand() & 0xFFFFu);
        g[34]._u32[i] = (mode & 2) ? go14_rand() : go14_rand() % 40u;
        g[17]._u32[i] = (mode & 2) ? go14_rand() : 0u - (go14_rand() % 40u);
        g[35]._u32[i] = go14_rword(); g[38]._u32[i] = go14_rand();
    }
}
static void go14_selftest(void)
{
    long bad_x = 0, bad_c = 0, n_c = 0, kept_c = 0, bad_fa = 0, bad_run = 0, runs = 0, kept = 0;
    long kept_sfa = 0, rej_sfa = 0, bad_sfa = 0;
    const long slow0 = s14_fp_slow, redo0 = s14_redo, pk0 = s14_pk_iters;
    u128 g[128];
    memset(g, 0, sizeof g);
    uint8_t* ls = (uint8_t*)calloc(1, SPU_LS_SIZE);
    for (int it = 0; ls && it < 20000; it++) {
        go14_rand_inv(g, it % 8);
        /* extraction, from registers and from LS through the lift's r67 path */
        const u128 r13 = go14_rq(), r2 = go14_rq();
        go14_inv I; go14_inv_init(g, &I);
        const v8 x = go14_extract(&I, go14_raw_bc(r13), go14_raw_bc(r2));
        bad_x += !go14_eq(go14_ref_fields(g, r13, r2, 25), go14_lo(x)) + !go14_eq(go14_ref_fields(g, r13, r2, 39), go14_hi(x));
        const uint32_t a = 0x1000u + (go14_rand() % 0x400u);
        for (int k = 0; k < 4; k++) go14_st(ls, (a & ~15u) + 16u * k, go14_rq());
        const u128 q27 = go14_q(a, go14_rand(), go14_rand(), go14_rand()), z = spu_andbi(q27, 15);
        const u128 c67 = spu_a(g[41], spu_shufb(z, z, g[40]));
        const u128 l13 = spu_shufb(go14_ld(ls, a), go14_ld(ls, a + 16), c67), l2 = spu_shufb(go14_ld(ls, a + 16), go14_ld(ls, a + 32), c67);
        const v8 xl = go14_extract(&I, go14_raw_ls(ls, a), go14_raw_ls(ls, a + 16));
        bad_x += !go14_eq(go14_ref_fields(g, l13, l2, 25), go14_lo(xl)) + !go14_eq(go14_ref_fields(g, l13, l2, 39), go14_hi(xl));
        /* decode, checked and speculative (when kept) */
        u128 p[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) p[h]._u32[i] = (it & 1) ? go14_rand() : go14_rand() & 0x3FFFFu;
        for (int spec = 1; spec >= 0; spec--) {
            v8 o;
            if (!go14_iter(&I, go14_v8(p[0], p[1]), &o, spec)) continue;
            kept_c += spec; n_c += 2;
            bad_c += !go14_eq(go14_ref_conv(g, p[0]), go14_lo(o)) + !go14_eq(go14_ref_conv(g, p[1]), go14_hi(o));
        }
        /* the checked fa alone, on operands with zeros, exponent 0/255/254 and cancelling sums */
        u128 fa_[2], fb_[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                fa_[h]._u32[i] = go14_rword(); fb_[h]._u32[i] = go14_rword();
                if ((it & 7) == 5) fb_[h]._u32[i] = fa_[h]._u32[i] ^ 0x80000000u;
            }
        const v8 xf = go14_fa(go14_v8(fa_[0], fa_[1]), go14_v8(fb_[0], fb_[1]));
        bad_fa += !go14_eq(spu_fa(fa_[0], fb_[0]), go14_lo(xf)) + !go14_eq(spu_fa(fa_[1], fb_[1]), go14_hi(xf));
        /* the speculative fa and its range check on the same operands: when kept, it must be the helper's
         * (the decode's own inputs, two conversions, never fail the check, so this is what exercises it) */
        go14_rng R; R.mn = _mm256_set1_epi32(-1); R.mx = _mm256_setzero_si256();
        const v8 xs = go14_xfa(1, &R, go14_v8(fa_[0], fa_[1]), go14_v8(fb_[0], fb_[1]));
        if (go14_rng_ok(&R)) {
            kept_sfa++;
            bad_sfa += !go14_eq(spu_fa(fa_[0], fb_[0]), go14_lo(xs)) + !go14_eq(spu_fa(fa_[1], fb_[1]), go14_hi(xs));
        } else rej_sfa++;
    }
    const long slow_unit = s14_fp_slow - slow0;
    /* whole loop runs on a scratch context: v1 (the lifted statements) against packed v3 and v2 */
    spu_context* ctx = (spu_context*)calloc(1, sizeof(spu_context));
    uint8_t* pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    uint8_t* ref_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    u128* pre_g = (u128*)malloc(sizeof(ctx->gpr));
    u128* ref_g = (u128*)malloc(sizeof(ctx->gpr));
    const int v2_0 = s14_v2;
    for (int it = 0; ctx && pre_ls && ref_ls && pre_g && ref_g && it < 600; it++) {
        ctx->ls = ctx->ls_store;
        u128* gg = ctx->gpr;
        for (int r = 0; r < 128; r++) gg[r] = go14_rq();
        go14_rand_inv(gg, it % 8);
        const uint32_t n = 2u + go14_rand() % 60u;
        const uint32_t a27 = 0x10000u + go14_rand() % 0x200u;
        for (uint32_t k = 0; k < (24u * n + 64u) / 16u + 1u; k++) go14_st(ctx->ls, ((a27 - 48u) & ~15u) + 16u * k, go14_rq());
        /* output: apart from the stream, or over the records not yet loaded (in place) */
        const uint32_t o36 = (it & 4) ? a27 + 24u + 32u * n + 16u * (go14_rand() % 4u) : 0x29000u + 32u * n + 16u * (go14_rand() % 4u);
        gg[36] = go14_q(o36, go14_rand(), go14_rand(), go14_rand());
        gg[29] = (it & 8) ? go14_addw(gg[36], 16u) : go14_q(0x2C000u + 32u * n, 0, 0, 0);
        gg[19] = go14_q(0u - 32u * n, go14_rand(), go14_rand(), go14_rand());
        gg[18] = (it & 16) ? gg[19] : go14_q(0u - 32u * n + 16u * (go14_rand() % 3u), 0, 0, 0);
        gg[27] = go14_q(a27, 0x30u, 0x30u, go14_rand());
        memcpy(pre_ls, ctx->ls, SPU_LS_SIZE); memcpy(pre_g, gg, sizeof(ctx->gpr));
        go14_v1(ctx);
        memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); memcpy(ref_g, gg, sizeof(ctx->gpr));
        for (int v2 = 0; v2 < 2; v2++) {
            memcpy(ctx->ls, pre_ls, SPU_LS_SIZE); memcpy(gg, pre_g, sizeof(ctx->gpr));
            s14_v2 = v2;
            s14_st_force = !v2 && (it & 32) ? 1u : 0u;   /* exercise the recompute path */
            const long r0 = s14_redo;
            go14_pk(ctx);
            s14_st_force = 0;
            if (!v2) kept += (long)n - 1 - (s14_redo - r0);
            runs++;
            bad_run += memcmp(ctx->ls, ref_ls, SPU_LS_SIZE) != 0 || memcmp(gg, ref_g, sizeof(ctx->gpr)) != 0;
        }
    }
    s14_v2 = v2_0;
    free(pre_ls); free(ref_ls); free(pre_g); free(ref_g); free(ctx); free(ls);
    const long redo = s14_redo - redo0;
    s14_fp_slow = slow0; s14_redo = redo0; s14_pk_iters = pk0;
    fprintf(stderr, "[edge-o11430] self-test: extraction %ld of 80000 quads differ; decode %ld of %ld quads differ "
                    "(%ld 8-lane decodes kept speculative); checked fa %ld of 40000 quads differ (%ld 8-lane ops took "
                    "the 4-lane fallback); speculative fa on the same operands %ld kept (%ld quads differ), %ld "
                    "rejected by the range check; whole loop runs against v1 %ld of %ld differ (%ld iterations kept "
                    "speculative, %ld recomputed)\n",
            bad_x, bad_c, n_c, kept_c, bad_fa, slow_unit, kept_sfa, bad_sfa, rej_sfa, bad_run, runs, kept, redo);
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s14_mode = -1;

int gow_edge_o11430_mode(void)
{
    if (s14_mode < 0) {
        const char* e = getenv("GOW_EDGE_O11430");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        const char* v = getenv("GOW_EDGE_O11430_V2");
        s14_v2 = v && atoi(v) == 1;
        s14_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o11430] GOW_EDGE_O11430=0 (lifted loop)\n"); }
        else {
            fprintf(stderr, "[edge-o11430] GOW_EDGE_O11430=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : s14_v2 ? "packed v2" : "packed v3");
            go14_selftest();
        }
    }
    return s14_mode;
}

static int s14_fault = -1;
static long s14_fault_at, s14_runs;
static void go14_fault(spu_context* ctx)
{
    if (s14_fault < 0) {
        const char* f = getenv("GOW_EDGE_O11430_FAULT");
        s14_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s14_fault_at = k ? atol(k + 1) : 0;
    }
    if (s14_fault <= 0 || s14_runs++ != s14_fault_at) return;
    const u128* r = ctx->gpr;
    switch (s14_fault) {
    case 1: ctx->ls[((r[36]._u32[0] - 32u) & (SPU_LS_MASK & ~0xFu)) + 3] ^= 1; break;   /* ST(r36 + r18) of the last iteration, r18 = -32 */
    case 3: ctx->ls[((r[36]._u32[0] - 32u) & (SPU_LS_MASK & ~0xFu)) + 1] ^= 1; break;   /* the same quad, x's mantissa bit 16 */
    case 2: ctx->gpr[9]._u32[0] ^= 1; break;
    default: break;
    }
    fprintf(stderr, "[edge-o11430] fault %d injected (native run %ld)\n", s14_fault, s14_fault_at);
    s14_fault = 0;
}

static void go14_run(spu_context* ctx)
{
    if (s14_mode == 2 || s14_mode == 4) go14_v1(ctx);
    else                                go14_pk(ctx);
    if (__builtin_expect(s14_fault != 0, 0)) go14_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then jumps to its exit. */
void gow_edge_o11430_11460(spu_context* ctx) { go14_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} go14_check_state;
static _Thread_local go14_check_state* t14_chk;

static long s14_calls, s14_bad, s14_iters;
static uint64_t s14_cyc_nat, s14_cyc_lift;
static int s14_atexit;

static void go14_check_report(void)
{
    fprintf(stderr, "[edge-o11430-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); tsc: native "
                    "%.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s14_calls, s14_bad, s14_iters, s14_cyc_nat / 1e6, s14_cyc_lift / 1e6,
            s14_iters ? (double)s14_cyc_nat / (double)s14_iters : 0.0,
            s14_iters ? (double)s14_cyc_lift / (double)s14_iters : 0.0);
    fprintf(stderr, "[edge-o11430-check] packed %ld iterations; entries left to v1: %ld short, %ld other controls, %ld "
                    "outside the LS; speculative iterations recomputed: %ld; 8-lane ops redone by the 4-lane helpers: "
                    "%ld\n", s14_pk_iters, s14_fb_short, s14_fb_ctl, s14_fb_rng, s14_redo, s14_fp_slow);
}

void gow_edge_o11430_check_begin(spu_context* ctx)
{
    go14_check_state* c = t14_chk;
    if (!c) {
        c = (go14_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t14_chk = c;
    }
    if (!s14_atexit) { s14_atexit = 1; atexit(go14_check_report); }
    s14_iters += (long)((0u - ctx->gpr[19]._u32[0]) >> 5);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    go14_run(ctx);
    s14_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_o11430_check_end(spu_context* ctx)
{
    go14_check_state* c = t14_chk;
    if (!c || !c->pending) return;
    s14_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s14_calls++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-o11430-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-o11430-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s14_bad++;
        fprintf(stderr, "[edge-o11430-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) go14_check_report();   /* a game run ends by kill: atexit never runs */
}

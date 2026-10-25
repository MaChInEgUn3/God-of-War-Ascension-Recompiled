/* gow_edge_native_f538.c -- native replacement for the loop of the EDGE job module's (d935, lift
 * spu/jobmod_d93503647dd8_e6d98) function F538: the loop at loc_0000F620 (exit: the fall-through after its
 * back edge, F538's return). The d935 lift is built through gen_edge_native_lift.py, which puts a switch
 * before the loop; GOW_EDGE_F538 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native.c (-O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1,
 * SPU_FP_CHECK_HOOK=0): the float helpers are the same inline helpers the lift uses. Runs inside
 * spu_run_with_halt, i.e. with MXCSR rounding toward zero.
 *
 * Clean-room: written from our own lift of the module and our own captures.
 *
 * GOW_EDGE_F538 (read once):
 *   0/unset  lifted loop
 *   1        native, packed (two quads per vector, one range check per iteration: "v3"); with
 *            GOW_EDGE_F538_V2=1 every iteration runs the checked ops instead ("v2": the per-lane rule and
 *            the 4-lane fallback on every float op)
 *   2        native v1 (the loop statement for statement, registers in locals)
 *   3        check the packed version (v3, or v2 with GOW_EDGE_F538_V2=1): run it on a copy of the state,
 *            then the lifted loop, and compare all GPRs and the whole LS at the loop exit
 *   4        check v1, the same way
 * GOW_EDGE_F538_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 15 of the first quad the last iteration stores, 2 bit 0 of r20 after the loop.
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
static inline __m128i g53_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 g53_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = g53_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void g53_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g53_bswap32x4(x));
}
#define LD(a)    g53_ld(ls, (a))
#define ST(a, v) g53_st(ls, (a), (v))

/* the lifted loop: GOW_F53_*. Game code, so it is not in the repo: gen_edge_f538_body.py writes it from
 * the d935 lift at build time (and checks its shape, its inputs and its dataflow). */
#include "gow_edge_f538_body.h"

/* =====================================================================================================
 * v1: the loop statement for statement, registers in C locals, direct LS access. The control flow is the
 * lift's: F620: BODY; if (r37 != 0) goto F620;
 * ===================================================================================================== */
static void g53_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_F53_REGS()
    GOW_F53_LOAD()
    do {
        GOW_F53_BODY()
    } while (r37._u32[0] != 0);
    GOW_F53_STORE()
}

/* ---- 8-lane building blocks (here: two quads per vector, quad j in the low half, j+1 in the high) ---- */
typedef __m256i v8;

static inline v8 g53_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 g53_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(a); }
static inline u128 g53_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 g53_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline v8 g53_ld2(const uint8_t* ls, uint32_t lo, uint32_t hi)
{
    const __m128i a = _mm_loadu_si128((const __m128i*)(ls + (lo & (SPU_LS_MASK & ~0xFu))));
    const __m128i b = _mm_loadu_si128((const __m128i*)(ls + (hi & (SPU_LS_MASK & ~0xFu))));
    return _mm256_shuffle_epi8(_mm256_set_m128i(b, a),
                               _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline void g53_stlo(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g53_bswap32x4(_mm256_castsi256_si128(v))); }
static inline void g53_sthi(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g53_bswap32x4(_mm256_extracti128_si256(v, 1))); }
static inline v8 g53_f(__m256 x) { return _mm256_castps_si256(x); }
static inline __m256 g53_p(v8 x) { return _mm256_castsi256_ps(x); }
/* rotqbyi by 4, 8, 12 bytes: word i <- word i+1, i+2, i+3 (mod 4), in each quad */
#define G53_ROT4(x)  _mm256_shuffle_epi32((x), _MM_SHUFFLE(0, 3, 2, 1))
#define G53_ROT8(x)  _mm256_shuffle_epi32((x), _MM_SHUFFLE(1, 0, 3, 2))
#define G53_ROT12(x) _mm256_shuffle_epi32((x), _MM_SHUFFLE(2, 1, 0, 3))

/* fm / fma eight lanes at a time, with the per-lane rule of the SPU_FP_NATIVE helpers (spu_helpers.h,
 * spu__xn_*): the native op rounded toward zero is kept when every input is an ordinary single or +-0 and
 * the result is an ordinary single below the top binade, or a zero that is exact (a zero factor, and for
 * fma a zero addend), which loses its sign. If any lane fails, the half it is in is redone by the 4-lane
 * helper itself, which returns the same result the lifted code gets (the helpers work lane by lane). */
static inline v8 g53_bexp(v8 x) { return _mm256_srli_epi32(_mm256_slli_epi32(x, 1), 24); }
static inline v8 g53_zero(v8 x) { return _mm256_cmpeq_epi32(_mm256_slli_epi32(x, 1), _mm256_setzero_si256()); }
static inline v8 g53_plain(v8 x)
{
    const v8 e = g53_bexp(x);
    return _mm256_or_si256(_mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()),
                                            _mm256_cmpgt_epi32(_mm256_set1_epi32(255), e)), g53_zero(x));
}
static inline v8 g53_okr(v8 r)
{
    const v8 e = g53_bexp(r);
    return _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
}
static inline v8 g53_pos0(v8 r, v8 z) { return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r); }
static inline int g53_all(v8 m) { return _mm256_movemask_ps(_mm256_castsi256_ps(m)) == 0xFF; }

static long s53_fp_slow;   /* 8-lane ops redone by the 4-lane helpers (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) v8 g53_fm_slow(v8 a, v8 b)
{ s53_fp_slow++; return g53_v8(spu_fm(g53_lo(a), g53_lo(b)), spu_fm(g53_hi(a), g53_hi(b))); }
static __attribute__((noinline, cold)) v8 g53_fma_slow(v8 a, v8 b, v8 c)
{ s53_fp_slow++; return g53_v8(spu_fma(g53_lo(a), g53_lo(b), g53_lo(c)), spu_fma(g53_hi(a), g53_hi(b), g53_hi(c))); }

static inline v8 g53_fm(v8 a, v8 b)
{
    const v8 r = g53_f(_mm256_mul_ps(g53_p(a), g53_p(b)));
    const v8 z = _mm256_or_si256(g53_zero(a), g53_zero(b));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g53_plain(a), g53_plain(b)), _mm256_or_si256(g53_okr(r), z));
    if (__builtin_expect(!g53_all(ok), 0)) return g53_fm_slow(a, b);
    return g53_pos0(r, z);
}
static inline v8 g53_fma(v8 a, v8 b, v8 c)   /* a*b + c */
{
    const v8 r = g53_f(_mm256_fmadd_ps(g53_p(a), g53_p(b), g53_p(c)));
    const v8 z = _mm256_and_si256(_mm256_or_si256(g53_zero(a), g53_zero(b)), g53_zero(c));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(_mm256_and_si256(g53_plain(a), g53_plain(b)), g53_plain(c)),
                                   _mm256_or_si256(g53_okr(r), z));
    if (__builtin_expect(!g53_all(ok), 0)) return g53_fma_slow(a, b, c);
    return g53_pos0(r, z);
}

/* ---- one range check per iteration (as the other stages' v3) ----
 * A speculative iteration runs the native ops unchecked and collects, in one running unsigned min and
 * max, the value shifted left by one (sign dropped) of every float input it takes from outside (LS, the
 * carried registers) and of every op result: s >= 0x01000000 is exponent >= 1, s <= 0xFDFFFFFF exponent
 * <= 253. Allowed zero lanes (a zero factor, and for fma a zero addend) are lifted out of the min and
 * become +0. The iteration is kept only if every lane passed: then every value it computed was an
 * ordinary single or an allowed zero, so each op's result is the helper's, and the inputs of ops that are
 * results of earlier ops needed no check of their own. Otherwise it is recomputed with the checked ops
 * above before anything is stored. The loop-invariant matrix columns are checked once per entry. */
typedef struct { v8 mn, mx; } g53_rng;
#define G53_AI static inline __attribute__((always_inline))
G53_AI void g53_rng_add(g53_rng* R, v8 lo, v8 hi) { R->mn = _mm256_min_epu32(R->mn, lo); R->mx = _mm256_max_epu32(R->mx, hi); }
G53_AI v8 g53_xin(int spec, g53_rng* R, v8 x)
{
    if (!spec) return _mm256_setzero_si256();
    const v8 s = _mm256_add_epi32(x, x);
    const v8 z = _mm256_cmpeq_epi32(s, _mm256_setzero_si256());
    g53_rng_add(R, _mm256_or_si256(s, z), s);
    return z;
}
G53_AI v8 g53_chk_out(g53_rng* R, v8 r, v8 zz)
{
    const v8 s = _mm256_add_epi32(r, r);
    g53_rng_add(R, _mm256_or_si256(s, zz), s);
    return _mm256_andnot_si256(zz, r);
}
static inline int g53_rng_ok(const g53_rng* R)
{
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(R->mn, _mm256_set1_epi32(0x01000000)), R->mn);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(R->mx, _mm256_set1_epi32((int)0xFDFFFFFFu)), R->mx);
    return g53_all(_mm256_and_si256(lo, hi));
}
/* a float op: speculative (the raw native op, its result tracked; *z = its allowed zero lanes) or checked
 * (spec == 0: the per-lane rule with the 4-lane fallback; z unused) */
G53_AI v8 g53_xfm(int spec, g53_rng* R, v8 a, v8 za, v8 b, v8 zb, v8* z)
{
    if (!spec) { *z = _mm256_setzero_si256(); return g53_fm(a, b); }
    *z = _mm256_or_si256(za, zb);
    return g53_chk_out(R, g53_f(_mm256_mul_ps(g53_p(a), g53_p(b))), *z);
}
G53_AI v8 g53_xfma(int spec, g53_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)   /* a*b + c */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g53_fma(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g53_chk_out(R, g53_f(_mm256_fmadd_ps(g53_p(a), g53_p(b), g53_p(c))), *z);
}

/* =====================================================================================================
 * Packed. What the loop computes (from the lift; see EDGE_NATIVE.md): per iteration 8 input quads v_k at
 * r38 + 16k (r38 += 128), each transformed by the four loop-invariant columns M0..M3 = r35 r36 r33 r34,
 * op for op as the lift computes it (rot_n = rotqbyi by n bytes, i.e. lane l takes v[l + n/4 mod 4]):
 *   p1 = fm(v, M0)   p2 = fma(rot_4 v, M1, p1)   p3 = fma(rot_8 v, M2, p2)   out = fma(rot_12 v, M3, p3)
 * Software pipelined: quads 0 and 1 are finished in the iteration that loads them (r20 r21), quads 2-7
 * carry p3 (r9 r43 r41 r50 r49 r48) and rot_12 v (r53 r52 r51 r57 r56 r55) into the next iteration, which
 * does their last fma. Each iteration stores the previous one's eight results at r58 r54 r47 r46 r45 r44
 * r42 r40 + r39 (r39 += r32; r32 = 128 after the first iteration, so the first iteration stores the
 * prologue's values and the second rewrites them). The loop runs r37 times (one more than the groups, the
 * last iteration only drains the pipeline and loads one group past the input).
 * Here two quads share one vector (0|1, 2|3, 4|5, 6|7) and all lanes reach memory. Every value is the same
 * op on the same operands as in the lift (the helpers work lane by lane). Per iteration all loads come
 * before the stores and the stores keep the lift's order, as in the lift, so the LS sees the same accesses.
 * The last iteration runs the lifted body (v1's macros) from the carried registers, so every register the
 * loop writes comes out exactly (the loop reads no other register before writing it, and the dataflow
 * above is checked by gen_edge_f538_body.py).
 * ===================================================================================================== */
typedef struct { v8 F01, P23, P45, P67, R23, R45, R67; } g53_carry;

/* one iteration from the carried state c and the group at a38; *o = the new carried state, s[4] = what
 * this iteration stores (F01 F23 F45 F67); returns 0 if a speculative run failed */
G53_AI int g53_iter(const uint8_t* ls, uint32_t a38, const v8* M, const v8* zM, const g53_carry* c,
                    g53_carry* o, v8* s, int spec)
{
    g53_rng R; R.mn = _mm256_set1_epi32(-1); R.mx = _mm256_setzero_si256();
    const v8 zP23 = g53_xin(spec, &R, c->P23), zP45 = g53_xin(spec, &R, c->P45), zP67 = g53_xin(spec, &R, c->P67);
    const v8 zR23 = g53_xin(spec, &R, c->R23), zR45 = g53_xin(spec, &R, c->R45), zR67 = g53_xin(spec, &R, c->R67);
    v8 z0, z1, z2, z3;
    const v8 F23 = g53_xfma(spec, &R, c->R23, zR23, M[3], zM[3], c->P23, zP23, &z0);
    const v8 F45 = g53_xfma(spec, &R, c->R45, zR45, M[3], zM[3], c->P45, zP45, &z1);
    const v8 F67 = g53_xfma(spec, &R, c->R67, zR67, M[3], zM[3], c->P67, zP67, &z2);
    const v8 V[4] = { g53_ld2(ls, a38, a38 + 16), g53_ld2(ls, a38 + 32, a38 + 48),
                      g53_ld2(ls, a38 + 64, a38 + 80), g53_ld2(ls, a38 + 96, a38 + 112) };
    v8 P[4];
    for (int k = 0; k < 4; k++) {
        const v8 v = V[k], zv = g53_xin(spec, &R, v);
        const v8 p1 = g53_xfm(spec, &R, v, zv, M[0], zM[0], &z0);
        const v8 p2 = g53_xfma(spec, &R, G53_ROT4(v), G53_ROT4(zv), M[1], zM[1], p1, z0, &z1);
        P[k] = g53_xfma(spec, &R, G53_ROT8(v), G53_ROT8(zv), M[2], zM[2], p2, z1, &z2);
        if (k == 0) {
            const v8 p3 = P[0];
            P[0] = g53_xfma(spec, &R, G53_ROT12(v), G53_ROT12(zv), M[3], zM[3], p3, z2, &z3);
        }
    }
    if (spec && !g53_rng_ok(&R)) return 0;
    s[0] = c->F01; s[1] = F23; s[2] = F45; s[3] = F67;
    o->F01 = P[0]; o->P23 = P[1]; o->P45 = P[2]; o->P67 = P[3];
    o->R23 = G53_ROT12(V[1]); o->R45 = G53_ROT12(V[2]); o->R67 = G53_ROT12(V[3]);
    return 1;
}

static int s53_v2;             /* GOW_EDGE_F538_V2=1: the packed loop runs the checked ops on every iteration */
static long s53_fb_short;      /* entries with fewer than 2 iterations (or an implausible count): v1 */
static long s53_fb_inv;        /* entries whose matrix columns are not all plain: checked ops throughout */
static long s53_pk_iters;      /* iterations run packed */
static long s53_redo;          /* speculative iterations recomputed with the checked ops */

static inline u128 g53_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}

static void g53_pk(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    const uint32_t n = g[37]._u32[0];          /* do ... r37 -= 1 while (r37 != 0) */
    if (n < 2 || n > SPU_LS_SIZE / 128) { s53_fb_short++; g53_v1(ctx); return; }
    const v8 M[4] = { g53_bc(g[35]), g53_bc(g[36]), g53_bc(g[33]), g53_bc(g[34]) };
    v8 zM[4];
    int spec = !s53_v2;
    for (int i = 0; i < 4; i++) {
        zM[i] = g53_zero(M[i]);
        if (!g53_all(g53_plain(M[i])) || !g53_all(_mm256_or_si256(g53_okr(M[i]), zM[i]))) {
            if (spec) s53_fb_inv++;
            spec = 0;
        }
    }
    g53_carry c = { g53_v8(g[20], g[21]), g53_v8(g[9], g[43]), g53_v8(g[41], g[50]), g53_v8(g[49], g[48]),
                    g53_v8(g[53], g[52]), g53_v8(g[51], g[57]), g53_v8(g[56], g[55]) };
    const uint32_t o58 = g[58]._u32[0], o54 = g[54]._u32[0], o47 = g[47]._u32[0], o46 = g[46]._u32[0];
    const uint32_t o45 = g[45]._u32[0], o44 = g[44]._u32[0], o42 = g[42]._u32[0], o40 = g[40]._u32[0];
    u128 q39 = g[39], q32 = g[32];
    uint32_t a38 = g[38]._u32[0];
    for (uint32_t i = 0; i + 1 < n; i++) {
        g53_carry o; v8 s[4];
        if (!spec || !g53_iter(ls, a38, M, zM, &c, &o, s, 1)) {
            if (spec) s53_redo++;
            g53_iter(ls, a38, M, zM, &c, &o, s, 0);
        }
        const uint32_t b = q39._u32[0];
        g53_stlo(ls, o58 + b, s[0]); g53_sthi(ls, o54 + b, s[0]);
        g53_stlo(ls, o47 + b, s[1]); g53_sthi(ls, o46 + b, s[1]);
        g53_stlo(ls, o45 + b, s[2]); g53_sthi(ls, o44 + b, s[2]);
        g53_stlo(ls, o42 + b, s[3]); g53_sthi(ls, o40 + b, s[3]);
        for (int w = 0; w < 4; w++) { q39._u32[w] += q32._u32[w]; q32._u32[w] = 128u; }   /* r28 = a(r39, r32); r39 = r28; r32 = il(128) */
        a38 += 128u;
        c = o;
    }
    s53_pk_iters += (long)n - 1;
    /* the last iteration through the lifted body, from the carried registers after iteration n - 2 */
    GOW_F53_REGS()
    GOW_F53_LOAD()
    r20 = g53_lo(c.F01); r21 = g53_hi(c.F01);
    r9 = g53_lo(c.P23); r43 = g53_hi(c.P23); r41 = g53_lo(c.P45); r50 = g53_hi(c.P45);
    r49 = g53_lo(c.P67); r48 = g53_hi(c.P67);
    r53 = g53_lo(c.R23); r52 = g53_hi(c.R23); r51 = g53_lo(c.R45); r57 = g53_hi(c.R45);
    r56 = g53_lo(c.R67); r55 = g53_hi(c.R67);
    r38 = g53_addw(r38, 128u * (n - 1)); r37 = g53_addw(r37, 0u - (n - 1));
    r39 = q39; r32 = q32;
    do {
        GOW_F53_BODY()
    } while (r37._u32[0] != 0);
    GOW_F53_STORE()
}

/* ---- self-test at mode init: the checked 8-lane fm/fma (fallback included) and the rotations against the
 * 4-lane helpers, then whole iterations (speculative when kept, and checked) against the helpers applied
 * quad by quad as the lift does, on operands with zeros, exponent 0/255/254 and cancelling sums mixed in ---- */
static uint32_t s53_rng = 0x2545F491u;
static uint32_t g53_rand(void) { s53_rng ^= s53_rng << 13; s53_rng ^= s53_rng >> 17; s53_rng ^= s53_rng << 5; return s53_rng; }
static uint32_t g53_rword(int it)
{
    uint32_t x = g53_rand();
    switch (g53_rand() % 23) {
    case 0: x = 0; break;
    case 1: x &= 0x807FFFFFu; break;                     /* exponent 0 */
    case 2: x |= 0x7F800000u; break;                     /* exponent 255 */
    case 3: x = 0x7F000000u | (x & 0x807FFFFFu); break;  /* exponent 254 */
    default: if (it & 1) x = (x & 0x83FFFFFFu) | 0x3C000000u; break;   /* moderate exponents */
    }
    return x;
}
static uint32_t g53_mword(void) { return (g53_rand() & 0x83FFFFFFu) | 0x3C000000u; }   /* ordinary, |x| ~ 2^-7..2^8 */
static int g53_eq(u128 a, u128 b) { return !memcmp(&a, &b, 16); }
static void g53_selftest(void)
{
    long bad = 0, bad_it = 0, kept = 0, iters = 0;
    const long slow0 = s53_fp_slow;
    for (int it = 0; it < 20000; it++) {
        u128 a[2], b[2], c[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                a[h]._u32[i] = g53_rword(it); b[h]._u32[i] = g53_rword(it); c[h]._u32[i] = g53_rword(it);
                if ((it & 15) == 6) c[h]._u32[i] = a[h]._u32[i] ^ 0x80000000u;   /* cancelling with a*1 */
                if ((it & 15) == 6) b[h]._u32[i] = 0x3F800000u;
            }
        const v8 A = g53_v8(a[0], a[1]), B = g53_v8(b[0], b[1]), C = g53_v8(c[0], c[1]);
        const v8 xm = g53_fm(A, B), xf = g53_fma(A, B, C);
        const v8 r4 = G53_ROT4(A), r8 = G53_ROT8(A), r12 = G53_ROT12(A);
        for (int h = 0; h < 2; h++) {
            bad += !g53_eq(spu_fm(a[h], b[h]), h ? g53_hi(xm) : g53_lo(xm));
            bad += !g53_eq(spu_fma(a[h], b[h], c[h]), h ? g53_hi(xf) : g53_lo(xf));
            bad += !g53_eq(spu_rotqbyi(a[h], 4), h ? g53_hi(r4) : g53_lo(r4));
            bad += !g53_eq(spu_rotqbyi(a[h], 8), h ? g53_hi(r8) : g53_lo(r8));
            bad += !g53_eq(spu_rotqbyi(a[h], 12), h ? g53_hi(r12) : g53_lo(r12));
        }
    }
    /* whole iterations: a group of 8 quads in a scratch LS, carried state and columns random */
    uint8_t* ls = (uint8_t*)calloc(1, SPU_LS_SIZE);
    for (int it = 0; ls && it < 4000; it++) {
        u128 m[4], cr[14], v[8];
        for (int k = 0; k < 4; k++) for (int i = 0; i < 4; i++) m[k]._u32[i] = (it & 3) ? g53_mword() : g53_rword(it);
        for (int k = 0; k < 14; k++) for (int i = 0; i < 4; i++) cr[k]._u32[i] = (it & 2) ? g53_mword() : g53_rword(it);
        for (int k = 0; k < 8; k++) {
            for (int i = 0; i < 4; i++) v[k]._u32[i] = (it & 1) ? g53_mword() : g53_rword(it);
            g53_st(ls, 0x1000 + 16u * k, v[k]);
        }
        const v8 M[4] = { g53_bc(m[0]), g53_bc(m[1]), g53_bc(m[2]), g53_bc(m[3]) };
        const v8 zM[4] = { g53_zero(M[0]), g53_zero(M[1]), g53_zero(M[2]), g53_zero(M[3]) };
        int mplain = 1;
        for (int k = 0; k < 4; k++) mplain &= g53_all(g53_plain(M[k])) && g53_all(_mm256_or_si256(g53_okr(M[k]), zM[k]));
        const g53_carry ci = { g53_v8(cr[0], cr[1]), g53_v8(cr[2], cr[3]), g53_v8(cr[4], cr[5]), g53_v8(cr[6], cr[7]),
                               g53_v8(cr[8], cr[9]), g53_v8(cr[10], cr[11]), g53_v8(cr[12], cr[13]) };
        /* reference: the helpers, quad by quad (M0..M3 = r35 r36 r33 r34); cr: F01, P23, P45, P67, R23, R45, R67 */
        u128 st_ref[8], F[8], P3[8], R[8];
        st_ref[0] = cr[0]; st_ref[1] = cr[1];
        for (int k = 0; k < 6; k++) st_ref[2 + k] = spu_fma(cr[8 + k], m[3], cr[2 + k]);
        for (int k = 0; k < 8; k++) {
            const u128 p1 = spu_fm(v[k], m[0]);
            const u128 p2 = spu_fma(spu_rotqbyi(v[k], 4), m[1], p1);
            P3[k] = spu_fma(spu_rotqbyi(v[k], 8), m[2], p2);
            R[k] = spu_rotqbyi(v[k], 12);
            F[k] = spu_fma(R[k], m[3], P3[k]);
        }
        for (int spec = mplain; spec >= 0; spec--) {
            g53_carry o; v8 s[4];
            if (!g53_iter(ls, 0x1000, M, zM, &ci, &o, s, spec)) continue;
            kept += spec; iters++;
            int e = 0;
            for (int k = 0; k < 4; k++) e += !g53_eq(st_ref[2 * k], g53_lo(s[k])) + !g53_eq(st_ref[2 * k + 1], g53_hi(s[k]));
            e += !g53_eq(F[0], g53_lo(o.F01)) + !g53_eq(F[1], g53_hi(o.F01));
            const v8* Po[3] = { &o.P23, &o.P45, &o.P67 };
            const v8* Ro[3] = { &o.R23, &o.R45, &o.R67 };
            for (int k = 0; k < 3; k++)
                e += !g53_eq(P3[2 + 2 * k], g53_lo(*Po[k])) + !g53_eq(P3[3 + 2 * k], g53_hi(*Po[k])) +
                     !g53_eq(R[2 + 2 * k], g53_lo(*Ro[k])) + !g53_eq(R[3 + 2 * k], g53_hi(*Ro[k]));
            bad_it += e != 0;
        }
    }
    free(ls);
    const long slow = s53_fp_slow - slow0;
    s53_fp_slow = slow0;
    fprintf(stderr, "[edge-f538] self-test against the 4-lane helpers: checked fm/fma and rotqbyi 4/8/12 x8 %ld of 200000 "
                    "differ (%ld 8-lane ops took the 4-lane fallback); whole iterations %ld of %ld differ (%ld speculative "
                    "kept)\n", bad, slow, bad_it, iters, kept);
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s53_mode = -1;

int gow_edge_f538_mode(void)
{
    if (s53_mode < 0) {
        const char* e = getenv("GOW_EDGE_F538");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        const char* v = getenv("GOW_EDGE_F538_V2");
        s53_v2 = v && atoi(v) == 1;
        s53_mode = m;
        if (!m) fprintf(stderr, "[edge-f538] GOW_EDGE_F538=0 (lifted loop)\n");
        else {
            fprintf(stderr, "[edge-f538] GOW_EDGE_F538=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : s53_v2 ? "packed v2" : "packed v3");
            g53_selftest();
        }
    }
    return s53_mode;
}

static int s53_fault = -1;
static long s53_fault_at, s53_runs;
static void g53_fault(spu_context* ctx)
{
    if (s53_fault < 0) {
        const char* f = getenv("GOW_EDGE_F538_FAULT");
        s53_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s53_fault_at = k ? atol(k + 1) : 0;
    }
    if (s53_fault <= 0 || s53_runs++ != s53_fault_at) return;
    const u128* r = ctx->gpr;
    switch (s53_fault) {
    case 1: ctx->ls[((r[58]._u32[0] + r[39]._u32[0] - 128u) & (SPU_LS_MASK & ~0xFu)) + 15] ^= 1; break;  /* ST(r58 + r39, r20) */
    case 2: ctx->gpr[20]._u32[0] ^= 1; break;
    default: break;
    }
    fprintf(stderr, "[edge-f538] fault %d injected (native run %ld)\n", s53_fault, s53_fault_at);
    s53_fault = 0;
}

static void g53_run(spu_context* ctx)
{
    if (s53_mode == 2 || s53_mode == 4) g53_v1(ctx);
    else                                g53_pk(ctx);
    if (__builtin_expect(s53_fault != 0, 0)) g53_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then jumps to its exit. */
void gow_edge_f538_f620(spu_context* ctx) { g53_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} g53_check_state;
static _Thread_local g53_check_state* t53_chk;

static long s53_calls, s53_bad, s53_iters;
static uint64_t s53_cyc_nat, s53_cyc_lift;
static int s53_atexit;

static void g53_check_report(void)
{
    fprintf(stderr, "[edge-f538-check] %ld calls checked, %ld differ; %ld iterations (8 quads each); tsc: native %.1f M, "
                    "lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s53_calls, s53_bad, s53_iters, s53_cyc_nat / 1e6, s53_cyc_lift / 1e6,
            s53_iters ? (double)s53_cyc_nat / (double)s53_iters : 0.0,
            s53_iters ? (double)s53_cyc_lift / (double)s53_iters : 0.0);
    fprintf(stderr, "[edge-f538-check] packed %ld iterations; entries left to v1 (short): %ld; entries on the checked "
                    "ops (columns not plain): %ld; speculative iterations recomputed: %ld; 8-lane ops redone by the "
                    "4-lane helpers: %ld\n", s53_pk_iters, s53_fb_short, s53_fb_inv, s53_redo, s53_fp_slow);
}

void gow_edge_f538_check_begin(spu_context* ctx)
{
    g53_check_state* c = t53_chk;
    if (!c) {
        c = (g53_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t53_chk = c;
    }
    if (!s53_atexit) { s53_atexit = 1; atexit(g53_check_report); }
    s53_iters += (long)ctx->gpr[37]._u32[0];
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    g53_run(ctx);
    s53_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_f538_check_end(spu_context* ctx)
{
    g53_check_state* c = t53_chk;
    if (!c || !c->pending) return;
    s53_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s53_calls++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-f538-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-f538-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s53_bad++;
        fprintf(stderr, "[edge-f538-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) g53_check_report();   /* a game run ends by kill: atexit never runs */
}

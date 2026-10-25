/* gow_edge_native_a880.c -- native replacement for the hot loop of the EDGE job module's (d935, lift
 * spu/jobmod_d93503647dd8_e6d98) function A880: the loop at loc_0000A950 (exit loc_0000AD48). The d935 lift
 * is built through gen_edge_native_lift.py, which puts a switch before the loop; GOW_EDGE_A880 picks the
 * path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native.c (-O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1,
 * SPU_FP_CHECK_HOOK=0): the float helpers are the same inline helpers the lift uses. Runs inside
 * spu_run_with_halt, i.e. with MXCSR rounding toward zero.
 *
 * Clean-room: written from our own lift of the module and our own captures.
 *
 * GOW_EDGE_A880 (read once):
 *   0/unset  lifted loop
 *   1        native, packed (8 elements per vector, one range check per iteration: "v3"); with
 *            GOW_EDGE_A880_V2=1 every iteration runs the checked ops instead ("v2": the per-lane rule and
 *            the 4-lane fallback on every float op)
 *   2        native v1 (the loop statement for statement, registers in locals)
 *   3        check the packed version (v3, or v2 with GOW_EDGE_A880_V2=1): run it on a copy of the state,
 *            then the lifted loop, and compare all GPRs and the whole LS at the loop exit (loc_AD48)
 *   4        check v1, the same way
 * GOW_EDGE_A880_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 15 of the first quad the last iteration stores, 2 bit 0 of r73 after the loop. (Some jobs call A880
 * twice on the same buffers, and the second call rewrites them: a fault in the first call of such a job
 * never reaches the job's end; k picks a later run.)
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
static inline __m128i g88_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 g88_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = g88_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void g88_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g88_bswap32x4(x));
}
#define LD(a)    g88_ld(ls, (a))
#define ST(a, v) g88_st(ls, (a), (v))

/* the lifted loop: GOW_A88_*. Game code, so it is not in the repo: gen_edge_a880_body.py writes it from
 * the d935 lift at build time (and checks its shape, its inputs and its operation counts). */
#include "gow_edge_a880_body.h"

/* =====================================================================================================
 * v1: the loop statement for statement, registers in C locals, direct LS access. The control flow is the
 * lift's: A950: BODY; if (r46 != 0) goto A950;
 * ===================================================================================================== */
static void g88_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_A88_REGS()
    GOW_A88_LOAD()
    do {
        GOW_A88_BODY()
    } while (r46._u32[0] != 0);
    GOW_A88_STORE()
}

/* ---- 8-lane building blocks (here: one lane per element of an iteration, structure of arrays) ---- */
typedef __m256i v8;

static inline v8 g88_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline u128 g88_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 g88_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline v8 g88_ld2(const uint8_t* ls, uint32_t lo, uint32_t hi)
{
    const __m128i a = _mm_loadu_si128((const __m128i*)(ls + (lo & (SPU_LS_MASK & ~0xFu))));
    const __m128i b = _mm_loadu_si128((const __m128i*)(ls + (hi & (SPU_LS_MASK & ~0xFu))));
    return _mm256_shuffle_epi8(_mm256_set_m128i(b, a),
                               _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline void g88_stlo(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g88_bswap32x4(_mm256_castsi256_si128(v))); }
static inline void g88_sthi(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g88_bswap32x4(_mm256_extracti128_si256(v, 1))); }
static inline v8 g88_f(__m256 x) { return _mm256_castps_si256(x); }
static inline __m256 g88_p(v8 x) { return _mm256_castsi256_ps(x); }

/* fcgt: the toolkit's key compare (spu__sse_key: magnitude with denormals as 0, negated for negatives) */
static inline v8 g88_key(v8 x)
{
    const v8 m = _mm256_and_si256(x, _mm256_set1_epi32(0x7FFFFFFF));
    const v8 den = _mm256_cmpeq_epi32(_mm256_and_si256(x, _mm256_set1_epi32(0x7F800000)), _mm256_setzero_si256());
    const v8 mag = _mm256_andnot_si256(den, m);
    const v8 neg = _mm256_srai_epi32(x, 31);
    return _mm256_sub_epi32(_mm256_xor_si256(mag, neg), neg);
}
static inline v8 g88_fcgt(v8 a, v8 b) { return _mm256_cmpgt_epi32(g88_key(a), g88_key(b)); }
/* frsqest (one gather for the 64-entry table) / fi: spu_helpers.h's forms, integer work on the bit fields */
static inline v8 g88_frsqest(v8 x)
{
    const v8 exp = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(x, 18), _mm256_set1_epi32(0x3F));
    const v8 f = _mm256_and_si256(_mm256_i32gather_epi32((const int*)spu_frsqest_fraction_lut, idx, 4), _mm256_set1_epi32(0x7FFFFF));
    v8 rexp = _mm256_sub_epi32(_mm256_set1_epi32(190), _mm256_srli_epi32(_mm256_add_epi32(exp, _mm256_set1_epi32(1)), 1));
    rexp = _mm256_blendv_epi8(rexp, _mm256_set1_epi32(255), _mm256_cmpeq_epi32(exp, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_slli_epi32(rexp, 23), f);
}
static inline v8 g88_fi(v8 a, v8 est)
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

/* fm / fma / fnms / fa eight lanes at a time, with the per-lane rule of the SPU_FP_NATIVE helpers
 * (spu_helpers.h, spu__xn_*): the native op rounded toward zero is kept when every input is an ordinary
 * single or +-0 and the result is an ordinary single below the top binade, or a zero that is exact
 * (fm/fma/fnms: a zero factor, and a zero addend; fa: any zero result), which loses its sign. If any lane
 * fails, the half it is in is redone by the 4-lane helper itself, which returns the same result the lifted
 * code gets (the helpers work lane by lane, so any grouping of lanes into quadwords gives each lane's
 * own result). */
static inline v8 g88_bexp(v8 x) { return _mm256_srli_epi32(_mm256_slli_epi32(x, 1), 24); }
static inline v8 g88_zero(v8 x) { return _mm256_cmpeq_epi32(_mm256_slli_epi32(x, 1), _mm256_setzero_si256()); }
static inline v8 g88_plain(v8 x)
{
    const v8 e = g88_bexp(x);
    return _mm256_or_si256(_mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()),
                                            _mm256_cmpgt_epi32(_mm256_set1_epi32(255), e)), g88_zero(x));
}
static inline v8 g88_okr(v8 r)
{
    const v8 e = g88_bexp(r);
    return _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
}
static inline v8 g88_pos0(v8 r, v8 z) { return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r); }
static inline int g88_all(v8 m) { return _mm256_movemask_ps(_mm256_castsi256_ps(m)) == 0xFF; }

static long s88_fp_slow;   /* 8-lane ops redone by the 4-lane helpers (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) v8 g88_fm_slow(v8 a, v8 b)
{ s88_fp_slow++; return g88_v8(spu_fm(g88_lo(a), g88_lo(b)), spu_fm(g88_hi(a), g88_hi(b))); }
static __attribute__((noinline, cold)) v8 g88_fma_slow(v8 a, v8 b, v8 c)
{ s88_fp_slow++; return g88_v8(spu_fma(g88_lo(a), g88_lo(b), g88_lo(c)), spu_fma(g88_hi(a), g88_hi(b), g88_hi(c))); }
static __attribute__((noinline, cold)) v8 g88_fnms_slow(v8 a, v8 b, v8 c)
{ s88_fp_slow++; return g88_v8(spu_fnms(g88_lo(a), g88_lo(b), g88_lo(c)), spu_fnms(g88_hi(a), g88_hi(b), g88_hi(c))); }
static __attribute__((noinline, cold)) v8 g88_fa_slow(v8 a, v8 b)
{ s88_fp_slow++; return g88_v8(spu_fa(g88_lo(a), g88_lo(b)), spu_fa(g88_hi(a), g88_hi(b))); }

static inline v8 g88_fm(v8 a, v8 b)
{
    const v8 r = g88_f(_mm256_mul_ps(g88_p(a), g88_p(b)));
    const v8 z = _mm256_or_si256(g88_zero(a), g88_zero(b));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g88_plain(a), g88_plain(b)), _mm256_or_si256(g88_okr(r), z));
    if (__builtin_expect(!g88_all(ok), 0)) return g88_fm_slow(a, b);
    return g88_pos0(r, z);
}
static inline v8 g88_fmx(v8 x, v8 y, v8 w, int* bad)   /* x*y + w, the lane rule of spu__xn_fma */
{
    const v8 r = g88_f(_mm256_fmadd_ps(g88_p(x), g88_p(y), g88_p(w)));
    const v8 z = _mm256_and_si256(_mm256_or_si256(g88_zero(x), g88_zero(y)), g88_zero(w));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(_mm256_and_si256(g88_plain(x), g88_plain(y)), g88_plain(w)),
                                   _mm256_or_si256(g88_okr(r), z));
    *bad = !g88_all(ok);
    return g88_pos0(r, z);
}
static inline v8 g88_fma(v8 a, v8 b, v8 c)   /* a*b + c */
{
    int bad; const v8 r = g88_fmx(a, b, c, &bad);
    if (__builtin_expect(bad, 0)) return g88_fma_slow(a, b, c);
    return r;
}
static inline v8 g88_fnms(v8 a, v8 b, v8 c)  /* c - a*b: spu__xn_fma with a's sign flipped */
{
    int bad; const v8 r = g88_fmx(_mm256_xor_si256(a, _mm256_set1_epi32((int)0x80000000u)), b, c, &bad);
    if (__builtin_expect(bad, 0)) return g88_fnms_slow(a, b, c);
    return r;
}
static inline v8 g88_fa(v8 a, v8 b)
{
    const v8 r = g88_f(_mm256_add_ps(g88_p(a), g88_p(b)));
    const v8 z = g88_zero(r);
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g88_plain(a), g88_plain(b)), _mm256_or_si256(g88_okr(r), z));
    if (__builtin_expect(!g88_all(ok), 0)) return g88_fa_slow(a, b);
    return g88_pos0(r, z);
}

/* ---- one range check per iteration (as the other stages' v3) ----
 * A speculative iteration runs the native ops unchecked and collects, in one running unsigned min and
 * max, the value shifted left by one (sign dropped) of every float input it takes from outside (LS, the
 * frsqest/fi chain) and of every op result: s >= 0x01000000 is exponent >= 1, s <= 0xFDFFFFFF exponent
 * <= 253. Allowed zero lanes (fm/fma/fnms: a zero factor, and a zero addend; fa: any zero result) are
 * lifted out of the min and become +0. The iteration is kept only if every lane passed: then every value
 * it computed was an ordinary single or an allowed zero, so each op's result is the helper's, and the
 * inputs of ops that are results of earlier ops needed no check of their own. Otherwise it is recomputed
 * with the checked ops above before anything is stored. Exponent-254 inputs (allowed by the helpers)
 * trip the range too: that only costs a recompute. */
typedef struct { v8 mn, mx; } g88_rng;
#define G88_AI static inline __attribute__((always_inline))
G88_AI void g88_rng_add(g88_rng* R, v8 lo, v8 hi) { R->mn = _mm256_min_epu32(R->mn, lo); R->mx = _mm256_max_epu32(R->mx, hi); }
G88_AI v8 g88_xin(int spec, g88_rng* R, v8 x)
{
    if (!spec) return _mm256_setzero_si256();
    const v8 s = _mm256_add_epi32(x, x);
    const v8 z = _mm256_cmpeq_epi32(s, _mm256_setzero_si256());
    g88_rng_add(R, _mm256_or_si256(s, z), s);
    return z;
}
G88_AI v8 g88_chk_out(g88_rng* R, v8 r, v8 zz)
{
    const v8 s = _mm256_add_epi32(r, r);
    g88_rng_add(R, _mm256_or_si256(s, zz), s);
    return _mm256_andnot_si256(zz, r);
}
static inline int g88_rng_ok(const g88_rng* R)
{
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(R->mn, _mm256_set1_epi32(0x01000000)), R->mn);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(R->mx, _mm256_set1_epi32((int)0xFDFFFFFFu)), R->mx);
    return g88_all(_mm256_and_si256(lo, hi));
}
/* a float op: speculative (the raw native op, its result tracked; *z = its allowed zero lanes) or checked
 * (spec == 0: the per-lane rule with the 4-lane fallback; z unused) */
G88_AI v8 g88_xfm(int spec, g88_rng* R, v8 a, v8 za, v8 b, v8 zb, v8* z)
{
    if (!spec) { *z = _mm256_setzero_si256(); return g88_fm(a, b); }
    *z = _mm256_or_si256(za, zb);
    return g88_chk_out(R, g88_f(_mm256_mul_ps(g88_p(a), g88_p(b))), *z);
}
G88_AI v8 g88_xfma(int spec, g88_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)   /* a*b + c */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g88_fma(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g88_chk_out(R, g88_f(_mm256_fmadd_ps(g88_p(a), g88_p(b), g88_p(c))), *z);
}
G88_AI v8 g88_xfnms(int spec, g88_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)  /* c - a*b */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g88_fnms(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g88_chk_out(R, g88_f(_mm256_fnmadd_ps(g88_p(a), g88_p(b), g88_p(c))), *z);
}
G88_AI v8 g88_xfa(int spec, g88_rng* R, v8 a, v8 b, v8* z)
{
    if (!spec) { *z = _mm256_setzero_si256(); return g88_fa(a, b); }
    const v8 r = g88_f(_mm256_add_ps(g88_p(a), g88_p(b)));
    *z = g88_zero(r);
    return g88_chk_out(R, r, *z);
}

/* =====================================================================================================
 * Packed. What the loop computes (from the lift; see EDGE_NATIVE.md): per iteration 8 elements; element k
 * takes quad A_k (A_0 at r51 + r81, A_k at r49 + 16k) and quad B_k (B_0 at r51 + r82, B_k at r50 + 16k)
 * and writes one quad (out_0 at r51 + r53, out_k at r48 + 16k); all pointers += 128, r52 += 8, while
 * r83 > r52 (signed). With the loop's controls (r47 = words 1 2 0 0: "yzx", the LS constants at 0xF840 and
 * 0xF850 = rotate by one and by two words, r39 = splat word 0, the immediates eps = 0x0DA2425F, 1.0, 0.5,
 * mask xyz), each output lane j of 0..2 is, op for op as the lift computes that lane:
 *   t  = fm(B, A.yzx)            c  = fnms(A, B.yzx, t) = t - A * B.yzx      (B x A, rotated)
 *   n  = fm(splat(B.w), c.yzx)   (lane 3 masked to 0 before)
 *   q  = fm(n, n)                L  = fa(fa(q.x, q.y), q.z)                  (lane 0, splat)
 *   y  = fi(L, frsqest(L))       r  = fma(fnms(L, fm(y, y), 1.0), fm(y, 0.5), y)
 *   out = L > eps (fcgt) ? fm(n, r) : n, lane 3 = 0
 * i.e. the normalized cross product of B and A scaled by B.w. Here one vector lane is one element (the
 * quads transposed into x/y/z/w vectors), and only the lanes that reach memory are computed: lane 3 of
 * every result and lanes 1-3 of the sums are dropped by the lift's masks and splats. Every value kept is
 * the same op on the same operands as in the lift (the helpers work lane by lane). Within an iteration
 * all loads come before the stores, as in the lift, and the stores keep its order, so the LS sees the
 * same accesses; the controls and the 0xF840/0xF850 constants (the lift reloads those every iteration)
 * are checked at entry, and the output stream must not reach them, else v1. The last iteration runs the
 * lifted body (v1's macros) from the advanced pointers, so every register the loop writes comes out
 * exactly (the loop reads no other register before writing it: gen_edge_a880_body.py checks that).
 * ===================================================================================================== */
typedef struct { v8 X, Y, Z, W; } g88_soa;

G88_AI g88_soa g88_load8(const uint8_t* ls, const uint32_t* a)
{
    const __m256 v0 = g88_p(g88_ld2(ls, a[0], a[4])), v1 = g88_p(g88_ld2(ls, a[1], a[5]));
    const __m256 v2 = g88_p(g88_ld2(ls, a[2], a[6])), v3 = g88_p(g88_ld2(ls, a[3], a[7]));
    const __m256 t0 = _mm256_unpacklo_ps(v0, v1), t1 = _mm256_unpacklo_ps(v2, v3);
    const __m256 t2 = _mm256_unpackhi_ps(v0, v1), t3 = _mm256_unpackhi_ps(v2, v3);
    g88_soa s;
    s.X = g88_f(_mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(1, 0, 1, 0)));
    s.Y = g88_f(_mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(3, 2, 3, 2)));
    s.Z = g88_f(_mm256_shuffle_ps(t2, t3, _MM_SHUFFLE(1, 0, 1, 0)));
    s.W = g88_f(_mm256_shuffle_ps(t2, t3, _MM_SHUFFLE(3, 2, 3, 2)));
    return s;
}

/* one iteration's 8 output quads as (out_k | out_k+4), k = 0..3; returns 0 if a speculative run failed */
G88_AI int g88_iter(const uint8_t* ls, const uint32_t* ia, const uint32_t* ib, v8* o, int spec)
{
    g88_rng R; R.mn = _mm256_set1_epi32(-1); R.mx = _mm256_setzero_si256();
    const g88_soa A = g88_load8(ls, ia), B = g88_load8(ls, ib);
    const v8 zAX = g88_xin(spec, &R, A.X), zAY = g88_xin(spec, &R, A.Y), zAZ = g88_xin(spec, &R, A.Z);
    const v8 zBX = g88_xin(spec, &R, B.X), zBY = g88_xin(spec, &R, B.Y), zBZ = g88_xin(spec, &R, B.Z);
    const v8 zBW = g88_xin(spec, &R, B.W);
    const v8 ONE = _mm256_set1_epi32(0x3F800000), HALF = _mm256_set1_epi32(0x3F000000);
    const v8 EPS = _mm256_set1_epi32(0x0DA2425F), NZ = _mm256_setzero_si256();
    v8 zt0, zt1, zt2, zc0, zc1, zc2, zn0, zn1, zn2, zq0, zq1, zq2, zs1, zL;
    const v8 t0 = g88_xfm(spec, &R, B.X, zBX, A.Y, zAY, &zt0);
    const v8 t1 = g88_xfm(spec, &R, B.Y, zBY, A.Z, zAZ, &zt1);
    const v8 t2 = g88_xfm(spec, &R, B.Z, zBZ, A.X, zAX, &zt2);
    const v8 c0 = g88_xfnms(spec, &R, A.X, zAX, B.Y, zBY, t0, zt0, &zc0);
    const v8 c1 = g88_xfnms(spec, &R, A.Y, zAY, B.Z, zBZ, t1, zt1, &zc1);
    const v8 c2 = g88_xfnms(spec, &R, A.Z, zAZ, B.X, zBX, t2, zt2, &zc2);
    const v8 n0 = g88_xfm(spec, &R, B.W, zBW, c1, zc1, &zn0);
    const v8 n1 = g88_xfm(spec, &R, B.W, zBW, c2, zc2, &zn1);
    const v8 n2 = g88_xfm(spec, &R, B.W, zBW, c0, zc0, &zn2);
    const v8 q0 = g88_xfm(spec, &R, n0, zn0, n0, zn0, &zq0);
    const v8 q1 = g88_xfm(spec, &R, n1, zn1, n1, zn1, &zq1);
    const v8 q2 = g88_xfm(spec, &R, n2, zn2, n2, zn2, &zq2);
    const v8 s1 = g88_xfa(spec, &R, q0, q1, &zs1);
    const v8 L = g88_xfa(spec, &R, s1, q2, &zL);
    const v8 gt = g88_fcgt(L, EPS);
    const v8 y = g88_fi(L, g88_frsqest(L));
    const v8 zy = g88_xin(spec, &R, y);
    v8 zy2, ze, zh, zr, zm0, zm1, zm2;
    const v8 y2 = g88_xfm(spec, &R, y, zy, y, zy, &zy2);
    const v8 e = g88_xfnms(spec, &R, L, zL, y2, zy2, ONE, NZ, &ze);
    const v8 h = g88_xfm(spec, &R, y, zy, HALF, NZ, &zh);
    const v8 r = g88_xfma(spec, &R, e, ze, h, zh, y, zy, &zr);
    const v8 m0 = g88_xfm(spec, &R, n0, zn0, r, zr, &zm0);
    const v8 m1 = g88_xfm(spec, &R, n1, zn1, r, zr, &zm1);
    const v8 m2 = g88_xfm(spec, &R, n2, zn2, r, zr, &zm2);
    if (spec && !g88_rng_ok(&R)) return 0;
    const __m256 ox = g88_p(_mm256_blendv_epi8(n0, m0, gt)), oy = g88_p(_mm256_blendv_epi8(n1, m1, gt));
    const __m256 oz = g88_p(_mm256_blendv_epi8(n2, m2, gt)), zero = _mm256_setzero_ps();
    const __m256 u0 = _mm256_unpacklo_ps(ox, oy), u1 = _mm256_unpacklo_ps(oz, zero);
    const __m256 u2 = _mm256_unpackhi_ps(ox, oy), u3 = _mm256_unpackhi_ps(oz, zero);
    o[0] = g88_f(_mm256_shuffle_ps(u0, u1, _MM_SHUFFLE(1, 0, 1, 0)));
    o[1] = g88_f(_mm256_shuffle_ps(u0, u1, _MM_SHUFFLE(3, 2, 3, 2)));
    o[2] = g88_f(_mm256_shuffle_ps(u2, u3, _MM_SHUFFLE(1, 0, 1, 0)));
    o[3] = g88_f(_mm256_shuffle_ps(u2, u3, _MM_SHUFFLE(3, 2, 3, 2)));
    return 1;
}

static int s88_v2;             /* GOW_EDGE_A880_V2=1: the packed loop runs the checked ops on every iteration */
static long s88_fb_short;      /* entries with fewer than 2 iterations (or an implausible count): v1 */
static long s88_fb_ctl;        /* entries with other controls/constants, or stores reaching 0xF840..0xF85F: v1 */
static long s88_pk_iters;      /* iterations run packed */
static long s88_redo;          /* speculative iterations recomputed with the checked ops */

static int g88_quad_is(u128 x, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return x._u32[0] == a && x._u32[1] == b && x._u32[2] == c && x._u32[3] == d;
}
static inline u128 g88_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}
/* iterations of the loop from its entry registers: r52 += 8 until !(r83 > r52), signed, word 0 */
static uint32_t g88_count(const u128* g)
{
    const int32_t lim = (int32_t)g[83]._u32[0];
    uint32_t k = g[52]._u32[0], n = 0;
    do { k += 8; n++; } while (lim > (int32_t)k && n <= 4096);
    return n;
}

static void g88_pk(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    const uint32_t n = g88_count(g);
    if (n < 2 || n > SPU_LS_SIZE / 128) { s88_fb_short++; g88_v1(ctx); return; }
    if (!g88_quad_is(g[47], 0x04050607u, 0x08090A0Bu, 0x00010203u, 0x00010203u) ||
        !g88_quad_is(g88_ld(ls, 0xF840), 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu, 0x10111213u) ||
        !g88_quad_is(g88_ld(ls, 0xF850), 0x08090A0Bu, 0x0C0D0E0Fu, 0x10111213u, 0x14151617u)) {
        s88_fb_ctl++; g88_v1(ctx); return;
    }
    const uint32_t b51 = g[51]._u32[0];
    uint32_t ia[8], ib[8], io[8];
    ia[0] = b51 + g[81]._u32[0]; ib[0] = b51 + g[82]._u32[0]; io[0] = b51 + g[53]._u32[0];
    for (int k = 1; k < 8; k++) {
        ia[k] = g[49]._u32[0] + 16u * k; ib[k] = g[50]._u32[0] + 16u * k; io[k] = g[48]._u32[0] + 16u * k;
    }
    /* the packed iterations' stores must not reach the constants the lift reloads every iteration */
    for (uint32_t i = 0; i + 1 < n; i++)
        for (int k = 0; k < 8; k++) {
            const uint32_t a = (io[k] + 128u * i) & (SPU_LS_MASK & ~0xFu);
            if (a == 0xF840 || a == 0xF850) { s88_fb_ctl++; g88_v1(ctx); return; }
        }
    const int spec = !s88_v2;
    for (uint32_t i = 0; i + 1 < n; i++) {
        v8 o[4];
        if (!spec || !g88_iter(ls, ia, ib, o, 1)) {
            if (spec) s88_redo++;
            g88_iter(ls, ia, ib, o, 0);
        }
        /* the lift's store order: out_0, out_7, out_1 .. out_6 */
        g88_stlo((uint8_t*)ls, io[0], o[0]); g88_sthi((uint8_t*)ls, io[7], o[3]);
        g88_stlo((uint8_t*)ls, io[1], o[1]); g88_stlo((uint8_t*)ls, io[2], o[2]); g88_stlo((uint8_t*)ls, io[3], o[3]);
        g88_sthi((uint8_t*)ls, io[4], o[0]); g88_sthi((uint8_t*)ls, io[5], o[1]); g88_sthi((uint8_t*)ls, io[6], o[2]);
        for (int k = 0; k < 8; k++) { ia[k] += 128u; ib[k] += 128u; io[k] += 128u; }
    }
    s88_pk_iters += (long)n - 1;
    /* the last iteration through the lifted body, from the pointers after iteration n - 2 */
    GOW_A88_REGS()
    GOW_A88_LOAD()
    r48 = g88_addw(r48, 128u * (n - 1)); r49 = g88_addw(r49, 128u * (n - 1)); r50 = g88_addw(r50, 128u * (n - 1));
    r51 = g88_addw(r51, 128u * (n - 1)); r52 = g88_addw(r52, 8u * (n - 1));
    do {
        GOW_A88_BODY()
    } while (r46._u32[0] != 0);
    GOW_A88_STORE()
}

/* ---- self-test of the 8-lane integer forms (frsqest, fi, fcgt) against the 4-lane helpers ---- */
static uint32_t s88_rng = 0x2545F491u;
static uint32_t g88_rand(void) { s88_rng ^= s88_rng << 13; s88_rng ^= s88_rng >> 17; s88_rng ^= s88_rng << 5; return s88_rng; }
static void g88_selftest(void)
{
    long bad = 0, bad2 = 0;
    const long slow0 = s88_fp_slow;
    for (int it = 0; it < 20000; it++) {
        u128 a[2], b[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                uint32_t x = g88_rand(), y = g88_rand();
                if ((it & 7) == 1) x &= 0x807FFFFFu;            /* exponent 0 */
                if ((it & 7) == 2) x |= 0x7F800000u;            /* exponent 255 */
                if ((it & 7) == 3) y = x;                        /* equal operands for fcgt */
                if ((it & 7) == 4) x = (x & 0x80FFFFFFu) | 0x3F000000u;   /* near 1.0 */
                if ((it & 7) == 5) y = 0x0DA2425Fu;              /* the loop's eps */
                a[h]._u32[i] = x; b[h]._u32[i] = y;
            }
        const v8 A = g88_v8(a[0], a[1]), B = g88_v8(b[0], b[1]);
        const v8 rs = g88_frsqest(A), fi = g88_fi(A, B), gt = g88_fcgt(A, B), fi2 = g88_fi(A, rs);
        /* the checked float ops (their 4-lane fallback included) against the helpers, on operands with
         * zeros, exponent 0/255/254 and cancelling products mixed in */
        u128 c[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                uint32_t z = g88_rand();
                if ((it & 3) == 1) z = 0;
                if ((it & 15) == 6) z = a[h]._u32[i] ^ 0x80000000u;
                if ((it & 15) == 7) z = 0x7F000000u | (z & 0x807FFFFFu);
                c[h]._u32[i] = z;
            }
        const v8 C = g88_v8(c[0], c[1]);
        const v8 xm = g88_fm(A, B), xa = g88_fa(A, C), xn = g88_fnms(A, B, C), xf = g88_fma(A, C, B);
        for (int h = 0; h < 2; h++) {
            const u128 e5 = spu_fm(a[h], b[h]), e6 = spu_fa(a[h], c[h]), e7 = spu_fnms(a[h], b[h], c[h]);
            const u128 e8 = spu_fma(a[h], c[h], b[h]);
            const u128 g5 = h ? g88_hi(xm) : g88_lo(xm), g6 = h ? g88_hi(xa) : g88_lo(xa);
            const u128 g7 = h ? g88_hi(xn) : g88_lo(xn), g8 = h ? g88_hi(xf) : g88_lo(xf);
            bad2 += memcmp(&e5, &g5, 16) != 0;
            bad2 += memcmp(&e6, &g6, 16) != 0;
            bad2 += memcmp(&e7, &g7, 16) != 0;
            bad2 += memcmp(&e8, &g8, 16) != 0;
        }
        for (int h = 0; h < 2; h++) {
            const u128 e1 = spu_frsqest(a[h]), e2 = spu_fi(a[h], b[h]), e3 = spu_fcgt(a[h], b[h]), e4 = spu_fi(a[h], e1);
            const u128 g1 = h ? g88_hi(rs) : g88_lo(rs), g2 = h ? g88_hi(fi) : g88_lo(fi);
            const u128 g3 = h ? g88_hi(gt) : g88_lo(gt), g4 = h ? g88_hi(fi2) : g88_lo(fi2);
            bad += memcmp(&e1, &g1, 16) != 0;
            bad += memcmp(&e2, &g2, 16) != 0;
            bad += memcmp(&e3, &g3, 16) != 0;
            bad += memcmp(&e4, &g4, 16) != 0;
        }
    }
    const long slow = s88_fp_slow - slow0;
    s88_fp_slow = slow0;
    fprintf(stderr, "[edge-a880] self-test against the 4-lane helpers: frsqest/fi/fcgt x8 %ld of 160000 differ; checked "
                    "fm/fa/fnms/fma x8 %ld of 160000 differ (%ld 8-lane ops took the 4-lane fallback)\n", bad, bad2, slow);
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s88_mode = -1;

int gow_edge_a880_mode(void)
{
    if (s88_mode < 0) {
        const char* e = getenv("GOW_EDGE_A880");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop (for now) */
        if (m < 0 || m > 4) m = 0;
        const char* v = getenv("GOW_EDGE_A880_V2");
        s88_v2 = v && atoi(v) == 1;
        s88_mode = m;
        if (!m) fprintf(stderr, "[edge-a880] GOW_EDGE_A880=0 (lifted loop)\n");
        else {
            fprintf(stderr, "[edge-a880] GOW_EDGE_A880=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : s88_v2 ? "packed v2" : "packed v3");
            g88_selftest();
        }
    }
    return s88_mode;
}

static int s88_fault = -1;
static long s88_fault_at, s88_runs;
static void g88_fault(spu_context* ctx)
{
    if (s88_fault < 0) {
        const char* f = getenv("GOW_EDGE_A880_FAULT");
        s88_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s88_fault_at = k ? atol(k + 1) : 0;
    }
    if (s88_fault <= 0 || s88_runs++ != s88_fault_at) return;
    const u128* r = ctx->gpr;
    switch (s88_fault) {
    case 1: ctx->ls[((r[51]._u32[0] - 128u + r[53]._u32[0]) & (SPU_LS_MASK & ~0xFu)) + 15] ^= 1; break;  /* ST(r51 + r53, r20) */
    case 2: ctx->gpr[73]._u32[0] ^= 1; break;
    default: break;
    }
    fprintf(stderr, "[edge-a880] fault %d injected (native run %ld)\n", s88_fault, s88_fault_at);
    s88_fault = 0;
}

static void g88_run(spu_context* ctx)
{
    if (s88_mode == 2 || s88_mode == 4) g88_v1(ctx);
    else                                g88_pk(ctx);
    if (__builtin_expect(s88_fault != 0, 0)) g88_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then jumps to its exit. */
void gow_edge_a880_a950(spu_context* ctx) { g88_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. loc_AD48 is
 * also reached by the skip in the A928 block (count not positive), hence `pending`. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} g88_check_state;
static _Thread_local g88_check_state* t88_chk;

static long s88_calls, s88_bad, s88_iters;
static uint64_t s88_cyc_nat, s88_cyc_lift;
static int s88_atexit;

static void g88_check_report(void)
{
    fprintf(stderr, "[edge-a880-check] %ld calls checked, %ld differ; %ld iterations (8 elements each); tsc: native %.1f M, "
                    "lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s88_calls, s88_bad, s88_iters, s88_cyc_nat / 1e6, s88_cyc_lift / 1e6,
            s88_iters ? (double)s88_cyc_nat / (double)s88_iters : 0.0,
            s88_iters ? (double)s88_cyc_lift / (double)s88_iters : 0.0);
    fprintf(stderr, "[edge-a880-check] packed %ld iterations; entries left to v1: %ld short, %ld controls/constants; "
                    "speculative iterations recomputed: %ld; 8-lane ops redone by the 4-lane helpers: %ld\n",
            s88_pk_iters, s88_fb_short, s88_fb_ctl, s88_redo, s88_fp_slow);
}

void gow_edge_a880_check_begin(spu_context* ctx)
{
    g88_check_state* c = t88_chk;
    if (!c) {
        c = (g88_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t88_chk = c;
    }
    if (!s88_atexit) { s88_atexit = 1; atexit(g88_check_report); }
    s88_iters += (long)g88_count(ctx->gpr);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    g88_run(ctx);
    s88_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_a880_check_end(spu_context* ctx)
{
    g88_check_state* c = t88_chk;
    if (!c || !c->pending) return;
    s88_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s88_calls++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-a880-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-a880-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s88_bad++;
        fprintf(stderr, "[edge-a880-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) g88_check_report();   /* a game run ends by kill: atexit never runs */
}

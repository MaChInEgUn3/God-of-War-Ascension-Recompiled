/* gow_edge_native_6ac8.c -- native replacement for the loop nest of the EDGE job module's (d935, lift
 * spu/jobmod_d93503647dd8_e6d98) function 6AC8: the outer loop at loc_00006BE0 with its inner loop at
 * loc_00006DE0, up to the exit loc_00006F98. The d935 lift is built through gen_edge_native_lift.py, which
 * puts a switch before the outer loop; GOW_EDGE_6AC8 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native.c (-O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1,
 * SPU_FP_CHECK_HOOK=0): the float helpers are the same inline helpers the lift uses. Runs inside
 * spu_run_with_halt, i.e. with MXCSR rounding toward zero.
 *
 * Clean-room: written from our own lift of the module and our own captures.
 *
 * GOW_EDGE_6AC8 (read once):
 *   0        lifted loop nest
 *   1/unset  native, packed (two quads per vector, one range check per sphere step: "v3"); with
 *            GOW_EDGE_6AC8_V2=1 every iteration runs the checked ops instead ("v2": the per-lane rule and
 *            the 4-lane fallback on every float op)
 *   2        native v1 (the loop nest statement for statement, registers in locals)
 *   3        check the packed version (v3, or v2 with GOW_EDGE_6AC8_V2=1): run it on a copy of the state,
 *            then the lifted loop nest, and compare all GPRs and the whole LS at its exit
 *   4        check v1, the same way
 * GOW_EDGE_6AC8_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 15 of the first quad the last outer iteration stores, 2 bit 0 of r33 after the loop nest.
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
static inline __m128i g6a_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 g6a_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = g6a_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void g6a_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g6a_bswap32x4(x));
}
#define LD(a)    g6a_ld(ls, (a))
#define ST(a, v) g6a_st(ls, (a), (v))

/* the lifted loop nest: GOW_6A_*. Game code, so it is not in the repo: gen_edge_6ac8_body.py writes it
 * from the d935 lift at build time (and checks its shape, its inputs and its dataflow). */
#include "gow_edge_6ac8_body.h"

/* =====================================================================================================
 * v1: the loop nest statement for statement, registers in C locals, direct LS access. The control flow is
 * the lift's: 6BE0: A; if (r85 == 0) goto 6F40; PRE; 6DE0: INNER; if (r18 == 0) goto 6DE0;
 * 6F40: TAIL; if (r76 != 0) goto 6BE0; 6F98:
 * ===================================================================================================== */
static void g6a_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_6A_REGS()
    GOW_6A_LOAD()
    do {
        GOW_6A_A()
        if (r85._u32[0] != 0) {
            GOW_6A_PRE()
            do {
                GOW_6A_INNER()
            } while (r18._u32[0] == 0);
        }
        GOW_6A_TAIL()
    } while (r76._u32[0] != 0);
    GOW_6A_STORE()
}

/* ---- 8-lane building blocks (two quads per vector: quad j in the low half, j+4 in the high) ---- */
typedef __m256i v8;

static inline v8 g6a_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 g6a_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(a); }
static inline u128 g6a_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 g6a_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline v8 g6a_ld2(const uint8_t* ls, uint32_t lo, uint32_t hi)
{
    const __m128i a = _mm_loadu_si128((const __m128i*)(ls + (lo & (SPU_LS_MASK & ~0xFu))));
    const __m128i b = _mm_loadu_si128((const __m128i*)(ls + (hi & (SPU_LS_MASK & ~0xFu))));
    return _mm256_shuffle_epi8(_mm256_set_m128i(b, a),
                               _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline void g6a_stlo(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g6a_bswap32x4(_mm256_castsi256_si128(v))); }
static inline void g6a_sthi(uint8_t* ls, uint32_t lsa, v8 v) { _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g6a_bswap32x4(_mm256_extracti128_si256(v, 1))); }
/* word 3 of the big-endian quad at lsa: rotqbyi(LD(lsa), 12)._u32[0], the lift's gather offset */
static inline uint32_t g6a_w3(const uint8_t* ls, uint32_t lsa)
{
    uint32_t w; memcpy(&w, ls + (lsa & (SPU_LS_MASK & ~0xFu)) + 12, 4); return __builtin_bswap32(w);
}
static inline v8 g6a_f(__m256 x) { return _mm256_castps_si256(x); }
static inline __m256 g6a_p(v8 x) { return _mm256_castsi256_ps(x); }
/* word k of each quad in all four of its words: shufb(x, x, r47/r41/r39/r35) with the controls checked */
#define G6A_SPL(x, k) _mm256_shuffle_epi32((x), (k) * 0x55)

/* fcgt: the toolkit's key compare (spu__sse_key: magnitude with denormals as 0, negated for negatives) */
static inline v8 g6a_key(v8 x)
{
    const v8 m = _mm256_and_si256(x, _mm256_set1_epi32(0x7FFFFFFF));
    const v8 den = _mm256_cmpeq_epi32(_mm256_and_si256(x, _mm256_set1_epi32(0x7F800000)), _mm256_setzero_si256());
    const v8 mag = _mm256_andnot_si256(den, m);
    const v8 neg = _mm256_srai_epi32(x, 31);
    return _mm256_sub_epi32(_mm256_xor_si256(mag, neg), neg);
}
static inline v8 g6a_fcgt(v8 a, v8 b) { return _mm256_cmpgt_epi32(g6a_key(a), g6a_key(b)); }
/* frsqest (one gather for the 64-entry table) / fi: spu_helpers.h's forms, integer work on the bit fields */
static inline v8 g6a_frsqest(v8 x)
{
    const v8 exp = _mm256_and_si256(_mm256_srli_epi32(x, 23), _mm256_set1_epi32(0xFF));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(x, 18), _mm256_set1_epi32(0x3F));
    const v8 f = _mm256_and_si256(_mm256_i32gather_epi32((const int*)spu_frsqest_fraction_lut, idx, 4), _mm256_set1_epi32(0x7FFFFF));
    v8 rexp = _mm256_sub_epi32(_mm256_set1_epi32(190), _mm256_srli_epi32(_mm256_add_epi32(exp, _mm256_set1_epi32(1)), 1));
    rexp = _mm256_blendv_epi8(rexp, _mm256_set1_epi32(255), _mm256_cmpeq_epi32(exp, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_slli_epi32(rexp, 23), f);
}
static inline v8 g6a_fi(v8 a, v8 est)
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

/* fm / fma / fnms / fs eight lanes at a time, with the per-lane rule of the SPU_FP_NATIVE helpers
 * (spu_helpers.h, spu__xn_*): the native op rounded toward zero is kept when every input is an ordinary
 * single or +-0 and the result is an ordinary single below the top binade, or a zero that is exact
 * (fm/fma/fnms: a zero factor, and a zero addend; fs: any zero result), which loses its sign. If any lane
 * fails, the half it is in is redone by the 4-lane helper itself, which returns the same result the lifted
 * code gets (the helpers work lane by lane, so any grouping of lanes into quadwords gives each lane's own
 * result). */
static inline v8 g6a_bexp(v8 x) { return _mm256_srli_epi32(_mm256_slli_epi32(x, 1), 24); }
static inline v8 g6a_zero(v8 x) { return _mm256_cmpeq_epi32(_mm256_slli_epi32(x, 1), _mm256_setzero_si256()); }
static inline v8 g6a_plain(v8 x)
{
    const v8 e = g6a_bexp(x);
    return _mm256_or_si256(_mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()),
                                            _mm256_cmpgt_epi32(_mm256_set1_epi32(255), e)), g6a_zero(x));
}
static inline v8 g6a_okr(v8 r)
{
    const v8 e = g6a_bexp(r);
    return _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
}
static inline v8 g6a_pos0(v8 r, v8 z) { return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r); }
static inline int g6a_all(v8 m) { return _mm256_movemask_ps(_mm256_castsi256_ps(m)) == 0xFF; }
#define G6A_SIGN _mm256_set1_epi32((int)0x80000000u)

static long s6a_fp_slow;   /* 8-lane ops redone by the 4-lane helpers (lab statistic; racy across threads) */
static __attribute__((noinline, cold)) v8 g6a_fm_slow(v8 a, v8 b)
{ s6a_fp_slow++; return g6a_v8(spu_fm(g6a_lo(a), g6a_lo(b)), spu_fm(g6a_hi(a), g6a_hi(b))); }
static __attribute__((noinline, cold)) v8 g6a_fma_slow(v8 a, v8 b, v8 c)
{ s6a_fp_slow++; return g6a_v8(spu_fma(g6a_lo(a), g6a_lo(b), g6a_lo(c)), spu_fma(g6a_hi(a), g6a_hi(b), g6a_hi(c))); }
static __attribute__((noinline, cold)) v8 g6a_fnms_slow(v8 a, v8 b, v8 c)
{ s6a_fp_slow++; return g6a_v8(spu_fnms(g6a_lo(a), g6a_lo(b), g6a_lo(c)), spu_fnms(g6a_hi(a), g6a_hi(b), g6a_hi(c))); }
static __attribute__((noinline, cold)) v8 g6a_fs_slow(v8 a, v8 b)
{ s6a_fp_slow++; return g6a_v8(spu_fs(g6a_lo(a), g6a_lo(b)), spu_fs(g6a_hi(a), g6a_hi(b))); }

static inline v8 g6a_fm(v8 a, v8 b)
{
    const v8 r = g6a_f(_mm256_mul_ps(g6a_p(a), g6a_p(b)));
    const v8 z = _mm256_or_si256(g6a_zero(a), g6a_zero(b));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g6a_plain(a), g6a_plain(b)), _mm256_or_si256(g6a_okr(r), z));
    if (__builtin_expect(!g6a_all(ok), 0)) return g6a_fm_slow(a, b);
    return g6a_pos0(r, z);
}
static inline v8 g6a_fmx(v8 x, v8 y, v8 w, int* bad)   /* x*y + w, the lane rule of spu__xn_fma */
{
    const v8 r = g6a_f(_mm256_fmadd_ps(g6a_p(x), g6a_p(y), g6a_p(w)));
    const v8 z = _mm256_and_si256(_mm256_or_si256(g6a_zero(x), g6a_zero(y)), g6a_zero(w));
    const v8 ok = _mm256_and_si256(_mm256_and_si256(_mm256_and_si256(g6a_plain(x), g6a_plain(y)), g6a_plain(w)),
                                   _mm256_or_si256(g6a_okr(r), z));
    *bad = !g6a_all(ok);
    return g6a_pos0(r, z);
}
static inline v8 g6a_fma(v8 a, v8 b, v8 c)   /* a*b + c */
{
    int bad; const v8 r = g6a_fmx(a, b, c, &bad);
    if (__builtin_expect(bad, 0)) return g6a_fma_slow(a, b, c);
    return r;
}
static inline v8 g6a_fnms(v8 a, v8 b, v8 c)  /* c - a*b: spu__xn_fma with a's sign flipped */
{
    int bad; const v8 r = g6a_fmx(_mm256_xor_si256(a, G6A_SIGN), b, c, &bad);
    if (__builtin_expect(bad, 0)) return g6a_fnms_slow(a, b, c);
    return r;
}
static inline v8 g6a_fs(v8 a, v8 b)          /* a - b: spu__xn_fa with b's sign flipped */
{
    const v8 nb = _mm256_xor_si256(b, G6A_SIGN);
    const v8 r = g6a_f(_mm256_add_ps(g6a_p(a), g6a_p(nb)));
    const v8 z = g6a_zero(r);
    const v8 ok = _mm256_and_si256(_mm256_and_si256(g6a_plain(a), g6a_plain(nb)), _mm256_or_si256(g6a_okr(r), z));
    if (__builtin_expect(!g6a_all(ok), 0)) return g6a_fs_slow(a, b);
    return g6a_pos0(r, z);
}

/* ---- one range check per sphere step (the other stages' v3 check one per iteration) ----
 * A speculative step runs the native ops unchecked and collects, in one running unsigned min and
 * max, the value shifted left by one (sign dropped) of every float input it takes from outside (LS, the
 * frsqest/fi chain) and of every op result: s >= 0x01000000 is exponent >= 1, s <= 0xFDFFFFFF exponent
 * <= 253. Allowed zero lanes (fm/fma/fnms: a zero factor, and a zero addend; fs: any zero result) are
 * lifted out of the min and become +0. The step is kept only if every lane passed: then every value it
 * computed was an ordinary single or an allowed zero, so each op's result is the helper's, and the inputs
 * of ops that are results of earlier ops needed no check of their own. Otherwise it is recomputed with the
 * checked ops above (the accumulators it read are untouched until it passes). The loop-invariant
 * constants are checked once per entry. The ops on a step's outside inputs (fs of the positions, fm of the
 * sphere's vector) always run checked and their results are range-checked as inputs. Exponent-254
 * inputs (allowed by the helpers) trip the range too: that only costs a recompute. */
typedef struct { v8 mn, mx; } g6a_rng;
#define G6A_AI static inline __attribute__((always_inline))
G6A_AI void g6a_rng_add(g6a_rng* R, v8 lo, v8 hi) { R->mn = _mm256_min_epu32(R->mn, lo); R->mx = _mm256_max_epu32(R->mx, hi); }
G6A_AI v8 g6a_xin(int spec, g6a_rng* R, v8 x)
{
    if (!spec) return _mm256_setzero_si256();
    const v8 s = _mm256_add_epi32(x, x);
    const v8 z = _mm256_cmpeq_epi32(s, _mm256_setzero_si256());
    g6a_rng_add(R, _mm256_or_si256(s, z), s);
    return z;
}
G6A_AI v8 g6a_chk_out(g6a_rng* R, v8 r, v8 zz)
{
    const v8 s = _mm256_add_epi32(r, r);
    g6a_rng_add(R, _mm256_or_si256(s, zz), s);
    return _mm256_andnot_si256(zz, r);
}
static inline int g6a_rng_ok(const g6a_rng* R)
{
    const v8 lo = _mm256_cmpeq_epi32(_mm256_max_epu32(R->mn, _mm256_set1_epi32(0x01000000)), R->mn);
    const v8 hi = _mm256_cmpeq_epi32(_mm256_min_epu32(R->mx, _mm256_set1_epi32((int)0xFDFFFFFFu)), R->mx);
    return g6a_all(_mm256_and_si256(lo, hi));
}
/* a float op: speculative (the raw native op, its result tracked; *z = its allowed zero lanes) or checked
 * (spec == 0: the per-lane rule with the 4-lane fallback; z unused) */
G6A_AI v8 g6a_xfm(int spec, g6a_rng* R, v8 a, v8 za, v8 b, v8 zb, v8* z)
{
    if (!spec) { *z = _mm256_setzero_si256(); return g6a_fm(a, b); }
    *z = _mm256_or_si256(za, zb);
    return g6a_chk_out(R, g6a_f(_mm256_mul_ps(g6a_p(a), g6a_p(b))), *z);
}
G6A_AI v8 g6a_xfma(int spec, g6a_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)   /* a*b + c */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g6a_fma(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g6a_chk_out(R, g6a_f(_mm256_fmadd_ps(g6a_p(a), g6a_p(b), g6a_p(c))), *z);
}
G6A_AI v8 g6a_xfnms(int spec, g6a_rng* R, v8 a, v8 za, v8 b, v8 zb, v8 c, v8 zc, v8* z)  /* c - a*b */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g6a_fnms(a, b, c); }
    *z = _mm256_and_si256(_mm256_or_si256(za, zb), zc);
    return g6a_chk_out(R, g6a_f(_mm256_fnmadd_ps(g6a_p(a), g6a_p(b), g6a_p(c))), *z);
}
G6A_AI v8 g6a_xfs(int spec, g6a_rng* R, v8 a, v8 b, v8* z)   /* a - b */
{
    if (!spec) { *z = _mm256_setzero_si256(); return g6a_fs(a, b); }
    const v8 r = g6a_f(_mm256_add_ps(g6a_p(a), g6a_p(_mm256_xor_si256(b, G6A_SIGN))));
    *z = g6a_zero(r);
    return g6a_chk_out(R, r, *z);
}

/* =====================================================================================================
 * Packed. What the loop nest computes (from the lift; checked by gen_edge_6ac8_body.py, see
 * EDGE_NATIVE.md): per outer iteration 8 vertices. Vertex k has the offset idx_k = word 3 of the quad at
 * r67 + 16k (r67 += 128), the position quad P_k = LS[r48 + idx_k] and the output quad O_k = LS[r34 + idx_k].
 * The positions are transposed (selb/shufb with r54 r56 r81 r82 r83, as the lift does, by the 4-lane
 * helpers) into X Y Z for vertices 0-3 (r44 r42 r38) and 4-7 (r43 r40 r37). For each of the r64 spheres
 * j (only when r85 != 0): S_j = LS[r51 + o_j], V_j = LS[r50 + o_j] with o_0 = r77, o_1 = r78, o_j = r84 +
 * 16 (j - 2), and, op for op as the lift computes each lane (s.x = word 0 of S splatted, etc.):
 *   D  = fma(dz, dz, fma(dy, dy, fm(dx, dx)))   dx = fs(S.x, X), dy = fs(S.y, Y), dz = fs(S.z, Z)
 *   y  = fi(D, frsqest(D))   d = fm(fma(fnms(fm(y, D), y, 1.0), fm(y, 0.5), y), D)      (1.0 = r29, 0.5 = r52)
 *   x  = fm(d, S.w)   t = fs(1.0, x > 1.0 (fcgt) ? 1.0 : x)   s = fm(fm(t, t), fnms(2.0, t, 3.0))  (r66 r65)
 *   Vs = fm(r53, V_j)   w = fm(fm(s, s), Vs.w)   acc_k = fma(w lane of vertex k, Vs, acc_k)
 * with acc_k = r79 before the first sphere; then out_k = fma(O_k, r49, acc_k) is stored at r34 + idx_k,
 * vertex 0 to 7 in order. (A smoothstep falloff around each sphere, squared and weighted: the meaning is
 * not verified.) The lift software-pipelines the spheres (sphere j's weight, j+1's distance and j+2's
 * squared distance in one inner iteration, loading two spheres past the last); the packed path computes
 * only the spheres that reach the stores, each value the same op on the same operands. Here the 8
 * vertices' X/Y/Z, D, d, x, t, s, w are one vector each (vertices 0-3 low, 4-7 high), and the
 * accumulators pair vertex k with k+4 under the sphere's Vs in both halves. Per outer iteration all loads
 * come before the stores and the stores keep the lift's order, as in the lift, so the LS sees the same
 * stores (it loads a subset of the lift's addresses). The last outer iteration runs the lifted blocks
 * (v1's macros) from r67/r68 advanced, so every register the nest writes comes out exactly (the nest reads
 * no register before writing it but the loop invariants and r67/r68: the generator checks that).
 * ===================================================================================================== */
typedef struct {
    v8 c29, c52, c66, c65, c53, c49, c79;          /* constants broadcast to both halves */
    v8 z29, z52, z66, z65, z53, z49, z79;          /* their zero lanes (speculative path) */
    u128 m54, m56, k81, k82, k83;                  /* the transpose's masks and controls */
    uint32_t b48, b34, b51, b50, o77, o78, o84, ns;
} g6a_inv;

/* the lift's 4x4 transpose of P_a..P_d into X Y Z (gen_edge_6ac8_body.py checks these expressions) */
G6A_AI void g6a_xpose(const g6a_inv* I, u128 Pa, u128 Pb, u128 Pc, u128 Pd, u128* X, u128* Y, u128* Z)
{
    const u128 c81 = spu_shufb(Pc, Pd, I->k81);
    const u128 m1 = spu_selb(c81, spu_selb(Pa, Pb, I->m56), I->m54);
    const u128 m2 = spu_shufb(c81, spu_selb(Pa, Pb, I->m54), I->k82);
    const u128 m3 = spu_shufb(spu_selb(Pb, Pa, I->m54), spu_selb(Pc, Pd, I->m54), I->k83);
    *X = spu_selb(m1, m2, I->m56); *Y = spu_selb(m3, m1, I->m56); *Z = spu_selb(m2, m3, I->m56);
}

/* one sphere step: the weight of sphere S/V on the 8 vertices X Y Z, added into acc (pairs vertex k with
 * k+4); spec: the raw native ops with one range check, returns 0 (acc untouched) if it failed */
G6A_AI int g6a_step(const g6a_inv* I, v8 X, v8 Y, v8 Z, v8 S, v8 V, v8* acc, int spec)
{
    g6a_rng R; R.mn = _mm256_set1_epi32(-1); R.mx = _mm256_setzero_si256();
    /* the ops on the step's outside inputs run checked in both modes (their own per-lane rule and
     * fallback: the positions carry non-plain lanes now and then), their results are range-checked as inputs */
    const v8 dx = g6a_fs(G6A_SPL(S, 0), X), dy = g6a_fs(G6A_SPL(S, 1), Y), dz = g6a_fs(G6A_SPL(S, 2), Z);
    const v8 Vs = g6a_fm(I->c53, V), Sw = G6A_SPL(S, 3);
    const v8 z0 = g6a_xin(spec, &R, dx), z1 = g6a_xin(spec, &R, dy), z2 = g6a_xin(spec, &R, dz);
    const v8 zVs = g6a_xin(spec, &R, Vs), zSw = g6a_xin(spec, &R, Sw);
    v8 zacc[4];
    for (int k = 0; k < 4; k++) zacc[k] = g6a_xin(spec, &R, acc[k]);
    v8 zD, zy, za, ze, zh, zr, zd, zx, zt, zt2, zq, zs, zs2, zw, zk[4];
    v8 D = g6a_xfm(spec, &R, dx, z0, dx, z0, &zD);
    D = g6a_xfma(spec, &R, dy, z1, dy, z1, D, zD, &zD);
    D = g6a_xfma(spec, &R, dz, z2, dz, z2, D, zD, &zD);
    const v8 y = g6a_fi(D, g6a_frsqest(D));
    zy = g6a_xin(spec, &R, y);
    const v8 a = g6a_xfm(spec, &R, y, zy, D, zD, &za);
    const v8 e = g6a_xfnms(spec, &R, a, za, y, zy, I->c29, I->z29, &ze);
    const v8 h = g6a_xfm(spec, &R, y, zy, I->c52, I->z52, &zh);
    const v8 r = g6a_xfma(spec, &R, e, ze, h, zh, y, zy, &zr);
    const v8 d = g6a_xfm(spec, &R, r, zr, D, zD, &zd);
    const v8 x = g6a_xfm(spec, &R, d, zd, Sw, zSw, &zx);
    const v8 gt = g6a_fcgt(x, I->c29);
    const v8 m = _mm256_blendv_epi8(x, I->c29, gt);   /* selb(x, 1.0, gt) */
    const v8 t = g6a_xfs(spec, &R, I->c29, m, &zt);
    const v8 t2 = g6a_xfm(spec, &R, t, zt, t, zt, &zt2);
    const v8 q = g6a_xfnms(spec, &R, I->c66, I->z66, t, zt, I->c65, I->z65, &zq);
    const v8 s = g6a_xfm(spec, &R, t2, zt2, q, zq, &zs);
    const v8 s2 = g6a_xfm(spec, &R, s, zs, s, zs, &zs2);
    const v8 w = g6a_xfm(spec, &R, s2, zs2, G6A_SPL(Vs, 3), G6A_SPL(zVs, 3), &zw);
    v8 o[4];
    o[0] = g6a_xfma(spec, &R, G6A_SPL(w, 0), G6A_SPL(zw, 0), Vs, zVs, acc[0], zacc[0], &zk[0]);
    o[1] = g6a_xfma(spec, &R, G6A_SPL(w, 1), G6A_SPL(zw, 1), Vs, zVs, acc[1], zacc[1], &zk[1]);
    o[2] = g6a_xfma(spec, &R, G6A_SPL(w, 2), G6A_SPL(zw, 2), Vs, zVs, acc[2], zacc[2], &zk[2]);
    o[3] = g6a_xfma(spec, &R, G6A_SPL(w, 3), G6A_SPL(zw, 3), Vs, zVs, acc[3], zacc[3], &zk[3]);
    if (spec && !g6a_rng_ok(&R)) return 0;
    for (int k = 0; k < 4; k++) acc[k] = o[k];
    return 1;
}

static long s6a_redo;          /* speculative sphere steps recomputed with the checked ops */

/* one outer iteration from the index group at a67: idx[8] and the 8 output quads as (out_k | out_k+4),
 * k = 0..3. spec: each sphere step runs speculatively and is recomputed with the checked ops if its range
 * check fails; the final fma always runs checked (its own per-lane rule and fallback). Returns the steps
 * kept speculative. */
G6A_AI int g6a_iter(const uint8_t* ls, uint32_t a67, const g6a_inv* I, uint32_t* idx, v8* out, int spec)
{
    for (int k = 0; k < 8; k++) idx[k] = g6a_w3(ls, a67 + 16u * k);
    u128 XL, YL, ZL, XH, YH, ZH;
    g6a_xpose(I, g6a_ld(ls, I->b48 + idx[0]), g6a_ld(ls, I->b48 + idx[1]), g6a_ld(ls, I->b48 + idx[2]),
              g6a_ld(ls, I->b48 + idx[3]), &XL, &YL, &ZL);
    g6a_xpose(I, g6a_ld(ls, I->b48 + idx[4]), g6a_ld(ls, I->b48 + idx[5]), g6a_ld(ls, I->b48 + idx[6]),
              g6a_ld(ls, I->b48 + idx[7]), &XH, &YH, &ZH);
    const v8 X = g6a_v8(XL, XH), Y = g6a_v8(YL, YH), Z = g6a_v8(ZL, ZH);
    v8 O[4], acc[4];
    for (int k = 0; k < 4; k++) {
        O[k] = g6a_ld2(ls, I->b34 + idx[k], I->b34 + idx[k + 4]);
        acc[k] = I->c79;
    }
    int kept = 0;
    for (uint32_t j = 0; j < I->ns; j++) {
        const uint32_t o = j == 0 ? I->o77 : j == 1 ? I->o78 : I->o84 + 16u * (j - 2);
        const v8 S = g6a_bc(g6a_ld(ls, I->b51 + o)), V = g6a_bc(g6a_ld(ls, I->b50 + o));
        if (spec && g6a_step(I, X, Y, Z, S, V, acc, 1)) { kept++; continue; }
        if (spec) s6a_redo++;
        g6a_step(I, X, Y, Z, S, V, acc, 0);
    }
    for (int k = 0; k < 4; k++) out[k] = g6a_fma(O[k], I->c49, acc[k]);
    return kept;
}

static int s6a_v2;             /* GOW_EDGE_6AC8_V2=1: the packed loop runs the checked ops on every iteration */
static long s6a_fb_short;      /* entries with fewer than 2 outer iterations (or an implausible count): v1 */
static long s6a_fb_ctl;        /* entries with other splat controls or an implausible sphere count: v1 */
static long s6a_fb_inv;        /* entries whose constants are not all plain: checked ops throughout */
static long s6a_pk_iters;      /* outer iterations run packed */
static long s6a_pk_spheres;    /* sphere steps run packed */

static int g6a_quad_is(u128 x, uint32_t a) { return x._u32[0] == a && x._u32[1] == a && x._u32[2] == a && x._u32[3] == a; }
static inline u128 g6a_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}
/* outer iterations from the entry registers: r68 += 8 until !(r80 > r68), signed, word 0 */
static uint32_t g6a_count(const u128* g)
{
    const int32_t lim = (int32_t)g[80]._u32[0];
    uint32_t k = g[68]._u32[0], n = 0;
    do { k += 8; n++; } while (lim > (int32_t)k && n <= 4096);
    return n;
}
static int g6a_plain_const(v8 c) { return g6a_all(g6a_plain(c)) && g6a_all(_mm256_or_si256(g6a_okr(c), g6a_zero(c))); }

static void g6a_pk(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    const uint32_t n = g6a_count(g);
    if (n < 2 || n > SPU_LS_SIZE / 128) { s6a_fb_short++; g6a_v1(ctx); return; }
    const uint32_t ns = g[85]._u32[0] != 0 ? g[64]._u32[0] : 0;   /* the inner loop runs r64 times */
    if (!g6a_quad_is(g[47], 0x00010203u) || !g6a_quad_is(g[41], 0x04050607u) ||
        !g6a_quad_is(g[39], 0x08090A0Bu) || !g6a_quad_is(g[35], 0x0C0D0E0Fu) || ns > 4096 ||
        (g[85]._u32[0] != 0 && ns == 0)) {
        s6a_fb_ctl++; g6a_v1(ctx); return;
    }
    g6a_inv I;
    I.c29 = g6a_bc(g[29]); I.c52 = g6a_bc(g[52]); I.c66 = g6a_bc(g[66]); I.c65 = g6a_bc(g[65]);
    I.c53 = g6a_bc(g[53]); I.c49 = g6a_bc(g[49]); I.c79 = g6a_bc(g[79]);
    int spec = !s6a_v2;
    const v8* cs[7] = { &I.c29, &I.c52, &I.c66, &I.c65, &I.c53, &I.c49, &I.c79 };
    v8* zs[7] = { &I.z29, &I.z52, &I.z66, &I.z65, &I.z53, &I.z49, &I.z79 };
    for (int i = 0; i < 7; i++) {
        *zs[i] = g6a_zero(*cs[i]);
        if (!g6a_plain_const(*cs[i])) { if (spec) s6a_fb_inv++; spec = 0; }
    }
    I.m54 = g[54]; I.m56 = g[56]; I.k81 = g[81]; I.k82 = g[82]; I.k83 = g[83];
    I.b48 = g[48]._u32[0]; I.b34 = g[34]._u32[0]; I.b51 = g[51]._u32[0]; I.b50 = g[50]._u32[0];
    I.o77 = g[77]._u32[0]; I.o78 = g[78]._u32[0]; I.o84 = g[84]._u32[0]; I.ns = ns;
    uint32_t a67 = g[67]._u32[0];
    for (uint32_t i = 0; i + 1 < n; i++) {
        uint32_t idx[8]; v8 o[4];
        g6a_iter(ls, a67, &I, idx, o, spec);
        /* the lift's store order: vertex 0 .. 7 */
        g6a_stlo(ls, I.b34 + idx[0], o[0]); g6a_stlo(ls, I.b34 + idx[1], o[1]);
        g6a_stlo(ls, I.b34 + idx[2], o[2]); g6a_stlo(ls, I.b34 + idx[3], o[3]);
        g6a_sthi(ls, I.b34 + idx[4], o[0]); g6a_sthi(ls, I.b34 + idx[5], o[1]);
        g6a_sthi(ls, I.b34 + idx[6], o[2]); g6a_sthi(ls, I.b34 + idx[7], o[3]);
        a67 += 128u;
    }
    s6a_pk_iters += (long)n - 1;
    s6a_pk_spheres += (long)(n - 1) * ns;
    /* the last outer iteration through the lifted blocks, from r67/r68 after iteration n - 2 */
    GOW_6A_REGS()
    GOW_6A_LOAD()
    r67 = g6a_addw(r67, 128u * (n - 1)); r68 = g6a_addw(r68, 8u * (n - 1));
    do {
        GOW_6A_A()
        if (r85._u32[0] != 0) {
            GOW_6A_PRE()
            do {
                GOW_6A_INNER()
            } while (r18._u32[0] == 0);
        }
        GOW_6A_TAIL()
    } while (r76._u32[0] != 0);
    GOW_6A_STORE()
}

/* ---- self-test at mode init: the 8-lane integer forms (frsqest, fi, fcgt) and the checked fm/fma/fnms/fs
 * (fallback included) against the 4-lane helpers, then whole outer iterations (speculative when kept, and
 * checked) against v1, i.e. the lifted statements, on a scratch context ---- */
static uint32_t s6a_rng = 0x2545F491u;
static uint32_t g6a_rand(void) { s6a_rng ^= s6a_rng << 13; s6a_rng ^= s6a_rng >> 17; s6a_rng ^= s6a_rng << 5; return s6a_rng; }
static uint32_t g6a_rword(int it)
{
    uint32_t x = g6a_rand();
    switch (g6a_rand() % 23) {
    case 0: x = 0; break;
    case 1: x &= 0x807FFFFFu; break;                     /* exponent 0 */
    case 2: x |= 0x7F800000u; break;                     /* exponent 255 */
    case 3: x = 0x7F000000u | (x & 0x807FFFFFu); break;  /* exponent 254 */
    default: if (it & 1) x = (x & 0x83FFFFFFu) | 0x3C000000u; break;   /* moderate exponents */
    }
    return x;
}
static uint32_t g6a_mword(void) { return (g6a_rand() & 0x80FFFFFFu) | 0x3E000000u; }   /* ordinary, |x| ~ 0.125..2 */
static int g6a_eq(u128 a, u128 b) { return !memcmp(&a, &b, 16); }
static u128 g6a_splat(uint32_t a) { u128 r; for (int i = 0; i < 4; i++) r._u32[i] = a; return r; }
static void g6a_selftest(void)
{
    long bad = 0, bad2 = 0, bad_it = 0, kept = 0, iters = 0, steps = 0;
    const long slow0 = s6a_fp_slow, redo0 = s6a_redo;   /* the self-test's own counts are not the job's */
    for (int it = 0; it < 20000; it++) {
        u128 a[2], b[2], c[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                a[h]._u32[i] = g6a_rword(it); b[h]._u32[i] = g6a_rword(it); c[h]._u32[i] = g6a_rword(it);
                if ((it & 15) == 6) { c[h]._u32[i] = a[h]._u32[i] ^ 0x80000000u; b[h]._u32[i] = 0x3F800000u; }  /* cancelling */
                if ((it & 15) == 7) b[h]._u32[i] = a[h]._u32[i];                                                /* equal (fcgt, fs) */
                if ((it & 15) == 8) a[h]._u32[i] = (a[h]._u32[i] & 0x80FFFFFFu) | 0x3F000000u;                  /* near 1.0 */
            }
        const v8 A = g6a_v8(a[0], a[1]), B = g6a_v8(b[0], b[1]), C = g6a_v8(c[0], c[1]);
        const v8 rs = g6a_frsqest(A), fi = g6a_fi(A, B), gt = g6a_fcgt(A, B), fi2 = g6a_fi(A, rs);
        const v8 xm = g6a_fm(A, B), xs = g6a_fs(A, C), xn = g6a_fnms(A, B, C), xf = g6a_fma(A, C, B);
        for (int h = 0; h < 2; h++) {
            bad += !g6a_eq(spu_frsqest(a[h]), h ? g6a_hi(rs) : g6a_lo(rs));
            bad += !g6a_eq(spu_fi(a[h], b[h]), h ? g6a_hi(fi) : g6a_lo(fi));
            bad += !g6a_eq(spu_fcgt(a[h], b[h]), h ? g6a_hi(gt) : g6a_lo(gt));
            bad += !g6a_eq(spu_fi(a[h], spu_frsqest(a[h])), h ? g6a_hi(fi2) : g6a_lo(fi2));
            bad2 += !g6a_eq(spu_fm(a[h], b[h]), h ? g6a_hi(xm) : g6a_lo(xm));
            bad2 += !g6a_eq(spu_fs(a[h], c[h]), h ? g6a_hi(xs) : g6a_lo(xs));
            bad2 += !g6a_eq(spu_fnms(a[h], b[h], c[h]), h ? g6a_hi(xn) : g6a_lo(xn));
            bad2 += !g6a_eq(spu_fma(a[h], c[h], b[h]), h ? g6a_hi(xf) : g6a_lo(xf));
        }
    }
    /* whole outer iterations: one group of 8 vertices on a scratch context; v1 (n = 1) is the reference */
    spu_context* ctx = (spu_context*)calloc(1, sizeof(spu_context));
    uint8_t* pre = (uint8_t*)malloc(SPU_LS_SIZE);
    for (int it = 0; ctx && pre && it < 2000; it++) {
        ctx->ls = ctx->ls_store;
        uint8_t* ls = ctx->ls;
        u128* g = ctx->gpr;
        memset(g, 0, sizeof(ctx->gpr));
        const int mod = it & 3;   /* 0: specials mixed in, else ordinary values */
        const uint32_t b67 = 0x1000, b48 = 0x8000, b34 = 0x10000, b51 = 0x20000, b50 = 0x21000;
        for (int k = 0; k < 8; k++) {   /* index quads: word 3 the offset (16 x 0..15, so duplicates occur) */
            u128 q; for (int i = 0; i < 4; i++) q._u32[i] = g6a_rand();
            q._u32[3] = 16u * (g6a_rand() % ((it & 4) ? 16 : 64));
            g6a_st(ls, b67 + 16u * k, q);
        }
        for (int k = 0; k < 64; k++) {
            u128 p, o; for (int i = 0; i < 4; i++) { p._u32[i] = mod ? g6a_mword() : g6a_rword(it); o._u32[i] = mod ? g6a_mword() : g6a_rword(it); }
            g6a_st(ls, b48 + 16u * k, p); g6a_st(ls, b34 + 16u * k, o);
        }
        for (int k = 0; k < 8; k++) {
            u128 s, v; for (int i = 0; i < 4; i++) { s._u32[i] = mod ? g6a_mword() : g6a_rword(it); v._u32[i] = mod ? g6a_mword() : g6a_rword(it); }
            if (mod == 2) s._u32[3] = 0x3F000000u | (g6a_rand() & 0x7FFFFF);   /* radius scale near 0.5..1 */
            g6a_st(ls, b51 + 16u * k, s); g6a_st(ls, b50 + 16u * k, v);
        }
        const uint32_t ns = it % 4;   /* 0: the inner loop skipped */
        const uint32_t j0 = g6a_rand() % 4;
        g[29] = g6a_splat(0x3F800000u); g[52] = g6a_splat(0x3F000000u); g[66] = g6a_splat(0x40000000u);
        g[65] = g6a_splat(0x40400000u);
        g[53] = g6a_splat(mod ? g6a_mword() : g6a_rword(it)); g[49] = g6a_splat((it & 8) ? 0x3F800000u : 0);
        g[79] = g6a_splat((it & 16) ? 0x3F000000u : 0);
        g[47] = g6a_splat(0x00010203u); g[41] = g6a_splat(0x04050607u); g[39] = g6a_splat(0x08090A0Bu);
        g[35] = g6a_splat(0x0C0D0E0Fu);
        for (int i = 0; i < 4; i++) {   /* the masks: fsmbi(0xFF00) and fsmbi(0x0FF0); the controls random */
            g[54]._u32[i] = i < 2 ? 0xFFFFFFFFu : 0; g[56]._u32[i] = (i == 1 || i == 2) ? 0xFFFFFFFFu : 0;
            /* the controls: the ones the job keeps at LS 0xF870/0xF880/0xF890 (cap_3), other whole-word picks,
             * or random bytes (any shufb control) */
            static const uint32_t k8x[3][4] = { { 0x00010203u, 0x18191A1Bu, 0x04050607u, 0x10111213u },
                                                 { 0x18191A1Bu, 0x10111213u, 0x00010203u, 0x04050607u },
                                                 { 0x04050607u, 0x08090A0Bu, 0x18191A1Bu, 0x14151617u } };
            u128* kr[3] = { &g[81], &g[82], &g[83] };
            for (int c = 0; c < 3; c++)
                kr[c]->_u32[i] = (it & 0x60) == 0x60 ? (g6a_rand() & 0x9F9F9F9Fu)
                               : (it & 0x20) ? 0x04040404u * (g6a_rand() % 8) + 0x00010203u : k8x[c][i];
        }
        g[48] = g6a_splat(b48); g[34] = g6a_splat(b34); g[51] = g6a_splat(b51); g[50] = g6a_splat(b50);
        g[77] = g6a_splat(16u * j0); g[78] = g6a_splat(16u * j0 + 16u); g[84] = g6a_splat(16u * j0 + 32u);
        g[64] = g6a_splat(ns); g[85] = g6a_splat(ns ? 0xFFFFFFFFu : 0);
        g[67] = g6a_splat(b67); g[68] = g6a_splat(0); g[80] = g6a_splat(8);
        g6a_inv I;
        I.c29 = g6a_bc(g[29]); I.c52 = g6a_bc(g[52]); I.c66 = g6a_bc(g[66]); I.c65 = g6a_bc(g[65]);
        I.c53 = g6a_bc(g[53]); I.c49 = g6a_bc(g[49]); I.c79 = g6a_bc(g[79]);
        int cplain = 1;
        const v8* cs[7] = { &I.c29, &I.c52, &I.c66, &I.c65, &I.c53, &I.c49, &I.c79 };
        v8* zs[7] = { &I.z29, &I.z52, &I.z66, &I.z65, &I.z53, &I.z49, &I.z79 };
        for (int i = 0; i < 7; i++) { *zs[i] = g6a_zero(*cs[i]); cplain &= g6a_plain_const(*cs[i]); }
        I.m54 = g[54]; I.m56 = g[56]; I.k81 = g[81]; I.k82 = g[82]; I.k83 = g[83];
        I.b48 = b48; I.b34 = b34; I.b51 = b51; I.b50 = b50;
        I.o77 = 16u * j0; I.o78 = 16u * j0 + 16u; I.o84 = 16u * j0 + 32u; I.ns = ns;
        memcpy(pre, ls, SPU_LS_SIZE);
        g6a_v1(ctx);                                   /* reference: the lifted statements */
        for (int spec = cplain; spec >= 0; spec--) {
            uint32_t idx[8]; v8 o[4];
            uint8_t tmp[0x400];                        /* the output table (64 quads) of the pre-state */
            memcpy(tmp, pre + b34, sizeof(tmp));
            kept += g6a_iter(pre, b67, &I, idx, o, spec); iters++; steps += ns;
            /* the packed stores into tmp, in the lift's order */
            for (int k = 0; k < 8; k++) {
                const v8 q = o[k & 3];
                __m128i x = k < 4 ? _mm256_castsi256_si128(q) : _mm256_extracti128_si256(q, 1);
                _mm_storeu_si128((__m128i*)(tmp + (idx[k] & 0x3F0u)), g6a_bswap32x4(x));
            }
            bad_it += memcmp(tmp, ls + b34, sizeof(tmp)) != 0;
        }
    }
    free(pre); free(ctx);
    const long slow = s6a_fp_slow - slow0, redo = s6a_redo - redo0;
    s6a_fp_slow = slow0; s6a_redo = redo0;
    fprintf(stderr, "[edge-6ac8] self-test against the 4-lane helpers: frsqest/fi/fcgt x8 %ld of 160000 differ; checked "
                    "fm/fs/fnms/fma x8 %ld of 160000 differ (%ld 8-lane ops took the 4-lane fallback); whole outer iterations "
                    "against v1 %ld of %ld differ (%ld of %ld sphere steps kept speculative, %ld recomputed)\n", bad, bad2,
            slow, bad_it, iters, kept, steps, redo);
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s6a_mode = -1;

int gow_edge_6ac8_mode(void)
{
    if (s6a_mode < 0) {
        const char* e = getenv("GOW_EDGE_6AC8");
        int m = e && *e ? atoi(e) : 1;   /* default: native packed v3 */
        if (m < 0 || m > 4) m = 1;
        const char* v = getenv("GOW_EDGE_6AC8_V2");
        s6a_v2 = v && atoi(v) == 1;
        s6a_mode = m;
        if (!m) fprintf(stderr, "[edge-6ac8] GOW_EDGE_6AC8=0 (lifted loop nest)\n");
        else {
            fprintf(stderr, "[edge-6ac8] GOW_EDGE_6AC8=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : s6a_v2 ? "packed v2" : "packed v3");
            g6a_selftest();
        }
    }
    return s6a_mode;
}

static int s6a_fault = -1;
static long s6a_fault_at, s6a_runs;
static void g6a_fault(spu_context* ctx)
{
    if (s6a_fault < 0) {
        const char* f = getenv("GOW_EDGE_6AC8_FAULT");
        s6a_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s6a_fault_at = k ? atol(k + 1) : 0;
    }
    if (s6a_fault <= 0 || s6a_runs++ != s6a_fault_at) return;
    const u128* r = ctx->gpr;
    switch (s6a_fault) {
    case 1: ctx->ls[((r[34]._u32[0] + r[59]._u32[0]) & (SPU_LS_MASK & ~0xFu)) + 15] ^= 1; break;  /* ST(r34 + r59, r33) */
    case 2: ctx->gpr[33]._u32[0] ^= 1; break;
    default: break;
    }
    fprintf(stderr, "[edge-6ac8] fault %d injected (native run %ld)\n", s6a_fault, s6a_fault_at);
    s6a_fault = 0;
}

static void g6a_run(spu_context* ctx)
{
    if (s6a_mode == 2 || s6a_mode == 4) g6a_v1(ctx);
    else                                g6a_pk(ctx);
    if (__builtin_expect(s6a_fault != 0, 0)) g6a_fault(ctx);
}

/* Modes 1 and 2: replace the loop nest; the lift then jumps to its exit. */
void gow_edge_6ac8_6be0(spu_context* ctx) { g6a_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop nest on the live state, keep its result, put
 * the state back and let the lifted loop nest run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} g6a_check_state;
static _Thread_local g6a_check_state* t6a_chk;

static long s6a_calls, s6a_bad, s6a_iters, s6a_spheres;
static uint64_t s6a_cyc_nat, s6a_cyc_lift;
static int s6a_atexit;

static void g6a_check_report(void)
{
    fprintf(stderr, "[edge-6ac8-check] %ld calls checked, %ld differ; %ld outer iterations (8 vertices each), %ld sphere "
                    "steps; tsc: native %.1f M, lifted %.1f M (%.1f vs %.1f per outer iteration)\n",
            s6a_calls, s6a_bad, s6a_iters, s6a_spheres, s6a_cyc_nat / 1e6, s6a_cyc_lift / 1e6,
            s6a_iters ? (double)s6a_cyc_nat / (double)s6a_iters : 0.0,
            s6a_iters ? (double)s6a_cyc_lift / (double)s6a_iters : 0.0);
    fprintf(stderr, "[edge-6ac8-check] packed %ld outer iterations, %ld sphere steps; entries left to v1 (short): %ld, "
                    "(controls): %ld; entries on the checked ops (constants not plain): %ld; speculative sphere steps "
                    "recomputed: %ld; 8-lane ops redone by the 4-lane helpers: %ld\n", s6a_pk_iters, s6a_pk_spheres,
            s6a_fb_short, s6a_fb_ctl, s6a_fb_inv, s6a_redo, s6a_fp_slow);
}

void gow_edge_6ac8_check_begin(spu_context* ctx)
{
    g6a_check_state* c = t6a_chk;
    if (!c) {
        c = (g6a_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t6a_chk = c;
    }
    if (!s6a_atexit) { s6a_atexit = 1; atexit(g6a_check_report); }
    const uint32_t n = g6a_count(ctx->gpr);
    s6a_iters += (long)n;
    if (ctx->gpr[85]._u32[0] != 0) s6a_spheres += (long)n * (long)ctx->gpr[64]._u32[0];
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    g6a_run(ctx);
    s6a_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_6ac8_check_end(spu_context* ctx)
{
    g6a_check_state* c = t6a_chk;
    if (!c || !c->pending) return;
    s6a_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s6a_calls++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-6ac8-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-6ac8-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s6a_bad++;
        fprintf(stderr, "[edge-6ac8-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) g6a_check_report();   /* a game run ends by kill: atexit never runs */
}

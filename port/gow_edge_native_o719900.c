/* gow_edge_native_o719900.c -- native replacement for the loop of EDGE overlay blob 719900 (output packing,
 * family B in work/edgefrag_scope.md): the loop at blob+0x600, in every placement lift of the blob
 * (spu/edgefrag_719900_<lsa>; 37 placements, one lift each). gen_edge_frag_lift.py puts a switch before the
 * loop in each of them (checking that each placement's loop is the reference 11340's, statement for
 * statement); GOW_EDGE_O719900 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native_o11430.c (-O2 -march=x86-64-v3 -ffp-contract=off,
 * SPU_FP_NATIVE=1, SPU_FP_CHECK_HOOK=0): the float helpers (fma, cflts) are the same inline helpers the lift
 * uses, on the same operands in the same order, so they round the same way. Runs inside spu_run_with_halt,
 * i.e. with MXCSR rounding toward zero and FTZ/DAZ, as the lift does.
 *
 * Clean-room: written from our own lift of the fragment and our own captures.
 *
 * What the loop does (a reading of the lift; 2 vertices per iteration, 108 output bytes, r41 a negative byte
 * offset stepping by 108 to 0, r2 = r41 + 108 the back-edge condition):
 *   - seven input slot arrays (float4 per vertex; r49 r47 r45 r42 r40 r46 r54, +32 each per iteration),
 *     loaded one iteration ahead (software pipelined);
 *   - r49's: fma(x, r84, r94) (scale and bias) -> cflts 157 (x 2^16, truncated, saturated) -> the high
 *     halfwords picked by shufb r85/r115: s16 normalized;
 *   - r47's: fma(x, r106, r100) (scale/bias quads from the blob's pool) -> cflts 142 -> rotm by r93: an
 *     unsigned field of a width set by the prologue;
 *   - r45 r42 r40's: float -> half: sf(r63, x) (exponent rebias 127 -> 15), rotqbii 3, high halfwords by
 *     shufb r64 (mantissa truncated, i.e. toward zero); |x| (andc r60, the sign, on x's high halfword)
 *     clgth r70 -> r69 (clamp to the largest half), sign put back by selb r60, and |x|'s high halfword
 *     below r68 -> 0 (no half denormals: flushed);
 *   - r46's passed through;
 *   - r54's passed through (selb with r117 into the stream);
 *   - the fields are merged into a byte stream by shufb controls the prologue loaded, rotated to the output
 *     alignment (control r75 = r71 - splat(r19 & 15)) and stored as eight aligned quads at r34 + {r91 r90
 *     r89 r88 r87 r86 r102 r44}, the partial quad read back (r92 + r34) and merged.
 *
 * v1: the loop statement for statement in C locals (GOW_O719_BODY, generated from the reference lift at
 * build time), as one C loop (the lift hops through five functions per iteration), with direct LS access,
 * the rotqbii by a word shift pair, and the loop-invariant shufb controls left to the compiler to hoist.
 * Every op is the lift's op on the same operands in the same order, so it is exact for every input.
 *
 * v3 (packed): the 108 bytes of a record computed straight from the inputs, one record (2 vertices, 8 lanes)
 * per step. What the lift computes, read by evaluating its body over three iterations with the controls
 * of the captures (all the same in every call): iteration k stores record k at S_k = r103 + r41 + 108k (the
 * entry r41), contiguous (the merge keeps the bytes before S_k, the bytes after the record are rewritten by
 * the next one), and record k for k >= 3 is the input pair at P + 32(k - 3) of each array (the pipeline is
 * three iterations deep; records 0-2 come from what the prologue loaded). Per vertex (54 bytes):
 *   r54 bytes 0-11 | s16(r49) x y z w | pk(r47) | r46 bytes 0-11 | half(r45) x y z | half(r42) x y z |
 *   half(r40) x y z
 * with s16 = cflts(fma(x, r84, r94), 157) >> 16, pk = (Z's high halfword : X's high halfword) & ~r101 |
 * (Y >> 8) & r101 with X Y Z = cflts(fma(.., r106, r100), 142) >> by r93, and half(x) = 0 if |x|hi < r68,
 * else (|x|hi < r70 ? (x - r63) << 3 >> 16 : r69) without r60's bit, | x's sign (r60's bit).
 * Integer work is exact by construction; fma is the native op with the helper's lane rule (a pair with a
 * failing lane is redone by the helper), cflts the helper's integer formula 8 lanes wide. The carried
 * registers depend only on the last three iterations' loads (and r32's bytes 12-15, which the loop only
 * carries along), so the end state is rebuilt by the lifted body: iterations 0-2 run lifted from the
 * entry state (with their stores), records 3 .. n-2 packed, then iterations n-4 .. n-2 again lifted with
 * the pointers set back and no stores (the state converges), and the last iteration lifted with its
 * stores: all 71 registers the loop writes and the trailing bytes it leaves come out exactly. Entry checks,
 * else v1 (each counted): 8 .. 8192 iterations (r41 a negative multiple of 108); the controls and masks
 * equal the captures' (the layout above rests on them; r84 r94 r106 r100 r93 are used as they are); the
 * store pointers r86..r92 r102 r44 at r103 + 16..112 / 108 as the prologue sets them; the input pointers
 * 16-byte aligned; inputs and output inside the LS without wrapping and apart from each other.
 *
 * GOW_EDGE_O719900 (read once):
 *   unset/0  lifted loop (the default)
 *   1        native v3 (v1 for the entries it does not take)
 *   2        native v1
 *   3        check v3: run it on a copy of the state, then the lifted loop, and compare all GPRs and the
 *            whole LS at the loop exit; `[edge-o719900-check] N calls checked, M differ` every 4096 calls
 *            and at exit, with the calls per placement
 *   4        check v1, the same way
 * GOW_EDGE_O719900_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 3 of the quad the last iteration stores at r91 + r34, 3 bit 0 of byte 0 of that quad, 2 bit 0 of r31
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
static inline __m128i go79_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 go79_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = go79_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void go79_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), go79_bswap32x4(x));
}
#define LD(a)    go79_ld(ls, (a))
#define ST(a, v) go79_st(ls, (a), (v))

/* rotqbii: the quad rotated left by sh bits (SPU word 0 the most significant). Word i of the result is
 * (w[i] << sh) | (w[i+1 mod 4] >> (32 - sh)), the same bits as spu_helpers' 64-bit pair version. */
static inline u128 go79_rotqbii(u128 a, int sh)
{
    sh &= 7;
    if (!sh) return a;
    __m128i x; memcpy(&x, &a, 16);
    const __m128i n = _mm_shuffle_epi32(x, _MM_SHUFFLE(0, 3, 2, 1));   /* word i <- word i+1 */
    const __m128i r = _mm_or_si128(_mm_sll_epi32(x, _mm_cvtsi32_si128(sh)), _mm_srl_epi32(n, _mm_cvtsi32_si128(32 - sh)));
    u128 o; memcpy(&o, &r, 16); return o;
}

/* the lifted loop: GOW_O719_*. Game code, so it is not in the repo: gen_edge_frag_lift.py --body writes it
 * from the reference lift at build time (no LS address of its own, straight code: checked there). */
#define spu_rotqbii go79_rotqbii
#include "gow_edge_o719900_body.h"

/* v1: the whole loop (back edge on r2) */
static void go79_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O719_REGS()
    GOW_O719_LOAD()
    do {
        GOW_O719_BODY()
    } while (r2._u32[0] != 0);
    GOW_O719_STORE()
}

/* the lifted body for `count` iterations, the first `nost` of them without their stores (v3: the first
 * three iterations with their stores; the warm-up, its registers only (the loads still read the LS), and
 * the last iteration with its stores). One copy of the body for both, so the second call finds it cached. */
#undef ST
#define ST(a, v) do { if (i >= nost) go79_st(ls, (a), (v)); } while (0)
static void go79_iters(spu_context* ctx, uint32_t count, uint32_t nost)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O719_REGS()
    GOW_O719_LOAD()
    for (uint32_t i = 0; i < count; i++) {
        GOW_O719_BODY()
    }
    GOW_O719_STORE()
}
#undef ST
#define ST(a, v) go79_st(ls, (a), (v))
#undef spu_rotqbii

/* =====================================================================================================
 * v3: one record (the iteration's two vertices: vertex A in the low 128-bit lane, B in the high) per step.
 * ===================================================================================================== */
typedef __m256i v8;
#define GO79_AI static inline __attribute__((always_inline))

static inline v8 go79_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 go79_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(a); }
static inline u128 go79_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 go79_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline __m256 go79_p(v8 x) { return _mm256_castsi256_ps(x); }
static inline v8 go79_f(__m256 x) { return _mm256_castps_si256(x); }
/* 32 bytes of LS in memory order, and as host words (the pair's two quads) */
GO79_AI v8 go79_raw(const uint8_t* ls, uint32_t a) { return _mm256_loadu_si256((const __m256i*)(ls + a)); }
GO79_AI v8 go79_bsw(v8 x)
{
    return _mm256_shuffle_epi8(x, _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                                   3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}

/* fma: the native op, kept when every lane passes the SPU_FP_NATIVE helper's rule (SPU_FP_DAZ: the
 * result's exponent 1..253, or a zero; spu_helpers.h spu__xn_fma). A zero keeps its sign here (the helper
 * makes it +0), which cflts, the only reader, maps to 0 either way. Otherwise the pair is redone by the
 * helper itself. */
static long s79_fma_slow;   /* records whose fma went to the helper (lab statistic; racy across threads) */
static inline v8 go79_fma_slow(v8 a, v8 b, v8 c)
{
    return go79_v8(spu_fma(go79_lo(a), go79_lo(b), go79_lo(c)), spu_fma(go79_hi(a), go79_hi(b), go79_hi(c)));
}
GO79_AI v8 go79_fma_ok(v8 r)
{
    const v8 e = _mm256_srli_epi32(_mm256_slli_epi32(r, 1), 24);
    const v8 n = _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
    return _mm256_or_si256(n, _mm256_cmpeq_epi32(_mm256_slli_epi32(r, 1), _mm256_setzero_si256()));
}

/* cflts, 8 lanes: the SPU_FP_NATIVE helper's integer formula (spu__xe_cfl, signed) as it is */
GO79_AI v8 go79_cflts(v8 x, int i8)
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
GO79_AI v8 go79_cflts_ok(v8 x, float scale)
{
    const __m256 s = _mm256_mul_ps(go79_p(x), _mm256_set1_ps(scale));
    return _mm256_xor_si256(_mm256_cvttps_epi32(s), go79_f(_mm256_cmp_ps(s, _mm256_set1_ps(2147483648.0f), _CMP_GE_OQ)));
}

/* float -> half per word (the lift's sf, rotqbii 3 + high halfword, andc, clgth, selb, selb, andc) */
typedef struct go79_inv_ {
    v8 c84, c94, c106, c100, n93;   /* fma operands (broadcast) and the rotm counts (0 - r93) & 63 */
    v8 s60, a60, c63, t68, t70, c69, m101, k101;   /* the half's and the packed word's operands, from the
                                                    * registers (halfword values in 32-bit lanes) */
    uint32_t p49, p47, p45, p42, p40, p46, p54;
} go79_inv;

/* float -> half per word (the lift's sf, rotqbii 3 + high halfword, andc, clgth, selb, selb, andc), with
 * the halfword operands as checked at entry (all halfwords of each register equal) */
GO79_AI v8 go79_half(const go79_inv* I, v8 x)
{
    const v8 xhi = _mm256_srli_epi32(x, 16);
    const v8 ax = _mm256_and_si256(xhi, I->a60);
    const v8 h = _mm256_srli_epi32(_mm256_slli_epi32(_mm256_sub_epi32(x, I->c63), 3), 16);
    v8 r = _mm256_blendv_epi8(I->c69, h, _mm256_cmpgt_epi32(I->t70, ax));
    r = _mm256_or_si256(_mm256_and_si256(r, I->a60), _mm256_and_si256(xhi, I->s60));
    return _mm256_andnot_si256(_mm256_cmpgt_epi32(I->t68, ax), r);
}

/* the byte shuffles that place the fields (per lane; 0x80 = zero) */
#define GO79_M(...) _mm256_setr_epi8(__VA_ARGS__, __VA_ARGS__)
#define Z8 -128,-128,-128,-128,-128,-128,-128,-128

/* record k = input pair j = k - 3, written at s (its 108 bytes, then up to 10 bytes the next record or the
 * lifted last iteration rewrites). slow = 0: the native fma, and 0 returned, before anything is stored, if
 * a lane fails the rule; slow = 1: the helper's fma (go79_record_slow, out of the hot loop). */
GO79_AI int go79_record(uint8_t* ls, const go79_inv* I, uint32_t j, uint32_t s, int slow)
{
    const uint32_t o = 32u * j;
    const v8 x49 = go79_bsw(go79_raw(ls, I->p49 + o)), x47 = go79_bsw(go79_raw(ls, I->p47 + o));
    v8 f49, f47;
    if (slow) {
        f49 = go79_fma_slow(I->c84, x49, I->c94);
        f47 = go79_fma_slow(I->c106, x47, I->c100);
    } else {
        f49 = go79_f(_mm256_fmadd_ps(go79_p(I->c84), go79_p(x49), go79_p(I->c94)));
        f47 = go79_f(_mm256_fmadd_ps(go79_p(I->c106), go79_p(x47), go79_p(I->c100)));
        if (__builtin_expect(_mm256_movemask_ps(go79_p(_mm256_and_si256(go79_fma_ok(f49), go79_fma_ok(f47)))) != 0xFF, 0))
            return 0;
    }
    const v8 x45 = go79_bsw(go79_raw(ls, I->p45 + o)), x42 = go79_bsw(go79_raw(ls, I->p42 + o));
    const v8 x40 = go79_bsw(go79_raw(ls, I->p40 + o));
    const v8 r46 = go79_raw(ls, I->p46 + o), r54 = go79_raw(ls, I->p54 + o);
    const v8 c49 = slow ? go79_cflts(f49, 157) : go79_cflts_ok(f49, 65536.0f);
    const v8 c47 = slow ? go79_cflts(f47, 142) : go79_cflts_ok(f47, 2147483648.0f);
    const v8 s16 = _mm256_srli_epi32(c49, 16);
    const v8 q = _mm256_srlv_epi32(c47, I->n93);                              /* rotm (>31: 0) */
    const v8 X = _mm256_shuffle_epi32(q, 0x00), Y = _mm256_shuffle_epi32(q, 0x55), Z = _mm256_shuffle_epi32(q, 0xAA);
    const v8 K = _mm256_or_si256(
        _mm256_and_si256(_mm256_or_si256(_mm256_and_si256(Z, _mm256_set1_epi32((int)0xFFFF0000u)), _mm256_srli_epi32(X, 16)),
                         I->k101),
        _mm256_and_si256(_mm256_srli_epi32(Y, 8), I->m101));
    const v8 U = _mm256_packus_epi32(s16, go79_half(I, x45));                  /* s16 x y z w | h45 x y z w */
    const v8 V = _mm256_packus_epi32(go79_half(I, x42), go79_half(I, x40));       /* h42 x y z w | h40 x y z w */
    const v8 c0 = _mm256_blend_epi32(r54, _mm256_shuffle_epi8(U, GO79_M(Z8, -128,-128,-128,-128, 1,0,3,2)), 0x88);
    const v8 c1 = _mm256_or_si256(_mm256_or_si256(
                      _mm256_shuffle_epi8(U, GO79_M(5,4,7,6, -128,-128,-128,-128, Z8)),
                      _mm256_shuffle_epi8(K, GO79_M(-128,-128,-128,-128, 3,2,1,0, Z8))),
                      _mm256_bslli_epi128(r46, 8));
    const v8 c2 = _mm256_or_si256(_mm256_or_si256(
                      _mm256_shuffle_epi8(r46, GO79_M(8,9,10,11, -128,-128,-128,-128, Z8)),
                      _mm256_shuffle_epi8(U, GO79_M(-128,-128,-128,-128, 9,8,11,10, 13,12,-128,-128, -128,-128,-128,-128))),
                      _mm256_shuffle_epi8(V, GO79_M(Z8, -128,-128, 1,0, 3,2, 5,4)));
    const v8 c3 = _mm256_shuffle_epi8(V, GO79_M(9,8,11,10, 13,12,-128,-128, Z8));
    uint8_t* const d = ls + s;
    _mm_storeu_si128((__m128i*)(d + 0), _mm256_castsi256_si128(c0));
    _mm_storeu_si128((__m128i*)(d + 16), _mm256_castsi256_si128(c1));
    _mm_storeu_si128((__m128i*)(d + 32), _mm256_castsi256_si128(c2));
    _mm_storeu_si128((__m128i*)(d + 48), _mm256_castsi256_si128(c3));
    _mm_storeu_si128((__m128i*)(d + 54), _mm256_extracti128_si256(c0, 1));
    _mm_storeu_si128((__m128i*)(d + 70), _mm256_extracti128_si256(c1, 1));
    _mm_storeu_si128((__m128i*)(d + 86), _mm256_extracti128_si256(c2, 1));
    _mm_storeu_si128((__m128i*)(d + 102), _mm256_extracti128_si256(c3, 1));
    return 1;
}
static __attribute__((noinline, cold)) void go79_record_slow(uint8_t* ls, const go79_inv* I, uint32_t j, uint32_t s)
{
    s79_fma_slow++;
    go79_record(ls, I, j, s, 1);
}
#undef GO79_M
#undef Z8

/* the controls and masks the layout rests on: GOW_O719_CTL, {register, its four SPU words} as the prologue
 * leaves them (the same in every capture call). Game data, so not in the repo: gen_edge_frag_lift.py
 * --consts computes them at build time from the reference lift's prologue and the blob's bytes in the
 * EBOOT. They are: r60 0x8000 halfword masks (sign, |x|); r63 the half exponent rebias; r68 r70 r69 the
 * half thresholds and clamp (0x3800, 0x4780, 0x7BFF); r64 the high-halfword picker; r71 the output
 * rotation base and r113 the splat of its offset; r85 r115 the packed field's byte pickers and r101 its
 * bit mask; r67 r95-r99 r104 r105 r107-r112 the shufb controls that merge the fields into the stream;
 * r114 r116 r117 byte masks. */
#include "gow_edge_o719900_consts.h"
static const struct { uint8_t r; uint32_t w[4]; } go79_ctl[GOW_O719_NCTL] = GOW_O719_CTL;
#define GO79_NCTL GOW_O719_NCTL
/* the carried pointers: the seven input arrays (+32 per iteration) and r41 (+108; r34 follows it) */
static const uint8_t go79_ptr[7] = { 49, 47, 45, 42, 40, 46, 54 };
/* the store pointers: r103 + this (word 0) */
static const struct { uint8_t r; uint32_t d; } go79_out[] = {
    { 102, 0 }, { 86, 16 }, { 87, 32 }, { 88, 48 }, { 89, 64 }, { 90, 80 }, { 91, 96 }, { 92, 108 }, { 44, 112 },
};

#define GO79_NMIN 8u
#define GO79_NMAX 8192u
static long s79_fb_short;    /* entries with fewer than 8 iterations (or an implausible count): v1 */
static long s79_fb_ctl;      /* entries with other controls, masks or store pointers: v1 */
static long s79_fb_rng;      /* entries with inputs or output outside the LS or unaligned input pointers: v1 */
static long s79_fb_ovl;      /* entries whose output overlaps an input: v1 */
static long s79_pk_recs;     /* records written packed */
static long s79_v3_calls;    /* entries v3 took */
static uint64_t s79_cyc_pk;  /* tsc in the packed records (lab statistic) */
static uint32_t s79_last_s3; /* LS address of the last packed call's record 3 (fault 4), or ~0 */

static inline u128 go79_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}

static void go79_v3(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    s79_last_s3 = ~0u;
    const uint32_t w41 = g[41]._u32[0];
    const uint32_t nb = 0u - w41, n = nb / 108u;
    if (!w41 || nb % 108u || n < GO79_NMIN || n > GO79_NMAX) { s79_fb_short++; go79_v1(ctx); return; }
    for (int i = 0; i < GO79_NCTL; i++)
        if (memcmp(g[go79_ctl[i].r]._u32, go79_ctl[i].w, 16)) { s79_fb_ctl++; go79_v1(ctx); return; }
    const uint32_t o = g[103]._u32[0];
    for (int i = 0; i < (int)(sizeof(go79_out) / sizeof(go79_out[0])); i++)
        if (g[go79_out[i].r]._u32[0] != o + go79_out[i].d) { s79_fb_ctl++; go79_v1(ctx); return; }
    /* output: the stores of all n iterations, from the quad of S_0 to the quad after S_{n-1} + 112 */
    const uint32_t s0 = o + w41;
    const uint64_t olo = s0 & ~0xFu, ohi = (uint64_t)s0 + 108u * (uint64_t)n + 32u;
    if (s0 >= SPU_LS_SIZE || ohi > SPU_LS_SIZE) { s79_fb_rng++; go79_v1(ctx); return; }
    go79_inv I;
    uint32_t* const pp[7] = { &I.p49, &I.p47, &I.p45, &I.p42, &I.p40, &I.p46, &I.p54 };
    for (int i = 0; i < 7; i++) {
        const uint32_t p = g[go79_ptr[i]]._u32[0];
        const uint64_t phi = (uint64_t)p + 32u * (uint64_t)n + 32u;   /* every pair the n iterations load */
        if ((p & 15u) || p >= SPU_LS_SIZE || phi > SPU_LS_SIZE) { s79_fb_rng++; go79_v1(ctx); return; }
        if (p < ohi && olo < phi) { s79_fb_ovl++; go79_v1(ctx); return; }
        *pp[i] = p;
    }
    I.c84 = go79_bc(g[84]); I.c94 = go79_bc(g[94]); I.c106 = go79_bc(g[106]); I.c100 = go79_bc(g[100]);
    I.n93 = _mm256_and_si256(_mm256_sub_epi32(_mm256_setzero_si256(), go79_bc(g[93])), _mm256_set1_epi32(0x3F));
    I.s60 = _mm256_set1_epi32((int)(g[60]._u32[0] & 0xFFFFu));
    I.a60 = _mm256_set1_epi32((int)(~g[60]._u32[0] & 0xFFFFu));
    I.c63 = _mm256_set1_epi32((int)g[63]._u32[0]);
    I.t68 = _mm256_set1_epi32((int)(g[68]._u32[0] & 0xFFFFu));
    I.t70 = _mm256_set1_epi32((int)(g[70]._u32[0] & 0xFFFFu));
    I.c69 = _mm256_set1_epi32((int)(g[69]._u32[0] & 0xFFFFu));
    I.m101 = _mm256_set1_epi32((int)g[101]._u32[0]);
    I.k101 = _mm256_set1_epi32((int)~g[101]._u32[0]);
    s79_v3_calls++;
    /* iterations 0-2 lifted, from the entry state (records 0-2 come from what the prologue loaded) */
    go79_iters(ctx, 3, 0);
    /* records 3 .. n-2 */
    const uint64_t t0 = __rdtsc();
    for (uint32_t k = 3; k + 1 < n; k++)
        if (!go79_record(ls, &I, k - 3, s0 + 108u * k, 0)) go79_record_slow(ls, &I, k - 3, s0 + 108u * k);
    s79_cyc_pk += __rdtsc() - t0;
    s79_pk_recs += (long)n - 4;
    s79_last_s3 = s0 + 324u;
    /* the state before the last iteration: the pointers back to iteration n-4 (they are at 3), then three
     * lifted iterations without stores (every carried register then depends only on those iterations'
     * loads, but r32's bytes 12-15, which the loop carries unchanged), then the last one with its stores */
    const uint32_t back = n - 7u;   /* n >= 8 */
    for (int i = 0; i < 7; i++) g[go79_ptr[i]] = go79_addw(g[go79_ptr[i]], 32u * back);
    g[41] = go79_addw(g[41], 108u * back);
    g[34] = g[41];
    go79_iters(ctx, 4, 3);
}

/* ---- self-test at mode init: cflts against the helper, then whole loop runs, v3 against v1 (the lifted
 * statements) on a scratch context, all GPRs and the whole LS compared ---- */
static uint32_t s79_rng = 0x2545F491u;
static uint32_t go79_rand(void) { s79_rng ^= s79_rng << 13; s79_rng ^= s79_rng >> 17; s79_rng ^= s79_rng << 5; return s79_rng; }
static u128 go79_q(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static u128 go79_rq(void) { return go79_q(go79_rand(), go79_rand(), go79_rand(), go79_rand()); }
static uint32_t go79_fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
/* an input word: mostly ordinary values, plus zeros, denormals, huge, Inf/NaN, random bits, half edges */
static uint32_t go79_rword(void)
{
    const uint32_t r = go79_rand(), s = go79_rand() & 0x80000000u;
    switch (go79_rand() % 16u) {
    case 0: return s;
    case 1: return s | (r & 0x7FFFFFu);                                  /* exponent 0 */
    case 2: return r;                                                    /* any bits */
    case 3: return s | 0x7F800000u | (r & 0x7FFFFFu);                    /* exponent 255 */
    case 4: return s | ((0xFDu + r % 2u) << 23) | (r & 0x7FFFFFu);       /* exponent 253/254 */
    case 5: return s | ((0x38000000u + (r % 0x20000u) - 0x10000u));      /* |x| near 2^-15 */
    case 6: return s | ((0x47800000u + (r % 0x40000u) - 0x20000u));      /* |x| near 65536 */
    case 7: return s | ((0x60u + r % 0x40u) << 23) | (go79_rand() & 0x7FFFFFu);
    default: return go79_fbits(((float)(int32_t)(r >> 1) / 1073741824.0f) * 1.2f);   /* +-1.2 */
    }
}
static void go79_selftest(void)
{
    long bad_c = 0, bad_k = 0, bad_run = 0, runs = 0, took = 0;
    const long sh0 = s79_fb_short, ct0 = s79_fb_ctl, rg0 = s79_fb_rng, ov0 = s79_fb_ovl, pk0 = s79_pk_recs;
    const long fs0 = s79_fma_slow, v30 = s79_v3_calls;
    const uint64_t cp0 = s79_cyc_pk;
    for (int it = 0; it < 20000; it++) {
        u128 a[2]; for (int h = 0; h < 2; h++) for (int i = 0; i < 4; i++) a[h]._u32[i] = go79_rword();
        const int i8 = (it & 1) ? 157 : 142;
        const v8 x = go79_v8(a[0], a[1]), c = (it & 1) ? go79_cflts(x, 157) : go79_cflts(x, 142);
        const u128 r0 = spu_cflts(a[0], i8), r1 = spu_cflts(a[1], i8);
        const u128 c0 = go79_lo(c), c1 = go79_hi(c);
        bad_c += memcmp(&r0, &c0, 16) != 0;
        bad_c += memcmp(&r1, &c1, 16) != 0;
        /* the fast conversion on lanes the fma rule keeps: exponent 1..253 or a zero */
        u128 b[2];
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < 4; i++) {
                const uint32_t r = go79_rand();
                b[h]._u32[i] = (r & 15u) == 0 ? (r & 0x80000000u) : (r & 0x807FFFFFu) | ((1u + go79_rand() % 253u) << 23);
            }
        const v8 y = go79_v8(b[0], b[1]);
        const v8 d = (it & 1) ? go79_cflts_ok(y, 65536.0f) : go79_cflts_ok(y, 2147483648.0f);
        const u128 q0 = spu_cflts(b[0], i8), q1 = spu_cflts(b[1], i8), d0 = go79_lo(d), d1 = go79_hi(d);
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
        for (int r = 0; r < 128; r++) gg[r] = go79_rq();
        for (int i = 0; i < GO79_NCTL; i++) memcpy(gg[go79_ctl[i].r]._u32, go79_ctl[i].w, 16);
        if (it % 50 == 49) gg[go79_ctl[go79_rand() % GO79_NCTL].r]._u32[go79_rand() & 3] ^= 1u << (go79_rand() & 31);
        if (it & 1) {   /* the captures' fma operands and shifts, else random ones */
            static const struct { uint8_t r; uint32_t w[4]; } fma[] = GOW_O719_FMA;   /* generated */
            for (int i = 0; i < (int)(sizeof(fma) / sizeof(fma[0])); i++) memcpy(gg[fma[i].r]._u32, fma[i].w, 16);
        } else {
            for (int i = 0; i < 4; i++) {
                gg[84]._u32[i] = go79_rword(); gg[94]._u32[i] = go79_rword();
                gg[106]._u32[i] = go79_rword(); gg[100]._u32[i] = go79_rword();
                gg[93]._u32[i] = (it & 2) ? go79_rand() : 0u - (go79_rand() % 40u);
            }
        }
        const uint32_t n = (it % 7 == 0) ? 1u + go79_rand() % 9u : 1u + go79_rand() % 70u;
        /* inputs at 0x8000 + 0x2400 i (16-aligned; one in 40 runs not), filled past the n pairs */
        for (int i = 0; i < 7; i++) {
            const uint32_t p = 0x8000u + 0x2400u * (uint32_t)i + ((it % 40 == 7 && i == 3) ? 4u : 0u);
            gg[go79_ptr[i]] = go79_q(p, go79_rand(), go79_rand(), go79_rand());
            for (uint32_t k = 0; k < 8u * (n + 8u); k++) {
                const uint32_t v = go79_rword(), a = (p & ~3u) + 4u * k;
                ctx->ls[a] = (uint8_t)(v >> 24); ctx->ls[a + 1] = (uint8_t)(v >> 16); ctx->ls[a + 2] = (uint8_t)(v >> 8); ctx->ls[a + 3] = (uint8_t)v;
            }
        }
        /* output stream at any alignment, its surroundings random; one in 30 runs over an input */
        const uint32_t s0 = (it % 30 == 11) ? 0x8000u + 0x2400u * 2u + 8u : 0x30000u + go79_rand() % 64u;
        for (uint32_t a = (s0 & ~15u) - 64u; a < s0 + 108u * n + 256u && a < SPU_LS_SIZE; a++) ctx->ls[a] = (uint8_t)go79_rand();
        const uint32_t o = s0 + 108u * n;
        gg[103] = go79_q(o, go79_rand(), go79_rand(), go79_rand());
        for (int i = 0; i < (int)(sizeof(go79_out) / sizeof(go79_out[0])); i++)
            gg[go79_out[i].r] = go79_q(o + go79_out[i].d, go79_rand(), go79_rand(), go79_rand());
        if (it % 60 == 23) gg[90]._u32[0] += 16u;   /* a store pointer off: v1 */
        gg[41] = go79_q(0u - 108u * n, go79_rand(), go79_rand(), go79_rand());
        gg[34] = (it & 4) ? gg[41] : go79_q(0u - 108u * n, 0, 0, 0);
        memcpy(pre_ls, ctx->ls, SPU_LS_SIZE); memcpy(pre_g, gg, sizeof(ctx->gpr));
        go79_v1(ctx);
        memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); memcpy(ref_g, gg, sizeof(ctx->gpr));
        memcpy(ctx->ls, pre_ls, SPU_LS_SIZE); memcpy(gg, pre_g, sizeof(ctx->gpr));
        const long t0 = s79_v3_calls;
        go79_v3(ctx);
        took += s79_v3_calls - t0;
        runs++;
        bad_run += memcmp(ctx->ls, ref_ls, SPU_LS_SIZE) != 0 || memcmp(gg, ref_g, sizeof(ctx->gpr)) != 0;
    }
    free(pre_ls); free(ref_ls); free(pre_g); free(ref_g); free(ctx);
    fprintf(stderr, "[edge-o719900] self-test: cflts %ld of 40000 quads differ, the fast conversion %ld of 40000; "
                    "whole loop runs, v3 against v1: %ld of %ld differ (%ld taken by v3, %ld records packed, %ld records' fma by the helper; left to v1: %ld "
                    "short, %ld controls, %ld range, %ld overlap)\n",
            bad_c, bad_k, bad_run, runs, took, s79_pk_recs - pk0, s79_fma_slow - fs0, s79_fb_short - sh0, s79_fb_ctl - ct0,
            s79_fb_rng - rg0, s79_fb_ovl - ov0);
    s79_fb_short = sh0; s79_fb_ctl = ct0; s79_fb_rng = rg0; s79_fb_ovl = ov0; s79_pk_recs = pk0;
    s79_fma_slow = fs0; s79_v3_calls = v30; s79_cyc_pk = cp0;
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s79_mode = -1;

int gow_edge_o719900_mode(void)
{
    if (s79_mode < 0) {
        const char* e = getenv("GOW_EDGE_O719900");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        s79_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o719900] GOW_EDGE_O719900=%s (lifted loop)\n", e); }
        else {
            fprintf(stderr, "[edge-o719900] GOW_EDGE_O719900=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : "v3");
            go79_selftest();
        }
    }
    return s79_mode;
}

static int s79_fault = -1;
static long s79_fault_at, s79_runs;
static void go79_fault(spu_context* ctx)
{
    if (s79_fault < 0) {
        const char* f = getenv("GOW_EDGE_O719900_FAULT");
        s79_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s79_fault_at = k ? atol(k + 1) : 0;
    }
    if (s79_fault <= 0 || s79_runs++ != s79_fault_at) return;
    const u128* r = ctx->gpr;
    /* the last iteration stores at r91 + r34 with r34 = -108 (r34 = r2 = 0 after the loop) */
    const uint32_t q = (r[91]._u32[0] - 108u) & (SPU_LS_MASK & ~0xFu);
    uint32_t at = q;
    switch (s79_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[31]._u32[0] ^= 1; break;
    case 4:   /* record 3 (r103 + r41 + 324 at entry = r103 - 108n + 324; r103 is not written) */
        at = s79_last_s3 != ~0u ? s79_last_s3 : 0u;
        ctx->ls[at & SPU_LS_MASK] ^= 1;
        break;
    default: break;
    }
    fprintf(stderr, "[edge-o719900] fault %d injected (native run %ld, LS 0x%05X%s)\n", s79_fault, s79_fault_at, at,
            s79_fault == 4 && s79_last_s3 == ~0u ? ": the call was not packed" : "");
    s79_fault = 0;
}

static void go79_run(spu_context* ctx)
{
    if (s79_mode == 2 || s79_mode == 4) { s79_last_s3 = ~0u; go79_v1(ctx); }
    else                                go79_v3(ctx);
    if (__builtin_expect(s79_fault != 0, 0)) go79_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then runs the loop's exit. */
void gow_edge_o719900_loop(spu_context* ctx) { go79_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} go79_check_state;
static _Thread_local go79_check_state* t79_chk;

static long s79_calls, s79_bad, s79_iters;
static uint64_t s79_cyc_nat, s79_cyc_lift;
static int s79_atexit;

/* Calls checked per placement (the blob's LS address the generated switch passes). */
#define GO79_SITES 48
static unsigned s79_site_lsa[GO79_SITES];
static long s79_site_calls[GO79_SITES], s79_site_bad[GO79_SITES];
static _Thread_local unsigned t79_site;

static void go79_check_report(void)
{
    char buf[GO79_SITES * 28] = "", *p = buf;
    for (int i = 0; i < GO79_SITES && s79_site_calls[i]; i++)
        p += snprintf(p, sizeof(buf) - (size_t)(p - buf), " %X:%ld/%ld", s79_site_lsa[i], s79_site_calls[i],
                      s79_site_bad[i]);
    fprintf(stderr, "[edge-o719900-check] calls by placement (blob LS address:calls/differ):%s\n", buf);
    fprintf(stderr, "[edge-o719900-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); tsc: "
                    "native %.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s79_calls, s79_bad, s79_iters, s79_cyc_nat / 1e6, s79_cyc_lift / 1e6,
            s79_iters ? (double)s79_cyc_nat / (double)s79_iters : 0.0,
            s79_iters ? (double)s79_cyc_lift / (double)s79_iters : 0.0);
    fprintf(stderr, "[edge-o719900-check] v3 took %ld calls, %ld records packed; entries left to v1: %ld short, %ld "
                    "other controls or store pointers, %ld outside the LS or unaligned, %ld overlapping; records whose "
                    "fma went to the helper: %ld; tsc in the packed records %.1f M (%.1f per record)\n",
            s79_v3_calls, s79_pk_recs, s79_fb_short, s79_fb_ctl, s79_fb_rng, s79_fb_ovl, s79_fma_slow,
            s79_cyc_pk / 1e6, s79_pk_recs ? (double)s79_cyc_pk / (double)s79_pk_recs : 0.0);
}

void gow_edge_o719900_check_begin_at(spu_context* ctx, unsigned site)
{
    go79_check_state* c = t79_chk;
    if (!c) {
        c = (go79_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t79_chk = c;
    }
    if (!s79_atexit) { s79_atexit = 1; atexit(go79_check_report); }
    t79_site = site;
    s79_iters += (long)((0u - ctx->gpr[41]._u32[0]) / 108u);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    go79_run(ctx);
    s79_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_o719900_check_end(spu_context* ctx)
{
    go79_check_state* c = t79_chk;
    if (!c || !c->pending) return;
    s79_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s79_calls++;
    int site = -1;
    for (int i = 0; i < GO79_SITES; i++) {   /* matching or first free slot (not atomic, like s79_calls) */
        if (s79_site_calls[i] && s79_site_lsa[i] != t79_site) continue;
        s79_site_lsa[i] = t79_site;
        s79_site_calls[i]++;
        site = i;
        break;
    }
    t79_site = 0;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-o719900-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-o719900-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s79_bad++;
        if (site >= 0) s79_site_bad[site]++;
        fprintf(stderr, "[edge-o719900-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) go79_check_report();   /* a game run ends by kill: atexit never runs */
}

/* gow_edge_native_o71c310.c -- native replacement for the loop of EDGE overlay blob 71c310 (input decode,
 * family A in work/edgefrag_scope.md): the loop at blob+0x4A0, in every placement lift of the blob
 * (spu/edgefrag_71c310_<lsa>; 46 placements, one lift each). gen_edge_frag_lift.py puts a switch before the
 * loop in each of them (checking that each placement's loop is the reference 11db0's, statement for
 * statement); GOW_EDGE_O71C310 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native_o71fa90.c (-O2 -march=x86-64-v3 -ffp-contract=off,
 * SPU_FP_NATIVE=1, SPU_FP_CHECK_HOOK=0), inside spu_run_with_halt (MXCSR round toward zero, FTZ/DAZ).
 *
 * Clean-room: written from our own lift of the fragment and our own captures.
 *
 * What the loop does (a reading of the lift; 2 vertices per iteration, r35 a negative byte offset stepping
 * by 32 to 0, r32 = r35 + 32 the back-edge condition): records of 12 bytes per vertex, a pair of them (24
 * bytes) per iteration, read as a 32-byte window (r16 r11: three quads at r44 through the unaligned-window
 * shufb r79 + splat(r44 & 15); r44 + 24 per iteration), software pipelined (iteration i loads pair i + 2,
 * stores the fixed-point slots of pair i and the decoded slots of pair i - 1; the exit tail stores the last
 * pair's).
 *   - fixed point (as 7228c0's): per vertex a quad gathered from the window (shufb r34 / r83), and per word k
 *     the quad shifted right by a bit count and a byte count (rotqmbi, rotqmbybi) the prologue takes from
 *     the stream descriptor (word 0 of r36 r37 r33 r33 for the bits, of r36 r37 r33 r60 for the bytes; the
 *     words merged by selb r64, r80); then v = (w & r68) + r63, float(v >> r66 arithmetic) + float(v << r67)
 *     * 2^-32 (csflt 155, cuflt 123, fa), word 3 from r77 (selb r81): to the slots at r78 / r74 = r78 + 16.
 *     r63 r66 r67 r68 also come from the descriptor.
 *   - two packed unit vectors (as 71fa90's): four field quads from the window (shufb r85 r84 r87 r86), each
 *     word rotm by r55, under r53 with exponent bits r54; pairs of them as [xA yA xB yB] (shufb r82), fma(r72,
 *     f, r71), squares, adjacent sum, d = fs(r73, sum), z = fm(d, fi(d, frsqest(d))); word 0 of each field
 *     quad the byte offset of an entry of the table the prologue stores at r49 = r1 + 32, whose shufb
 *     controls and sign bits place x, y and z (selb r43, r41, r42, + r70 for vertex B): to the slots at
 *     r65 / r75 = r65 + 16 (second vector) and r69 / r76 = r69 + 16 (first).
 *
 * v1: the loop statement for statement in C locals (GOW_O71C_BODY, generated from the reference lift at
 * build time), as one C loop, with direct LS access. Every op is the lift's op on the same operands in the
 * same order, so it is exact for every input.
 *
 * v3 (packed): one pair of records per step, 8 lanes, computed straight from the window bytes. The gathers
 * and shifts are rebuilt from the live registers at entry (g7c_shape_build: for every output word, which
 * window bytes, which bit offset, mask, bias and shift counts), so every value of the descriptor's registers
 * is handled exactly, not only the shapes the captures hold (the self-test draws them at random); the shape
 * is cached per thread while those registers stay the same. Integer work is exact by construction; the
 * conversions are the native ones under the lift's MXCSR (round toward zero; cuflt of a word >= 2^31 as
 * twice the conversion of its half: exact), compared with the helpers in the self-test; fma, fm, fa and fs
 * are the native ops with the helpers' lane rule (a pair with a failing lane is redone by the helpers);
 * frsqest, fi and shufb are 8-lane ports of the helpers (as gow_edge_native_o71fa90.c's). Iterations 0-2
 * run lifted from the entry state, pairs 3 .. n-1 (fixed point) and 2 .. n-2 (decoded) packed, then
 * iterations n-4 .. n-2 lifted again with the pointers set forward and no stores (the state converges), and
 * the last one lifted with its stores: every register the loop writes comes out exactly. Entry checks, else
 * v1 (each counted): 8 .. 8192 iterations (r35 a negative multiple of 32); the controls and masks equal the
 * captures' (GOW_O71C_CTL); r74 r75 r76 at r78 r65 r69 + 16; the slot pointers 16-byte aligned; the record
 * stream, the table entries the index fields can reach and the three slot arrays inside the LS without
 * wrapping, and the slot arrays apart from each other, from the stream and from the table.
 *
 * GOW_EDGE_O71C310 (read once):
 *   unset/0  lifted loop
 *   1        native v3 (v1 for the entries it does not take)
 *   2        native v1
 *   3        check v3: run it on a copy of the state, then the lifted loop, and compare all GPRs and the
 *            whole LS at the loop exit; `[edge-o71c310-check] N calls checked, M differ` every 4096 calls and
 *            at exit, with the calls per placement
 *   4        check v1, the same way
 * GOW_EDGE_O71C310_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 3 of the quad the last iteration stores at r78 + r35 (r35 = -32 there), 3 bit 0 of byte 0 of that
 * quad, 2 bit 0 of r39 (a register the loop writes) after the loop, 4 bit 0 of byte 3 of pair 3's quad at
 * r65 (a packed one when v3 took the call).
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
static inline __m128i g7c_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 g7c_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = g7c_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void g7c_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), g7c_bswap32x4(x));
}
#define LD(a)    g7c_ld(ls, (a))
#define ST(a, v) g7c_st(ls, (a), (v))

/* the lifted loop: GOW_O71C_*. Game code, so it is not in the repo: gen_edge_frag_lift.py --body writes it
 * from the reference lift at build time (no LS address of its own, straight code: checked there). */
#include "gow_edge_o71c310_body.h"

/* v1: the whole loop (back edge on r32) */
static void g7c_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O71C_REGS()
    GOW_O71C_LOAD()
    do {
        GOW_O71C_BODY()
    } while (r32._u32[0] != 0);
    GOW_O71C_STORE()
}

/* the lifted body for `count` iterations, the first `nost` of them without their stores (v3: the first
 * three iterations with their stores; the warm-up, its registers only (the loads still read the LS), and
 * the last iteration with its stores). */
#undef ST
#define ST(a, v) do { if (i >= nost) g7c_st(ls, (a), (v)); } while (0)
static void g7c_iters(spu_context* ctx, uint32_t count, uint32_t nost)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O71C_REGS()
    GOW_O71C_LOAD()
    for (uint32_t i = 0; i < count; i++) {
        GOW_O71C_BODY()
    }
    GOW_O71C_STORE()
}
#undef ST
#define ST(a, v) g7c_st(ls, (a), (v))

/* =====================================================================================================
 * v3: one pair of records per step, 8 lanes. Fixed point: [slot r78 words 0-3 | slot r74 words 0-3]; the
 * vectors: [first vector (the lift's r39 -> r56; slots r69 r76) | second (r21 -> r39; slots r65 r75)].
 * ===================================================================================================== */
typedef __m256i v8;
#define G7C_AI static inline __attribute__((always_inline))

static inline v8 g7c_v8(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 g7c_bc(u128 x) { __m128i a; memcpy(&a, &x, 16); return _mm256_broadcastsi128_si256(a); }
static inline u128 g7c_lo(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_castsi256_si128(v)); return r; }
static inline u128 g7c_hi(v8 v) { u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm256_extracti128_si256(v, 1)); return r; }
static inline __m256 g7c_p(v8 x) { return _mm256_castsi256_ps(x); }
static inline v8 g7c_f(__m256 x) { return _mm256_castps_si256(x); }
static inline v8 g7c_u8(const uint8_t* p) { return _mm256_loadu_si256((const __m256i*)p); }
static inline v8 g7c_u32(const uint32_t* p) { return _mm256_loadu_si256((const __m256i*)p); }

/* The SPU_FP_NATIVE helpers' lane rule (spu_helpers.h spu__xn_fa/fm/fma with SPU_FP_DAZ): a lane is kept
 * when the native result has exponent 1..253 or is a zero (made +0); `ok` collects the lanes that pass. */
G7C_AI v8 g7c_keep(v8 r, v8* ok)
{
    const v8 r1 = _mm256_slli_epi32(r, 1);
    const v8 e = _mm256_srli_epi32(r1, 24), z = _mm256_cmpeq_epi32(r1, _mm256_setzero_si256());
    const v8 n = _mm256_and_si256(_mm256_cmpgt_epi32(e, _mm256_setzero_si256()), _mm256_cmpgt_epi32(_mm256_set1_epi32(254), e));
    *ok = _mm256_and_si256(*ok, _mm256_or_si256(n, z));
    return _mm256_andnot_si256(_mm256_and_si256(z, _mm256_set1_epi32((int)0x80000000u)), r);
}
G7C_AI v8 g7c_fma(v8 a, v8 b, v8 c, v8* ok) { return g7c_keep(g7c_f(_mm256_fmadd_ps(g7c_p(a), g7c_p(b), g7c_p(c))), ok); }
G7C_AI v8 g7c_fm(v8 a, v8 b, v8* ok)        { return g7c_keep(g7c_f(_mm256_mul_ps(g7c_p(a), g7c_p(b))), ok); }
G7C_AI v8 g7c_fa(v8 a, v8 b, v8* ok)        { return g7c_keep(g7c_f(_mm256_add_ps(g7c_p(a), g7c_p(b))), ok); }
G7C_AI v8 g7c_fs(v8 a, v8 b, v8* ok)
{
    return g7c_keep(g7c_f(_mm256_add_ps(g7c_p(a), g7c_p(_mm256_xor_si256(b, _mm256_set1_epi32((int)0x80000000u))))), ok);
}

/* csflt(x, 155): the signed word as a float, truncated (the conversion under the lift's round toward zero) */
G7C_AI v8 g7c_csflt(v8 x) { return g7c_f(_mm256_cvtepi32_ps(x)); }
/* cuflt(x, 123): the unsigned word as a float, truncated, times 2^-32 (a word >= 2^31 converted halved and
 * scaled back: its bit 0 is below the 24 bits kept). The scaling is exact (the result is >= 2^-32). */
G7C_AI v8 g7c_cuflt123(v8 x)
{
    const __m256 lo = _mm256_mul_ps(_mm256_cvtepi32_ps(x), _mm256_set1_ps(0x1p-32f));
    const __m256 hi = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srli_epi32(x, 1)), _mm256_set1_ps(0x1p-31f));
    return g7c_f(_mm256_blendv_ps(lo, hi, g7c_p(x)));
}

/* frsqest, 8 lanes: the helper's integer formula (exponent 190 - (e + 1) / 2, 255 for e = 0; fraction from
 * its 64-entry table by bits 18-23; no sign) */
G7C_AI v8 g7c_frsqest(v8 x)
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
G7C_AI v8 g7c_fi(v8 a, v8 b)
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
G7C_AI v8 g7c_shufb(v8 a, v8 b, v8 c)
{
    const v8 idx = _mm256_xor_si256(_mm256_and_si256(c, _mm256_set1_epi8(0x0F)), _mm256_set1_epi8(0x03));
    const v8 fromb = _mm256_cmpeq_epi8(_mm256_and_si256(c, _mm256_set1_epi8(0x10)), _mm256_set1_epi8(0x10));
    v8 r = _mm256_blendv_epi8(_mm256_shuffle_epi8(a, idx), _mm256_shuffle_epi8(b, idx), fromb);
    const v8 mC0 = _mm256_cmpeq_epi8(_mm256_and_si256(c, _mm256_set1_epi8((char)0xC0)), _mm256_set1_epi8((char)0xC0));
    const v8 mE0 = _mm256_cmpeq_epi8(_mm256_and_si256(c, _mm256_set1_epi8((char)0xE0)), _mm256_set1_epi8((char)0xE0));
    const v8 special = _mm256_blendv_epi8(mC0, _mm256_set1_epi8((char)0x80), mE0);
    return _mm256_blendv_epi8(r, special, c);   /* blendv picks by bit 7 of each control byte */
}
G7C_AI v8 g7c_selb(v8 a, v8 b, v8 m) { return _mm256_or_si256(_mm256_andnot_si256(m, a), _mm256_and_si256(b, m)); }

/* ---- the shape: what the gathers and shifts make of the window, rebuilt from the live registers ----
 * Every word the loop decodes is a big-endian word of window bytes (or constant bytes: the shufb special
 * selectors, bytes shifted in). Per output byte three tables: a pshufb control into window bytes 0-15 and
 * one into 16-31 (0x80 where the byte comes from the other half or is a constant), and the constant. */
typedef struct {
    uint8_t  fh[3][32], fl[3][32];   /* fixed point: the word at the bit offset's byte, and the next byte (top) */
    uint32_t fr[8], fr32[8];         /* the bit offset inside the byte: x = hi << r | lo >> (32 - r) */
    uint32_t m68[8], b63[8], s67[8], s66[8];   /* mask, bias, shl count, arithmetic shift-right count */
    uint8_t  vc[3][32];              /* the vector field words, as shufb r82 pairs them */
    uint32_t vsh[8], vmsk[8], vex[8];
    uint8_t  ic[3][16];              /* the index words: word 0 of the field quads r46 r45 r48 r47 */
    uint32_t ish[4], imsk[4], iex[4];
} g7c_shape;

/* the registers the shape is built from: the descriptor's (r33 r36 r37 r60 r63 r66 r67 r68) and the gather
 * controls, shifts and masks (checked against the captures' too) */
#define G7C_NKEY 18
static const uint8_t g7c_key_r[G7C_NKEY] = { 33, 36, 37, 60, 63, 66, 67, 68, 34, 83, 82, 84, 85, 86, 87, 53, 54, 55 };

static inline uint8_t g7c_byte(const u128* r, int b) { return (uint8_t)(r->_u32[b >> 2] >> (24 - 8 * (b & 3))); }
/* the window byte (0-31) a shufb control byte picks from (r16, r11) (full) or (r11, r11), or 0x100 | the
 * constant a special selector gives */
static inline int g7c_src(uint8_t c, int full)
{
    if (c & 0x80) return 0x100 | (c >= 0xE0 ? 0x80 : c >= 0xC0 ? 0xFF : 0x00);
    return full ? (c & 31) : 16 + (c & 15);
}
static inline void g7c_put(uint8_t* lo, uint8_t* hi, uint8_t* cst, int pos, int s)
{
    if (s >= 0x100)  { lo[pos] = 0x80; hi[pos] = 0x80; cst[pos] = (uint8_t)s; }
    else if (s < 16) { lo[pos] = (uint8_t)s; hi[pos] = 0x80; cst[pos] = 0; }
    else             { lo[pos] = 0x80; hi[pos] = (uint8_t)(s - 16); cst[pos] = 0; }
}

/* 0 if r82 does not pair whole words (then v3 does not take the call) */
static int g7c_shape_build(const u128* g, g7c_shape* s)
{
    memset(s, 0, sizeof *s);
    memset(s->fl[0], 0x80, 32); memset(s->fl[1], 0x80, 32);
    /* fixed point: word k of quad (control r34: slot r78; r83: slot r74) >> (bits(word 0 of bitr[k]) + 8 *
     * bytes(word 0 of byter[k])), i.e. the quad's bits from 32k - S on, zeros before the quad */
    static const uint8_t bitr[4] = { 36, 37, 33, 33 }, byter[4] = { 36, 37, 33, 60 };
    for (int h = 0; h < 2; h++) {
        const u128* C = &g[h ? 83 : 34];
        for (int k = 0; k < 4; k++) {
            const int lane = 4 * h + k;
            const int sb = (0 - (int)g[bitr[k]]._u32[0]) & 7, sB = (0 - ((int)g[byter[k]]._u32[0] >> 3)) & 31;
            const int zero = sB >= 16;
            const int pb = zero ? 0 : 32 * k - (sb + 8 * sB);
            const int j0 = pb >> 3, r = pb & 7;   /* floor, also for pb < 0 */
            for (int m = 0; m < 5; m++) {
                const int j = j0 + m;
                const int src = (zero || j < 0 || j > 15) ? 0x100 : g7c_src(g7c_byte(C, j), 1);
                if (m < 4) g7c_put(s->fh[0], s->fh[1], s->fh[2], 4 * lane + 3 - m, src);
                else       g7c_put(s->fl[0], s->fl[1], s->fl[2], 4 * lane + 3, src);
            }
            s->fr[lane] = (uint32_t)r;
            s->fr32[lane] = 32u - (uint32_t)r;
            s->m68[lane] = g[68]._u32[k];
            s->b63[lane] = g[63]._u32[k];
            s->s67[lane] = g[67]._u32[k] & 63u;
            s->s66[lane] = (0u - g[66]._u32[k]) & 63u;
        }
    }
    /* the vectors: lane j of half h is word w of field quad op (shufb r82 word j), the field quad a shufb of
     * the window: h 0 (r46 <- r85 over the window, r45 <- r84 over r11), h 1 (r48 <- r87, r47 <- r86) */
    static const uint8_t fq[2][2] = { { 85, 84 }, { 87, 86 } };
    for (int h = 0; h < 2; h++)
        for (int j = 0; j < 4; j++) {
            const int lane = 4 * h + j;
            const uint8_t b0 = g7c_byte(&g[82], 4 * j);
            if ((b0 & 0x80) || (b0 & 3)) return 0;
            for (int m = 1; m < 4; m++)
                if (g7c_byte(&g[82], 4 * j + m) != b0 + m) return 0;
            const int op = (b0 >> 4) & 1, w = (b0 >> 2) & 3;
            for (int m = 0; m < 4; m++)
                g7c_put(s->vc[0], s->vc[1], s->vc[2], 4 * lane + 3 - m, g7c_src(g7c_byte(&g[fq[h][op]], 4 * w + m), !op));
            s->vsh[lane] = (0u - g[55]._u32[w]) & 63u;
            s->vmsk[lane] = g[53]._u32[w];
            s->vex[lane] = g[54]._u32[w] & ~g[53]._u32[w];
        }
    static const uint8_t iq[4] = { 85, 84, 87, 86 };   /* r46 r45 r48 r47 */
    for (int i = 0; i < 4; i++) {
        for (int m = 0; m < 4; m++)
            g7c_put(s->ic[0], s->ic[1], s->ic[2], 4 * i + 3 - m, g7c_src(g7c_byte(&g[iq[i]], m), !(i & 1)));
        s->ish[i] = (0u - g[55]._u32[0]) & 63u;
        s->imsk[i] = g[53]._u32[0];
        s->iex[i] = g[54]._u32[0] & ~g[53]._u32[0];
    }
    return 1;
}

typedef struct g7c_inv_ {
    v8 fh0, fh1, fh2, fl0, fl1, fl2, fr, fr32, m68, b63, s67, s66, c77, m81;
    v8 vc0, vc1, vc2, vsh, vmsk, vex, c72, c71, c73;
    v8 m43, c42, m41, a70;
    __m128i ic0, ic1, ic2, ish, imsk, iex;
    uint32_t in;     /* LS address of pair 0 (r44 - 48 at entry) */
    uint32_t tab;    /* r49 */
    uint32_t o78, o74, o65, o75, o69, o76;   /* the slot pointers - 32n */
} g7c_inv;

/* the helpers' path for one pair's floats (a lane failed the rule): the lift's ops on u128 */
static long s7c_fx_slow, s7c_vec_slow;   /* pairs whose floats went to the helpers (lab statistic; racy) */
static __attribute__((noinline, cold)) v8 g7c_fixed_slow(v8 iv, v8 fv)
{
    s7c_fx_slow++;
    u128 r[2];
    for (int h = 0; h < 2; h++)
        r[h] = spu_fa(spu_csflt(h ? g7c_hi(iv) : g7c_lo(iv), 155), spu_cuflt(h ? g7c_hi(fv) : g7c_lo(fv), 123));
    return g7c_v8(r[0], r[1]);
}
static __attribute__((noinline, cold)) void g7c_vec_slow(const g7c_inv* I, v8 F, v8* V, v8* Z)
{
    s7c_vec_slow++;
    u128 v[2], z[2];
    const u128 c72 = g7c_lo(I->c72), c71 = g7c_lo(I->c71), c73 = g7c_lo(I->c73);
    for (int h = 0; h < 2; h++) {
        const u128 f = h ? g7c_hi(F) : g7c_lo(F);
        v[h] = spu_fma(c72, f, c71);
        const u128 sq = spu_fm(v[h], v[h]);
        const u128 d = spu_fs(c73, spu_fa(sq, spu_shlqbyi(sq, 4)));
        z[h] = spu_fm(d, spu_fi(d, spu_frsqest(d)));
    }
    *V = g7c_v8(v[0], v[1]);
    *Z = g7c_v8(z[0], z[1]);
}

static const uint8_t g7c_bsw_c[32] = { 3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12, 3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12 };

#define G7C_GATHER(L, H, c0, c1, c2) \
    _mm256_or_si256(_mm256_or_si256(_mm256_shuffle_epi8((L), (c0)), _mm256_shuffle_epi8((H), (c1))), (c2))

/* pair p: the fixed-point slots (fx) and the decoded slots (dec); the floats checked before anything is
 * stored, and redone by the helpers if a lane fails */
G7C_AI void g7c_pair(uint8_t* ls, const g7c_inv* I, uint32_t p, int fx, int dec)
{
    const uint8_t* q = ls + I->in + 24u * p;
    const __m128i w0 = _mm_loadu_si128((const __m128i*)q), w1 = _mm_loadu_si128((const __m128i*)(q + 16));
    const v8 L = _mm256_broadcastsi128_si256(w0), H = _mm256_broadcastsi128_si256(w1);
    const v8 bsw = g7c_u8(g7c_bsw_c);
    const uint32_t o = 32u * p;
    if (fx) {
        const v8 hw = G7C_GATHER(L, H, I->fh0, I->fh1, I->fh2);
        const v8 lw = G7C_GATHER(L, H, I->fl0, I->fl1, I->fl2);
        const v8 x = _mm256_or_si256(_mm256_sllv_epi32(hw, I->fr), _mm256_srlv_epi32(lw, I->fr32));
        const v8 v = _mm256_add_epi32(_mm256_and_si256(x, I->m68), I->b63);
        const v8 iv = _mm256_srav_epi32(v, I->s66), fv = _mm256_sllv_epi32(v, I->s67);
        v8 ok = _mm256_set1_epi32(-1);
        v8 F = g7c_fa(g7c_csflt(iv), g7c_cuflt123(fv), &ok);
        if (__builtin_expect(_mm256_movemask_ps(g7c_p(ok)) != 0xFF, 0)) F = g7c_fixed_slow(iv, fv);
        const v8 O = _mm256_shuffle_epi8(g7c_selb(F, I->c77, I->m81), bsw);
        _mm_storeu_si128((__m128i*)(ls + I->o78 + o), _mm256_castsi256_si128(O));
        _mm_storeu_si128((__m128i*)(ls + I->o74 + o), _mm256_extracti128_si256(O, 1));
    }
    if (!dec) return;
    const v8 fw = G7C_GATHER(L, H, I->vc0, I->vc1, I->vc2);
    const v8 F = _mm256_or_si256(_mm256_and_si256(_mm256_srlv_epi32(fw, I->vsh), I->vmsk), I->vex);
    v8 ok = _mm256_set1_epi32(-1);
    v8 V = g7c_fma(I->c72, F, I->c71, &ok);
    const v8 sq = g7c_fm(V, V, &ok);
    const v8 d = g7c_fs(I->c73, g7c_fa(sq, _mm256_bsrli_epi128(sq, 4), &ok), &ok);
    v8 Z = g7c_fm(d, g7c_fi(d, g7c_frsqest(d)), &ok);
    if (__builtin_expect(_mm256_movemask_ps(g7c_p(ok)) != 0xFF, 0)) g7c_vec_slow(I, F, &V, &Z);
    /* the table entries for the index words of r46 r45 r48 r47 */
    const __m128i iw = _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(w0, I->ic0), _mm_shuffle_epi8(w1, I->ic1)), I->ic2);
    const __m128i ix = _mm_or_si128(_mm_and_si128(_mm_srlv_epi32(iw, I->ish), I->imsk), I->iex);
    const v8 T1 = g7c_v8(g7c_ld(ls, I->tab + (uint32_t)_mm_cvtsi128_si32(ix)),
                         g7c_ld(ls, I->tab + (uint32_t)_mm_extract_epi32(ix, 1)));
    const v8 T2 = g7c_v8(g7c_ld(ls, I->tab + (uint32_t)_mm_extract_epi32(ix, 2)),
                         g7c_ld(ls, I->tab + (uint32_t)_mm_extract_epi32(ix, 3)));
    const v8 V1 = _mm256_permute2x128_si256(V, V, 0x00), V2 = _mm256_permute2x128_si256(V, V, 0x11);
    const v8 Z1 = _mm256_permute2x128_si256(Z, Z, 0x00), Z2 = _mm256_permute2x128_si256(Z, Z, 0x11);
    const v8 C1 = _mm256_add_epi32(g7c_selb(I->c42, T1, I->m41), I->a70);
    const v8 C2 = _mm256_add_epi32(g7c_selb(I->c42, T2, I->m41), I->a70);
    const v8 O1 = _mm256_shuffle_epi8(g7c_shufb(g7c_selb(Z1, T1, I->m43), V1, C1), bsw);
    const v8 O2 = _mm256_shuffle_epi8(g7c_shufb(g7c_selb(Z2, T2, I->m43), V2, C2), bsw);
    _mm_storeu_si128((__m128i*)(ls + I->o69 + o), _mm256_castsi256_si128(O1));
    _mm_storeu_si128((__m128i*)(ls + I->o76 + o), _mm256_extracti128_si256(O1, 1));
    _mm_storeu_si128((__m128i*)(ls + I->o65 + o), _mm256_castsi256_si128(O2));
    _mm_storeu_si128((__m128i*)(ls + I->o75 + o), _mm256_extracti128_si256(O2, 1));
}

/* the controls and masks the loop gets from its prologue: GOW_O71C_CTL, {register, its four SPU words}. Game
 * data, so not in the repo: gen_edge_frag_lift.py --consts computes them at build time from the reference
 * lift's prologue and the blob's bytes in the EBOOT. r79 (the identity control of the unaligned window), r88
 * (its splat), r64 and r80 (the selb masks merging the fixed-point words) are what the layout above rests on;
 * the others (the gathers r34 r83 r84-r87 r82, r53 r54 r55, the output controls r41 r42 r43 r70, r77 r81) are
 * used as they are and checked too. GOW_O71C_FMA (r72 r71 r73) and GOW_O71C_TAB (the table the prologue
 * stores at r49) are used by the self-test only. */
#include "gow_edge_o71c310_consts.h"
static const struct { uint8_t r; uint32_t w[4]; } g7c_ctl[GOW_O71C_NCTL] = GOW_O71C_CTL;
#define G7C_NCTL GOW_O71C_NCTL

#define G7C_NMIN 8u
#define G7C_NMAX 8192u
static long s7c_fb_short;    /* entries with fewer than 8 iterations (or an implausible count): v1 */
static long s7c_fb_ctl;      /* entries with other controls, masks or slot pointer pairs: v1 */
static long s7c_fb_rng;      /* entries with a range outside the LS or unaligned slot pointers: v1 */
static long s7c_fb_ovl;      /* entries whose slot arrays overlap each other, the stream or the table: v1 */
static long s7c_fb_shape;    /* entries whose r82 does not pair whole words: v1 */
static long s7c_pk_pairs;    /* pairs decoded packed */
static long s7c_v3_calls;    /* entries v3 took */
static long s7c_shape_builds;/* shapes built (the per-thread cache missed) */
static uint64_t s7c_cyc_pk;  /* tsc in the packed pairs (lab statistic) */
static uint32_t s7c_last_p3; /* LS address of the last packed call's pair-3 quad at r65 (fault 4), or ~0 */

/* the last shape per thread, by the registers it was built from (bytes: no alignment needs in TLS) */
typedef struct { int valid, good; u128 key[G7C_NKEY]; uint8_t shape[sizeof(g7c_shape)]; } g7c_shape_cache;
static _Thread_local g7c_shape_cache* t7c_cache;

static inline u128 g7c_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}
static inline int g7c_apart(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a1 <= b0 || b1 <= a0; }

static void g7c_v3(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    s7c_last_p3 = ~0u;
    const uint32_t w35 = g[35]._u32[0];
    const uint32_t nb = 0u - w35, n = nb / 32u;
    if (!w35 || nb % 32u || n < G7C_NMIN || n > G7C_NMAX) { s7c_fb_short++; g7c_v1(ctx); return; }
    for (int i = 0; i < G7C_NCTL; i++)
        if (memcmp(g[g7c_ctl[i].r]._u32, g7c_ctl[i].w, 16)) { s7c_fb_ctl++; g7c_v1(ctx); return; }
    const uint32_t p78 = g[78]._u32[0], p65 = g[65]._u32[0], p69 = g[69]._u32[0];
    if (g[74]._u32[0] != p78 + 16u || g[75]._u32[0] != p65 + 16u || g[76]._u32[0] != p69 + 16u) {
        s7c_fb_ctl++; g7c_v1(ctx); return;
    }
    /* slot arrays [p - 32n, p) (pairs 0 .. n-1; iteration 0's stores at r65/r75/r69/r76 + r6 come first in
     * both paths), the stream's loads [(r44 - 48) & ~15, r44 + 24(n-1) + 48), the table entries the index
     * words reach [r49 + ex, r49 + (ex | mask) + 16) */
    const uint32_t r44 = g[44]._u32[0], tab = g[49]._u32[0];
    const uint32_t imsk = g[53]._u32[0], iex = g[54]._u32[0] & ~imsk;
    const uint64_t s0 = (r44 - 48u) & ~15u, s1 = (uint64_t)r44 + 24u * (uint64_t)(n - 1u) + 48u;
    const uint64_t t0 = ((uint64_t)tab + iex) & ~(uint64_t)15, t1 = (((uint64_t)tab + (iex | imsk)) & ~(uint64_t)15) + 16u;
    const uint32_t ps[3] = { p78, p65, p69 };
    if (r44 < 48u || s1 > SPU_LS_SIZE || t1 > SPU_LS_SIZE) { s7c_fb_rng++; g7c_v1(ctx); return; }
    for (int i = 0; i < 3; i++)
        if ((ps[i] & 15u) || ps[i] < nb || ps[i] > SPU_LS_SIZE) { s7c_fb_rng++; g7c_v1(ctx); return; }
    for (int i = 0; i < 3; i++) {
        const uint64_t a0 = ps[i] - nb, a1 = ps[i];
        if (!g7c_apart(a0, a1, s0, s1) || !g7c_apart(a0, a1, t0, t1)) { s7c_fb_ovl++; g7c_v1(ctx); return; }
        for (int j = i + 1; j < 3; j++)
            if (!g7c_apart(a0, a1, ps[j] - nb, ps[j])) { s7c_fb_ovl++; g7c_v1(ctx); return; }
    }
    /* the shape: the thread's cached one if its registers are the same */
    g7c_shape_cache* sc = t7c_cache;
    if (!sc) sc = t7c_cache = (g7c_shape_cache*)calloc(1, sizeof(*sc));
    if (!sc) { s7c_fb_shape++; g7c_v1(ctx); return; }
    int same = sc->valid;
    for (int i = 0; same && i < G7C_NKEY; i++) same = !memcmp(&sc->key[i], &g[g7c_key_r[i]], 16);
    g7c_shape S;
    if (!same) {
        sc->good = g7c_shape_build(g, &S);
        memcpy(sc->shape, &S, sizeof S);
        for (int i = 0; i < G7C_NKEY; i++) sc->key[i] = g[g7c_key_r[i]];
        sc->valid = 1;
        s7c_shape_builds++;
    } else {
        memcpy(&S, sc->shape, sizeof S);
    }
    if (!sc->good) { s7c_fb_shape++; g7c_v1(ctx); return; }
    g7c_inv I;
    I.fh0 = g7c_u8(S.fh[0]); I.fh1 = g7c_u8(S.fh[1]); I.fh2 = g7c_u8(S.fh[2]);
    I.fl0 = g7c_u8(S.fl[0]); I.fl1 = g7c_u8(S.fl[1]); I.fl2 = g7c_u8(S.fl[2]);
    I.fr = g7c_u32(S.fr); I.fr32 = g7c_u32(S.fr32);
    I.m68 = g7c_u32(S.m68); I.b63 = g7c_u32(S.b63); I.s67 = g7c_u32(S.s67); I.s66 = g7c_u32(S.s66);
    I.c77 = g7c_bc(g[77]); I.m81 = g7c_bc(g[81]);
    I.vc0 = g7c_u8(S.vc[0]); I.vc1 = g7c_u8(S.vc[1]); I.vc2 = g7c_u8(S.vc[2]);
    I.vsh = g7c_u32(S.vsh); I.vmsk = g7c_u32(S.vmsk); I.vex = g7c_u32(S.vex);
    I.c72 = g7c_bc(g[72]); I.c71 = g7c_bc(g[71]); I.c73 = g7c_bc(g[73]);
    I.m43 = g7c_bc(g[43]); I.c42 = g7c_bc(g[42]); I.m41 = g7c_bc(g[41]);
    { u128 z; memset(&z, 0, sizeof z); I.a70 = g7c_v8(z, g[70]); }
    I.ic0 = _mm_loadu_si128((const __m128i*)S.ic[0]); I.ic1 = _mm_loadu_si128((const __m128i*)S.ic[1]);
    I.ic2 = _mm_loadu_si128((const __m128i*)S.ic[2]);
    I.ish = _mm_loadu_si128((const __m128i*)S.ish); I.imsk = _mm_loadu_si128((const __m128i*)S.imsk);
    I.iex = _mm_loadu_si128((const __m128i*)S.iex);
    I.in = r44 - 48u;
    I.tab = tab;
    I.o78 = p78 - nb; I.o74 = I.o78 + 16u; I.o65 = p65 - nb; I.o75 = I.o65 + 16u; I.o69 = p69 - nb; I.o76 = I.o69 + 16u;
    s7c_v3_calls++;
    /* iterations 0-2 lifted, from the entry state (they store what comes from the prologue's registers) */
    g7c_iters(ctx, 3, 0);
    /* fixed point of pairs 3 .. n-1, decoded slots of pairs 2 .. n-2 */
    const uint64_t c0 = __rdtsc();
    g7c_pair(ls, &I, 2, 0, 1);
    for (uint32_t p = 3; p + 1 < n; p++) g7c_pair(ls, &I, p, 1, 1);
    g7c_pair(ls, &I, n - 1u, 1, 0);
    s7c_cyc_pk += __rdtsc() - c0;
    s7c_pk_pairs += (long)n - 3;
    s7c_last_p3 = I.o65 + 96u;
    /* the state before the last iteration: the pointers from iteration 3 to n-4, three lifted iterations
     * without stores, then the last one with its stores */
    const uint32_t back = n - 7u;   /* n >= 8 */
    g[35] = g7c_addw(g[35], 32u * back);
    g[6] = g7c_addw(g[6], 32u * back);
    g[44] = g7c_addw(g[44], 24u * back);
    g7c_iters(ctx, 4, 3);
}

/* ---- self-test at mode init: the pieces against the helpers, then whole loop runs, v3 against v1 (the
 * lifted statements) on a scratch context, all GPRs and the whole LS compared ---- */
static uint32_t s7c_rng = 0xBB67AE85u;
static uint32_t g7c_rand(void) { s7c_rng ^= s7c_rng << 13; s7c_rng ^= s7c_rng >> 17; s7c_rng ^= s7c_rng << 5; return s7c_rng; }
static u128 g7c_q(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static u128 g7c_rq(void) { return g7c_q(g7c_rand(), g7c_rand(), g7c_rand(), g7c_rand()); }
/* a float word: ordinary values, zeros, denormals, exponent 1/253/254/255, any bits, values near 0 and 1 */
static uint32_t g7c_rword(void)
{
    const uint32_t r = g7c_rand(), s = g7c_rand() & 0x80000000u, m = g7c_rand() & 0x7FFFFFu;
    switch (g7c_rand() % 12u) {
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
/* an integer word for the conversions: any bits, small, near the powers of two, the extremes */
static uint32_t g7c_iword(void)
{
    const uint32_t r = g7c_rand();
    switch (g7c_rand() % 8u) {
    case 0: return 0u;
    case 1: return r & 0xFFu;
    case 2: return (1u << (r % 32u)) - (g7c_rand() % 3u);
    case 3: return 0x80000000u | (r & 0xFFFu);
    case 4: return 0xFFFFFFFFu - (r & 0xFFu);
    case 5: return r >> (g7c_rand() % 32u);
    default: return r;
    }
}
/* a descriptor count word: a small negative bit offset (as the prologue makes them), else any bits */
static uint32_t g7c_cword(int realistic) { return realistic ? 0u - g7c_rand() % 128u : g7c_rand(); }
static int g7c_eq(u128 a, u128 b) { return !memcmp(&a, &b, 16); }
static void g7c_selftest(void)
{
    long bad_e = 0, bad_i = 0, bad_s = 0, bad_f = 0, kept = 0, bad_c = 0, runs = 0, bad_run = 0, took = 0;
    const long sh0 = s7c_fb_short, ct0 = s7c_fb_ctl, rg0 = s7c_fb_rng, ov0 = s7c_fb_ovl, sp0 = s7c_fb_shape;
    const long pk0 = s7c_pk_pairs, fx0 = s7c_fx_slow, vs0 = s7c_vec_slow, v30 = s7c_v3_calls, sb0 = s7c_shape_builds;
    const uint64_t cp0 = s7c_cyc_pk;
    for (int it = 0; it < 20000; it++) {
        u128 a[2], b[2], c[2], e[2], ia[2];
        for (int h = 0; h < 2; h++) {
            a[h] = g7c_q(g7c_rword(), g7c_rword(), g7c_rword(), g7c_rword());
            b[h] = g7c_q(g7c_rword(), g7c_rword(), g7c_rword(), g7c_rword());
            c[h] = g7c_q(g7c_rword(), g7c_rword(), g7c_rword(), g7c_rword());
            e[h] = (it & 1) ? spu_frsqest(b[h]) : b[h];   /* fi on an estimate, or on any bits */
            ia[h] = g7c_q(g7c_iword(), g7c_iword(), g7c_iword(), g7c_iword());
        }
        /* the conversions */
        const v8 cs = g7c_csflt(g7c_v8(ia[0], ia[1])), cu = g7c_cuflt123(g7c_v8(ia[0], ia[1]));
        for (int h = 0; h < 2; h++) {
            bad_c += !g7c_eq(spu_csflt(ia[h], 155), h ? g7c_hi(cs) : g7c_lo(cs));
            bad_c += !g7c_eq(spu_cuflt(ia[h], 123), h ? g7c_hi(cu) : g7c_lo(cu));
        }
        /* frsqest and fi */
        const v8 r8 = g7c_frsqest(g7c_v8(a[0], a[1])), i8 = g7c_fi(g7c_v8(a[0], a[1]), g7c_v8(e[0], e[1]));
        for (int h = 0; h < 2; h++) {
            bad_e += !g7c_eq(spu_frsqest(a[h]), h ? g7c_hi(r8) : g7c_lo(r8));
            bad_i += !g7c_eq(spu_fi(a[h], e[h]), h ? g7c_hi(i8) : g7c_lo(i8));
        }
        /* shufb, controls with any bytes (special selectors included) */
        const u128 sc0 = g7c_rq(), sc1 = g7c_rq();
        const v8 s8 = g7c_shufb(g7c_v8(a[0], a[1]), g7c_v8(b[0], b[1]), g7c_v8(sc0, sc1));
        bad_s += !g7c_eq(spu_shufb(a[0], b[0], sc0), g7c_lo(s8));
        bad_s += !g7c_eq(spu_shufb(a[1], b[1], sc1), g7c_hi(s8));
        /* fma fm fa fs with the lane rule: a lane kept must be the helper's */
        for (int op = 0; op < 4; op++) {
            v8 ok = _mm256_set1_epi32(-1);
            const v8 A = g7c_v8(a[0], a[1]), B = g7c_v8(b[0], b[1]), C = g7c_v8(c[0], c[1]);
            const v8 r = op == 0 ? g7c_fma(A, B, C, &ok) : op == 1 ? g7c_fm(A, B, &ok) : op == 2 ? g7c_fa(A, B, &ok) : g7c_fs(A, B, &ok);
            for (int h = 0; h < 2; h++) {
                const u128 ref = op == 0 ? spu_fma(a[h], b[h], c[h]) : op == 1 ? spu_fm(a[h], b[h])
                               : op == 2 ? spu_fa(a[h], b[h]) : spu_fs(a[h], b[h]);
                const u128 got = h ? g7c_hi(r) : g7c_lo(r), okw = h ? g7c_hi(ok) : g7c_lo(ok);
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
    static const struct { uint8_t r; uint32_t w[4]; } fma[] = GOW_O71C_FMA;   /* generated */
    static const uint32_t tabw[GOW_O71C_NTAB][4] = GOW_O71C_TAB;               /* generated */
    for (int it = 0; ctx && pre_ls && ref_ls && pre_g && ref_g && it < 800; it++) {
        ctx->ls = ctx->ls_store;
        memset(ctx->ls, 0, SPU_LS_SIZE);
        u128* gg = ctx->gpr;
        for (int r = 0; r < 128; r++) gg[r] = g7c_rq();
        for (int i = 0; i < G7C_NCTL; i++) memcpy(gg[g7c_ctl[i].r]._u32, g7c_ctl[i].w, 16);
        if (it % 50 == 49) gg[g7c_ctl[g7c_rand() % G7C_NCTL].r]._u32[g7c_rand() & 3] ^= 1u << (g7c_rand() & 31);
        if (it & 1) {   /* the captures' operands, else random ones */
            for (int i = 0; i < (int)(sizeof(fma) / sizeof(fma[0])); i++) memcpy(gg[fma[i].r]._u32, fma[i].w, 16);
        } else {
            for (int i = 0; i < 4; i++) { gg[72]._u32[i] = g7c_rword(); gg[71]._u32[i] = g7c_rword(); gg[73]._u32[i] = g7c_rword(); }
        }
        /* the descriptor's registers: realistic (small negative bit offsets, field masks, shift counts) in
         * two runs of three, any bits otherwise; the same ones a few runs in a row (the shape cache) */
        if (it % 4 != 3) {
            const int real = it % 3 != 0;
            for (int i = 0; i < 4; i++) {
                gg[33]._u32[i] = g7c_cword(real); gg[36]._u32[i] = g7c_cword(real);
                gg[37]._u32[i] = g7c_cword(real); gg[60]._u32[i] = g7c_cword(real);
                const uint32_t wd = 1u + g7c_rand() % 32u, k = g7c_rand() % 33u;
                gg[68]._u32[i] = real ? (wd == 32 ? ~0u : (1u << wd) - 1u) : g7c_rand();
                gg[63]._u32[i] = real ? (g7c_rand() & 1 ? 0u : (g7c_rand() & 0xFFFu) - 0x800u) : g7c_rand();
                gg[66]._u32[i] = real ? 0u - k : g7c_rand();
                gg[67]._u32[i] = real ? 32u - k : g7c_rand();
            }
        } else if (it > 3) {
            for (int i = 0; i < 8; i++) gg[g7c_key_r[i]] = pre_g[g7c_key_r[i]];   /* the previous run's */
        }
        const uint32_t n = (it % 7 == 0) ? 1u + g7c_rand() % 9u : 1u + g7c_rand() % 70u;
        /* the table at 0x1000 + 16k (r49 = r1 + 32), the captures' entries in half the runs, else random;
         * the 4 entries past it and around it random */
        const uint32_t tab = 0x1000u + 16u * (g7c_rand() % 8u);
        for (uint32_t a = tab - 64u; a < tab + 320u; a++) ctx->ls[a] = (uint8_t)g7c_rand();
        if (it & 2)
            for (int k = 0; k < GOW_O71C_NTAB; k++)
                for (int j = 0; j < 4; j++) {
                    const uint32_t v = tabw[k][j], a = tab + 16u * (uint32_t)k + 4u * (uint32_t)j;
                    ctx->ls[a] = (uint8_t)(v >> 24); ctx->ls[a + 1] = (uint8_t)(v >> 16); ctx->ls[a + 2] = (uint8_t)(v >> 8); ctx->ls[a + 3] = (uint8_t)v;
                }
        gg[49] = g7c_q(tab, g7c_rand(), g7c_rand(), g7c_rand());
        /* the record stream at any alignment, random bytes past the n + 2 pairs it may read */
        const uint32_t S = 0x8000u + g7c_rand() % 64u;
        for (uint32_t a = S - 16u; a < S + 24u * (n + 4u) + 64u; a++) ctx->ls[a] = (uint8_t)g7c_rand();
        gg[44] = g7c_q(S + 48u, g7c_rand(), g7c_rand(), g7c_rand());
        /* slot arrays (16-aligned; one in 40 runs not; one in 30 over the stream), surroundings random */
        uint32_t pp[3] = { 0x20000u + 32u * n, 0x28000u + 32u * n, 0x30000u + 32u * n };
        if (it % 40 == 7) pp[1] += 4u;
        if (it % 30 == 11) pp[2] = (S & ~15u) + 32u * n + 64u;
        for (int i = 0; i < 3; i++)
            for (uint32_t a = pp[i] - 32u * n - 64u; a < pp[i] + 128u; a++) ctx->ls[a & SPU_LS_MASK] = (uint8_t)g7c_rand();
        gg[78] = g7c_q(pp[0], g7c_rand(), g7c_rand(), g7c_rand());
        gg[65] = g7c_q(pp[1], g7c_rand(), g7c_rand(), g7c_rand());
        gg[69] = g7c_q(pp[2], g7c_rand(), g7c_rand(), g7c_rand());
        gg[74] = g7c_q(pp[0] + 16u, g7c_rand(), g7c_rand(), g7c_rand());
        gg[75] = g7c_q(pp[1] + 16u, g7c_rand(), g7c_rand(), g7c_rand());
        gg[76] = g7c_q(pp[2] + 16u, g7c_rand(), g7c_rand(), g7c_rand());
        if (it % 60 == 23) gg[75]._u32[0] += 16u;   /* a slot pointer pair off: v1 */
        gg[35] = g7c_q(0u - 32u * n, g7c_rand(), g7c_rand(), g7c_rand());
        gg[6] = (it & 4) ? g7c_q(0, 0, 0, 0) : g7c_q((g7c_rand() % 64u) * 16u, g7c_rand(), g7c_rand(), g7c_rand());
        memcpy(pre_ls, ctx->ls, SPU_LS_SIZE); memcpy(pre_g, gg, sizeof(ctx->gpr));
        g7c_v1(ctx);
        memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); memcpy(ref_g, gg, sizeof(ctx->gpr));
        memcpy(ctx->ls, pre_ls, SPU_LS_SIZE); memcpy(gg, pre_g, sizeof(ctx->gpr));
        const long t0 = s7c_v3_calls;
        g7c_v3(ctx);
        took += s7c_v3_calls - t0;
        runs++;
        const int bad = memcmp(ctx->ls, ref_ls, SPU_LS_SIZE) != 0 || memcmp(gg, ref_g, sizeof(ctx->gpr)) != 0;
        if (bad && bad_run < 3) {
            for (int r = 0; r < 128; r++)
                if (memcmp(&gg[r], &ref_g[r], 16))
                    fprintf(stderr, "[edge-o71c310] self-test run %d (n %u): r%d v1 %08X v3 %08X\n", it, n, r, ref_g[r]._u32[0], gg[r]._u32[0]);
            for (long i = 0; i < SPU_LS_SIZE; i++)
                if (ctx->ls[i] != ref_ls[i]) {
                    fprintf(stderr, "[edge-o71c310] self-test run %d: LS first differs at 0x%05lX (v1 %02X v3 %02X)\n", it, i, ref_ls[i], ctx->ls[i]);
                    break;
                }
        }
        bad_run += bad;
    }
    free(pre_ls); free(ref_ls); free(pre_g); free(ref_g); free(ctx);
    fprintf(stderr, "[edge-o71c310] self-test: csflt/cuflt %ld of 80000 quads differ, frsqest %ld of 40000, fi %ld of "
                    "40000, shufb %ld of 40000; fma/fm/fa/fs lanes kept %ld, %ld differ; whole loop runs, v3 against "
                    "v1: %ld of %ld differ (%ld taken by v3, %ld pairs packed, %ld shapes built, pairs by the "
                    "helpers: %ld fixed point, %ld vectors; left to v1: %ld short, %ld controls, %ld range, %ld "
                    "overlap, %ld shape)\n",
            bad_c, bad_e, bad_i, bad_s, kept, bad_f, bad_run, runs, took, s7c_pk_pairs - pk0,
            s7c_shape_builds - sb0, s7c_fx_slow - fx0, s7c_vec_slow - vs0, s7c_fb_short - sh0, s7c_fb_ctl - ct0,
            s7c_fb_rng - rg0, s7c_fb_ovl - ov0, s7c_fb_shape - sp0);
    s7c_fb_short = sh0; s7c_fb_ctl = ct0; s7c_fb_rng = rg0; s7c_fb_ovl = ov0; s7c_fb_shape = sp0;
    s7c_pk_pairs = pk0; s7c_fx_slow = fx0; s7c_vec_slow = vs0; s7c_v3_calls = v30; s7c_shape_builds = sb0;
    s7c_cyc_pk = cp0;
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s7c_mode = -1;

int gow_edge_o71c310_mode(void)
{
    if (s7c_mode < 0) {
        const char* e = getenv("GOW_EDGE_O71C310");
        int m = e && *e ? atoi(e) : 0;   /* unset: the lifted loop (the port's main sets 1) */
        if (m < 0 || m > 4) m = 0;
        s7c_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o71c310] GOW_EDGE_O71C310=%s (lifted loop)\n", e); }
        else {
            fprintf(stderr, "[edge-o71c310] GOW_EDGE_O71C310=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : "v3");
            g7c_selftest();
        }
    }
    return s7c_mode;
}

static int s7c_fault = -1;
static long s7c_fault_at, s7c_runs;
static void g7c_fault(spu_context* ctx)
{
    if (s7c_fault < 0) {
        const char* f = getenv("GOW_EDGE_O71C310_FAULT");
        s7c_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s7c_fault_at = k ? atol(k + 1) : 0;
    }
    if (s7c_fault <= 0 || s7c_runs++ != s7c_fault_at) return;
    const uint32_t q = (ctx->gpr[78]._u32[0] - 32u) & (SPU_LS_MASK & ~0xFu);   /* r78 is not written */
    uint32_t at = q;
    switch (s7c_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[39]._u32[0] ^= 1; break;
    case 4:
        at = s7c_last_p3 != ~0u ? s7c_last_p3 : 0u;
        ctx->ls[(at + 3u) & SPU_LS_MASK] ^= 1;
        break;
    default: break;
    }
    fprintf(stderr, "[edge-o71c310] fault %d injected (native run %ld, LS 0x%05X%s)\n", s7c_fault, s7c_fault_at, at,
            s7c_fault == 4 && s7c_last_p3 == ~0u ? ": the call was not packed" : "");
    s7c_fault = 0;
}

static void g7c_run(spu_context* ctx)
{
    if (s7c_mode == 2 || s7c_mode == 4) { s7c_last_p3 = ~0u; g7c_v1(ctx); }
    else                                g7c_v3(ctx);
    if (__builtin_expect(s7c_fault != 0, 0)) g7c_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then runs the loop's exit. */
void gow_edge_o71c310_loop(spu_context* ctx) { g7c_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} g7c_check_state;
static _Thread_local g7c_check_state* t7c_chk;

static long s7c_calls, s7c_bad, s7c_iters;
static uint64_t s7c_cyc_nat, s7c_cyc_lift;
static int s7c_atexit;

#define G7C_SITES 56
static unsigned s7c_site_lsa[G7C_SITES];
static long s7c_site_calls[G7C_SITES], s7c_site_bad[G7C_SITES];
static _Thread_local unsigned t7c_site;

static void g7c_check_report(void)
{
    char buf[G7C_SITES * 28] = "", *p = buf;
    for (int i = 0; i < G7C_SITES && s7c_site_calls[i]; i++)
        p += snprintf(p, sizeof(buf) - (size_t)(p - buf), " %X:%ld/%ld", s7c_site_lsa[i], s7c_site_calls[i],
                      s7c_site_bad[i]);
    fprintf(stderr, "[edge-o71c310-check] calls by placement (blob LS address:calls/differ):%s\n", buf);
    fprintf(stderr, "[edge-o71c310-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); tsc: "
                    "native %.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s7c_calls, s7c_bad, s7c_iters, s7c_cyc_nat / 1e6, s7c_cyc_lift / 1e6,
            s7c_iters ? (double)s7c_cyc_nat / (double)s7c_iters : 0.0,
            s7c_iters ? (double)s7c_cyc_lift / (double)s7c_iters : 0.0);
    fprintf(stderr, "[edge-o71c310-check] v3 took %ld calls, %ld pairs packed, %ld shapes built; entries left to v1: "
                    "%ld short, %ld other controls or slot pointers, %ld outside the LS or unaligned, %ld overlapping, "
                    "%ld shape; pairs whose floats went to the helpers: %ld fixed point, %ld vectors; tsc in the "
                    "packed pairs %.1f M (%.1f per pair)\n",
            s7c_v3_calls, s7c_pk_pairs, s7c_shape_builds, s7c_fb_short, s7c_fb_ctl, s7c_fb_rng, s7c_fb_ovl,
            s7c_fb_shape, s7c_fx_slow, s7c_vec_slow, s7c_cyc_pk / 1e6,
            s7c_pk_pairs ? (double)s7c_cyc_pk / (double)s7c_pk_pairs : 0.0);
}

void gow_edge_o71c310_check_begin_at(spu_context* ctx, unsigned site)
{
    g7c_check_state* c = t7c_chk;
    if (!c) {
        c = (g7c_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t7c_chk = c;
    }
    if (!s7c_atexit) { s7c_atexit = 1; atexit(g7c_check_report); }
    t7c_site = site;
    s7c_iters += (long)((0u - ctx->gpr[35]._u32[0]) / 32u);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    g7c_run(ctx);
    s7c_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_o71c310_check_end(spu_context* ctx)
{
    g7c_check_state* c = t7c_chk;
    if (!c || !c->pending) return;
    s7c_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s7c_calls++;
    int site = -1;
    for (int i = 0; i < G7C_SITES; i++) {   /* matching or first free slot (not atomic, like s7c_calls) */
        if (s7c_site_calls[i] && s7c_site_lsa[i] != t7c_site) continue;
        s7c_site_lsa[i] = t7c_site;
        s7c_site_calls[i]++;
        site = i;
        break;
    }
    t7c_site = 0;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-o71c310-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-o71c310-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s7c_bad++;
        if (site >= 0) s7c_site_bad[site]++;
        fprintf(stderr, "[edge-o71c310-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) g7c_check_report();   /* a game run ends by kill: atexit never runs */
}

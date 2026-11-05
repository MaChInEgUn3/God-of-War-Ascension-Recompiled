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
 *   - r49's: fma(x, r84 = 32767.5, r94 = -0.5) -> cflts 157 (x 2^16, truncated, saturated) -> the high
 *     halfwords picked by shufb r85/r115: s16 normalized;
 *   - r47's: fma(x, r106, r100) (scale/bias quads from the blob's pool) -> cflts 142 -> rotm by r93: an
 *     unsigned field of a width set by the prologue;
 *   - r45 r42 r40 r46's: float -> half: sf(r63 = 0x38000000, x) (exponent rebias 127 -> 15), rotqbii 3,
 *     high halfwords by shufb r64 (mantissa truncated, i.e. toward zero); |x| (andc 0x8000 on x's high
 *     halfword) clgth 0x4780 -> 0x7BFF (clamp to the largest half), sign put back by selb 0x8000, and |x|'s
 *     high halfword below 0x3800 -> 0 (no half denormals: flushed);
 *   - r54's passed through (selb with r117 into the stream);
 *   - the fields are merged into a byte stream by shufb controls the prologue loaded, rotated to the output
 *     alignment (control r75 = r71 - splat(r19 & 15)) and stored as eight aligned quads at r34 + {r91 r90
 *     r89 r88 r87 r86 r102 r44}, the partial quad read back (r92 + r34) and merged.
 *
 * Native version: the loop statement for statement in C locals (GOW_O719_BODY, generated from the reference
 * lift at build time), as one C loop (the lift hops through five functions per iteration), with direct LS
 * access, the rotqbii by a word shift pair, and the loop-invariant shufb controls left to the compiler to
 * hoist. Every op is the lift's op on the same operands in the same order, so it is exact for every input:
 * there is no input it does not handle and nothing to fall back from; all 71 registers the loop writes come
 * out exactly. Check mode (3) proves that per call (all 128 GPRs and the whole LS).
 *
 * GOW_EDGE_O719900 (read once):
 *   unset/0  lifted loop (the default)
 *   1        native
 *   3        check: run the native loop on a copy of the state, then the lifted loop, and compare all GPRs
 *            and the whole LS at the loop exit; `[edge-o719900-check] N calls checked, M differ` every 4096
 *            calls and at exit, with the calls per placement
 * GOW_EDGE_O719900_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 3 of the quad the last iteration stores at r91 + r34, 3 bit 0 of byte 0 of that quad, 2 bit 0 of r31
 * (a register the loop writes) after the loop.
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
#undef spu_rotqbii

static void go79_native(spu_context* ctx)
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

/* ---- self-test of the one op written here (rotqbii) against the runtime's, at mode init */
static uint32_t s79_rng = 0x2545F491u;
static uint32_t go79_rand(void) { s79_rng ^= s79_rng << 13; s79_rng ^= s79_rng >> 17; s79_rng ^= s79_rng << 5; return s79_rng; }
static void go79_selftest(void)
{
    long bad = 0;
    for (int i = 0; i < 80000; i++) {
        u128 a; for (int w = 0; w < 4; w++) a._u32[w] = go79_rand();
        const int sh = (int)(go79_rand() & 15);
        const u128 x = go79_rotqbii(a, sh), y = spu_rotqbii(a, sh);
        bad += memcmp(&x, &y, 16) != 0;
    }
    fprintf(stderr, "[edge-o719900] self-test: rotqbii %ld of 80000 quads differ\n", bad);
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s79_mode = -1;

int gow_edge_o719900_mode(void)
{
    if (s79_mode < 0) {
        const char* e = getenv("GOW_EDGE_O719900");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m != 1 && m != 3) m = 0;
        s79_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o719900] GOW_EDGE_O719900=%s (lifted loop)\n", e); }
        else {
            fprintf(stderr, "[edge-o719900] GOW_EDGE_O719900=%d (%s)\n", m, m == 1 ? "native" : "check");
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
    switch (s79_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[31]._u32[0] ^= 1; break;
    default: break;
    }
    fprintf(stderr, "[edge-o719900] fault %d injected (native run %ld, quad 0x%05X)\n", s79_fault, s79_fault_at, q);
    s79_fault = 0;
}

static void go79_run(spu_context* ctx)
{
    go79_native(ctx);
    if (__builtin_expect(s79_fault != 0, 0)) go79_fault(ctx);
}

/* Mode 1: replace the loop; the lift then runs the loop's exit. */
void gow_edge_o719900_loop(spu_context* ctx) { go79_run(ctx); }

/* Mode 3 (check): at the loop head run the native loop on the live state, keep its result, put the state
 * back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
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

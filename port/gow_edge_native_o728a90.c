/* gow_edge_native_o728a90.c -- native replacement for the loop of EDGE overlay blob 728a90 (input side of family C
 * in work/edgefrag_scope.md, a pure copy): the loop at blob+0x154, in every placement lift of the blob
 * (spu/edgefrag_728a90_<lsa>; 36 placements, one lift each). gen_edge_frag_lift.py puts a switch before the loop
 * in each of them (checking that each placement's loop is the reference 11a20's, statement for statement);
 * GOW_EDGE_O728A90 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native_o723300.c (-O2 -march=x86-64-v3, SPU_FP_NATIVE=1): the loop has no
 * float op, so nothing here depends on the MXCSR.
 *
 * Clean-room: written from our own lift of the fragment and our own captures.
 *
 * What the loop does (a reading of the lift, confirmed by running the reference placement's prologue and loop
 * on random LS contents in a model of the lifted ops): 2 vertices per iteration, r11 (and its copy r9 for the
 * stores) a negative byte offset stepping by 32 to 0, r64 = r11 + 32 the back-edge condition. The input is a
 * stream of 18-byte records at any alignment, read as four aligned quads per iteration two record pairs ahead
 * (r15 = the pair two ahead, +36 per iteration) and aligned by shuffles from (r15 & 15). The output is one slot
 * array of float4: iteration k stores two aligned quads at r8 + r9 and r10 + r9 (r10 = r8 + 16), each the
 * first 12 bytes of a record with word 3 from r17 (selb with the mask r21). Nothing else in the LS changes.
 *
 * v1: the loop statement for statement in C locals (GOW_O728_BODY, generated from the reference lift at build
 * time), as one C loop, with direct LS access. Every op is the lift's op on the same operands in the same
 * order, so it is exact for every input.
 *
 * v3 (block copy): iterations 0-2 run lifted from the entry state (their records come from what the prologue
 * loaded); records 3 .. n-2 are copied straight from the input (iteration k's pair from r15 + 36(k - 2), r15 at
 * entry, word 3 of each quad from r17); then the pointers go to iteration n-4 and the lifted body runs the
 * last four iterations, the first three without their stores (every register the loop writes then depends
 * only on those iterations' loads) and the last one with them. So all registers and the LS come out exactly as
 * the lift leaves them. Entry checks, else v1 (each counted): 8 .. 65536 iterations (r11 a negative multiple of
 * 32, r9 equal to it); the shuffle controls and masks r17 r18 r19 r20 r21 equal the captures' (the record
 * layout rests on them); r10 = r8 + 16 and the output start r8 + r11 16-byte aligned; input (the quads the n
 * iterations load) and output inside the LS without wrapping and apart from each other.
 *
 * GOW_EDGE_O728A90 (read once):
 *   unset/0  lifted loop (the default)
 *   1        native v3 (v1 for the entries it does not take)
 *   2        native v1
 *   3        check v3: run it on a copy of the state, then the lifted loop, and compare all GPRs and the whole LS
 *            at the loop exit; `[edge-o728a90-check] N calls checked, M differ` every 4096 calls and at exit,
 *            with the calls per placement
 *   4        check v1, the same way
 * GOW_EDGE_O728A90_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of byte 3
 * of the quad the last iteration stores at r8 + r9 (r9 = -32 there), 3 bit 0 of byte 0 of that quad, 2 bit 0 of
 * r68 (a register the loop writes) after the loop, 4 bit 0 of the first byte of record 3 (a copied one when v3
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
static inline __m128i go28_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 go28_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = go28_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void go28_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), go28_bswap32x4(x));
}
#define LD(a)    go28_ld(ls, (a))
#define ST(a, v) go28_st(ls, (a), (v))

/* the lifted loop: GOW_O728_*. Game code, so it is not in the repo: gen_edge_frag_lift.py --body writes it
 * from the reference lift at build time (no LS address of its own, straight code: checked there). */
#include "gow_edge_o728a90_body.h"

/* v1: the whole loop (back edge on r64) */
static void go28_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O728_REGS()
    GOW_O728_LOAD()
    do {
        GOW_O728_BODY()
    } while (r64._u32[0] != 0);
    GOW_O728_STORE()
}

/* the lifted body for `count` iterations, the first `nost` of them without their stores (v3: the first three
 * iterations with their stores; the warm-up, its registers only (the loads still read the LS), and the last
 * iteration with its stores). */
#undef ST
#define ST(a, v) do { if (i >= nost) go28_st(ls, (a), (v)); } while (0)
static void go28_iters(spu_context* ctx, uint32_t count, uint32_t nost)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O728_REGS()
    GOW_O728_LOAD()
    for (uint32_t i = 0; i < count; i++) {
        GOW_O728_BODY()
    }
    GOW_O728_STORE()
}
#undef ST
#define ST(a, v) go28_st(ls, (a), (v))

/* the controls and masks the record layout rests on: GOW_O728_CTL, {register, its four SPU words} as the
 * prologue leaves them. Game data, so not in the repo: gen_edge_frag_lift.py --consts computes them at build
 * time from the reference lift's prologue and the blob's bytes in the EBOOT. They are: r20 the input
 * alignment base, r19 the splat of the alignment byte, r18 the shuffle that picks a record's 12 bytes, r21
 * the word-3 mask and r17 the quad word 3 comes from. */
#include "gow_edge_o728a90_consts.h"
static const struct { uint8_t r; uint32_t w[4]; } go28_ctl[GOW_O728_NCTL] = GOW_O728_CTL;
#define GO28_NCTL GOW_O728_NCTL

#define GO28_NMIN 8u
#define GO28_NMAX 65536u
static long s28_fb_short;    /* entries with fewer than 8 iterations (or an implausible count): v1 */
static long s28_fb_ctl;      /* entries with other controls, masks or store pointers: v1 */
static long s28_fb_rng;      /* entries with input or output outside the LS, or an unaligned output: v1 */
static long s28_fb_ovl;      /* entries whose output overlaps the input: v1 */
static long s28_pk_recs;     /* records copied */
static long s28_v3_calls;    /* entries v3 took */
static uint64_t s28_cyc_pk;  /* tsc in the copied records (lab statistic) */
static uint32_t s28_last_s3; /* LS address of the last v3 call's record 3 (fault 4), or ~0 */

static inline u128 go28_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}

static void go28_v3(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    s28_last_s3 = ~0u;
    const uint32_t w11 = g[11]._u32[0];
    const uint32_t nb = 0u - w11, n = nb / 32u;
    if (!w11 || nb % 32u || n < GO28_NMIN || n > GO28_NMAX || g[9]._u32[0] != w11) { s28_fb_short++; go28_v1(ctx); return; }
    for (int i = 0; i < GO28_NCTL; i++)
        if (memcmp(g[go28_ctl[i].r]._u32, go28_ctl[i].w, 16)) { s28_fb_ctl++; go28_v1(ctx); return; }
    const uint32_t o = g[8]._u32[0];
    if (g[10]._u32[0] != o + 16u) { s28_fb_ctl++; go28_v1(ctx); return; }
    /* output: n pairs of quads up to r8 */
    if (o < nb || o > SPU_LS_SIZE || ((o - nb) & 15u)) { s28_fb_rng++; go28_v1(ctx); return; }
    const uint32_t s0 = o - nb;
    /* input: the quads the n iterations load (four from r15's quad on, r15 + 36 per iteration) */
    const uint32_t p = g[15]._u32[0];
    const uint64_t ilo = p & ~0xFu, ihi = (((uint64_t)p + 36u * (uint64_t)(n - 1u) + 48u) & ~(uint64_t)0xF) + 16u;
    if (p >= SPU_LS_SIZE || ihi > SPU_LS_SIZE) { s28_fb_rng++; go28_v1(ctx); return; }
    if (ilo < o && s0 < ihi) { s28_fb_ovl++; go28_v1(ctx); return; }
    s28_v3_calls++;
    /* iterations 0-2 lifted, from the entry state (records 0-2 come from what the prologue loaded) */
    go28_iters(ctx, 3, 0);
    /* records 3 .. n-2: each quad the record's first 12 bytes and word 3 of r17 */
    const uint64_t t0 = __rdtsc();
    {
        const int w = (int)__builtin_bswap32(g[17]._u32[3]);
        const uint8_t* s = ls + p + 36u;
        uint8_t* d = ls + s0 + 96u;
        for (uint32_t k = 3; k + 1 < n; k++, s += 36, d += 32) {
            _mm_storeu_si128((__m128i*)d, _mm_insert_epi32(_mm_loadu_si128((const __m128i*)s), w, 3));
            _mm_storeu_si128((__m128i*)(d + 16), _mm_insert_epi32(_mm_loadu_si128((const __m128i*)(s + 18)), w, 3));
        }
    }
    s28_cyc_pk += __rdtsc() - t0;
    s28_pk_recs += (long)n - 4;
    s28_last_s3 = s0 + 96u;
    /* the pointers to iteration n-4 (they are at 3), three lifted iterations without stores, the last one
     * with them */
    const uint32_t back = n - 7u;   /* n >= 8 */
    g[15] = go28_addw(g[15], 36u * back);
    g[11] = go28_addw(g[11], 32u * back);
    g[9] = g[11];
    go28_iters(ctx, 4, 3);
}

/* ---- self-test at mode init: whole loop runs, v3 against v1 (the lifted statements) on a scratch context,
 * all GPRs and the whole LS compared ---- */
static uint32_t s28_rng = 0x2545F491u;
static uint32_t go28_rand(void) { s28_rng ^= s28_rng << 13; s28_rng ^= s28_rng >> 17; s28_rng ^= s28_rng << 5; return s28_rng; }
static u128 go28_q(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static u128 go28_rq(void) { return go28_q(go28_rand(), go28_rand(), go28_rand(), go28_rand()); }
static void go28_selftest(void)
{
    long bad_run = 0, runs = 0, took = 0;
    const long sh0 = s28_fb_short, ct0 = s28_fb_ctl, rg0 = s28_fb_rng, ov0 = s28_fb_ovl, pk0 = s28_pk_recs, v30 = s28_v3_calls;
    const uint64_t cp0 = s28_cyc_pk;
    spu_context* ctx = (spu_context*)calloc(1, sizeof(spu_context));
    uint8_t* pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    uint8_t* ref_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    u128* pre_g = (u128*)malloc(sizeof(ctx->gpr));
    u128* ref_g = (u128*)malloc(sizeof(ctx->gpr));
    for (int it = 0; ctx && pre_ls && ref_ls && pre_g && ref_g && it < 800; it++) {
        ctx->ls = ctx->ls_store;
        for (uint32_t a = 0; a < SPU_LS_SIZE; a += 4) { const uint32_t v = go28_rand(); memcpy(ctx->ls + a, &v, 4); }
        u128* gg = ctx->gpr;
        for (int r = 0; r < 128; r++) gg[r] = go28_rq();
        for (int i = 0; i < GO28_NCTL; i++) memcpy(gg[go28_ctl[i].r]._u32, go28_ctl[i].w, 16);
        if (it % 50 == 49) gg[go28_ctl[go28_rand() % GO28_NCTL].r]._u32[go28_rand() & 3] ^= 1u << (go28_rand() & 31);
        const uint32_t n = (it % 7 == 0) ? 1u + go28_rand() % 12u : (it % 23 == 5) ? 1000u + go28_rand() % 2000u
                                                                     : 1u + go28_rand() % 90u;
        /* input records at 0x8000 + any residue; output at 0x20000 (16-aligned; one in 40 runs not; one in 30
         * runs over the input) */
        const uint32_t p0 = 0x8000u + (uint32_t)it % 16u + 16u * (go28_rand() % 64u);
        const uint32_t s0 = (it % 30 == 11) ? ((p0 + 36u * (go28_rand() % (n + 2u))) & ~15u)
                                            : 0x20000u + 16u * (go28_rand() % 8u) + ((it % 40 == 7) ? 4u : 0u);
        const uint32_t o = s0 + 32u * n;
        gg[8] = go28_q(o, go28_rand(), go28_rand(), go28_rand());
        gg[10] = go28_q(o + 16u, go28_rand(), go28_rand(), go28_rand());
        if (it % 60 == 23) gg[10]._u32[0] += 16u;   /* a store pointer off: v1 */
        gg[15] = go28_q(p0 + 72u, go28_rand(), go28_rand(), go28_rand());
        gg[11] = go28_q(0u - 32u * n, go28_rand(), go28_rand(), go28_rand());
        gg[9] = (it & 4) ? gg[11] : go28_q(0u - 32u * n, 0, 0, 0);
        if (it % 70 == 31) gg[9]._u32[0] += 32u;   /* r9 off: v1 */
        memcpy(pre_ls, ctx->ls, SPU_LS_SIZE); memcpy(pre_g, gg, sizeof(ctx->gpr));
        go28_v1(ctx);
        memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); memcpy(ref_g, gg, sizeof(ctx->gpr));
        memcpy(ctx->ls, pre_ls, SPU_LS_SIZE); memcpy(gg, pre_g, sizeof(ctx->gpr));
        const long t0 = s28_v3_calls;
        go28_v3(ctx);
        took += s28_v3_calls - t0;
        runs++;
        bad_run += memcmp(ctx->ls, ref_ls, SPU_LS_SIZE) != 0 || memcmp(gg, ref_g, sizeof(ctx->gpr)) != 0;
    }
    free(pre_ls); free(ref_ls); free(pre_g); free(ref_g); free(ctx);
    fprintf(stderr, "[edge-o728a90] self-test: whole loop runs, v3 against v1: %ld of %ld differ (%ld taken by v3, %ld "
                    "records copied; left to v1: %ld short, %ld controls, %ld range, %ld overlap)\n",
            bad_run, runs, took, s28_pk_recs - pk0, s28_fb_short - sh0, s28_fb_ctl - ct0, s28_fb_rng - rg0, s28_fb_ovl - ov0);
    s28_fb_short = sh0; s28_fb_ctl = ct0; s28_fb_rng = rg0; s28_fb_ovl = ov0; s28_pk_recs = pk0;
    s28_v3_calls = v30; s28_cyc_pk = cp0;
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s28_mode = -1;

int gow_edge_o728a90_mode(void)
{
    if (s28_mode < 0) {
        const char* e = getenv("GOW_EDGE_O728A90");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        s28_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o728a90] GOW_EDGE_O728A90=%s (lifted loop)\n", e); }
        else {
            fprintf(stderr, "[edge-o728a90] GOW_EDGE_O728A90=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : "v3");
            go28_selftest();
        }
    }
    return s28_mode;
}

static int s28_fault = -1;
static long s28_fault_at, s28_runs;
static void go28_fault(spu_context* ctx)
{
    if (s28_fault < 0) {
        const char* f = getenv("GOW_EDGE_O728A90_FAULT");
        s28_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s28_fault_at = k ? atol(k + 1) : 0;
    }
    if (s28_fault <= 0 || s28_runs++ != s28_fault_at) return;
    /* the last iteration stores at r8 + r9 with r9 = -32 (r8 is not written) */
    const uint32_t q = (ctx->gpr[8]._u32[0] - 32u) & (SPU_LS_MASK & ~0xFu);
    uint32_t at = q;
    switch (s28_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[68]._u32[0] ^= 1; break;
    case 4:
        at = s28_last_s3 != ~0u ? s28_last_s3 : 0u;
        ctx->ls[at & SPU_LS_MASK] ^= 1;
        break;
    default: break;
    }
    fprintf(stderr, "[edge-o728a90] fault %d injected (native run %ld, LS 0x%05X%s)\n", s28_fault, s28_fault_at, at,
            s28_fault == 4 && s28_last_s3 == ~0u ? ": the call was not copied by v3" : "");
    s28_fault = 0;
}

static void go28_run(spu_context* ctx)
{
    if (s28_mode == 2 || s28_mode == 4) { s28_last_s3 = ~0u; go28_v1(ctx); }
    else                                go28_v3(ctx);
    if (__builtin_expect(s28_fault != 0, 0)) go28_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then runs the loop's exit. */
void gow_edge_o728a90_loop(spu_context* ctx) { go28_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} go28_check_state;
static _Thread_local go28_check_state* t28_chk;

static long s28_calls, s28_bad, s28_iters;
static uint64_t s28_cyc_nat, s28_cyc_lift;
static int s28_atexit;

/* Calls checked per placement (the blob's LS address the generated switch passes). */
#define GO28_SITES 48
static unsigned s28_site_lsa[GO28_SITES];
static long s28_site_calls[GO28_SITES], s28_site_bad[GO28_SITES];
static _Thread_local unsigned t28_site;

static void go28_check_report(void)
{
    char buf[GO28_SITES * 28] = "", *p = buf;
    for (int i = 0; i < GO28_SITES && s28_site_calls[i]; i++)
        p += snprintf(p, sizeof(buf) - (size_t)(p - buf), " %X:%ld/%ld", s28_site_lsa[i], s28_site_calls[i],
                      s28_site_bad[i]);
    fprintf(stderr, "[edge-o728a90-check] calls by placement (blob LS address:calls/differ):%s\n", buf);
    fprintf(stderr, "[edge-o728a90-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); tsc: "
                    "native %.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s28_calls, s28_bad, s28_iters, s28_cyc_nat / 1e6, s28_cyc_lift / 1e6,
            s28_iters ? (double)s28_cyc_nat / (double)s28_iters : 0.0,
            s28_iters ? (double)s28_cyc_lift / (double)s28_iters : 0.0);
    fprintf(stderr, "[edge-o728a90-check] v3 took %ld calls, %ld records copied; entries left to v1: %ld short, %ld "
                    "other controls or store pointers, %ld outside the LS or unaligned, %ld overlapping; tsc in the "
                    "copied records %.1f M (%.1f per record)\n",
            s28_v3_calls, s28_pk_recs, s28_fb_short, s28_fb_ctl, s28_fb_rng, s28_fb_ovl,
            s28_cyc_pk / 1e6, s28_pk_recs ? (double)s28_cyc_pk / (double)s28_pk_recs : 0.0);
}

void gow_edge_o728a90_check_begin_at(spu_context* ctx, unsigned site)
{
    go28_check_state* c = t28_chk;
    if (!c) {
        c = (go28_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t28_chk = c;
    }
    if (!s28_atexit) { s28_atexit = 1; atexit(go28_check_report); }
    t28_site = site;
    s28_iters += (long)((0u - ctx->gpr[11]._u32[0]) / 32u);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    go28_run(ctx);
    s28_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_o728a90_check_end(spu_context* ctx)
{
    go28_check_state* c = t28_chk;
    if (!c || !c->pending) return;
    s28_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s28_calls++;
    int site = -1;
    for (int i = 0; i < GO28_SITES; i++) {   /* matching or first free slot (not atomic, like s28_calls) */
        if (s28_site_calls[i] && s28_site_lsa[i] != t28_site) continue;
        s28_site_lsa[i] = t28_site;
        s28_site_calls[i]++;
        site = i;
        break;
    }
    t28_site = 0;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-o728a90-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-o728a90-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s28_bad++;
        if (site >= 0) s28_site_bad[site]++;
        fprintf(stderr, "[edge-o728a90-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) go28_check_report();   /* a game run ends by kill: atexit never runs */
}

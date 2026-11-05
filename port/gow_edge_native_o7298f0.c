/* gow_edge_native_o7298f0.c -- native replacement for the loop of EDGE overlay blob 7298f0 (output side of family C
 * in work/edgefrag_scope.md, a pure copy): the loop at blob+0xD0, in every placement lift of the blob
 * (spu/edgefrag_7298f0_<lsa>; 38 placements, one lift each). gen_edge_frag_lift.py puts a switch before the loop
 * in each of them (checking that each placement's loop is the reference 11530's, statement for statement);
 * GOW_EDGE_O7298F0 picks the path. See docs/EDGE_NATIVE.md.
 *
 * Built as an SPU source like gow_edge_native_o723300.c (-O2 -march=x86-64-v3, SPU_FP_NATIVE=1): the loop has no
 * float op, so nothing here depends on the MXCSR.
 *
 * Clean-room: written from our own lift of the fragment and our own captures.
 *
 * What the loop does (a reading of the lift, confirmed by running the reference placement's prologue and loop
 * on random LS contents in a model of the lifted ops): 2 vertices per iteration, r14 (and its copy r10 for the
 * stores) a negative byte offset stepping by 24 to 0, r43 = r14 + 24 the back-edge condition. The input is one
 * slot array of float4 (16 bytes per vertex), read as aligned pairs of quads one pair per iteration and three
 * pairs ahead (r12 = the pair three ahead, +32 per iteration). The output is the x y z of each vertex packed:
 * iteration k writes record k (24 bytes, the first 12 bytes of each of the pair's two quads) at S_k = r23 + r14
 * (r14 at entry) + 24k, any alignment, as three aligned quads at r21 r20 r19 (= r23 + 0, 16, 32) + r10. The
 * first quad keeps the bytes before S_k (r17, the quad at r22 + r10 = S_k's quad, read back after the previous
 * iteration's stores); the bytes after the record are shuffle leftovers the next iteration overwrites, so after
 * the loop the bytes from the end of the last record to the end of the last stored quad are the last
 * iteration's leftovers.
 *
 * v1: the loop statement for statement in C locals (GOW_O729_BODY, generated from the reference lift at build
 * time), as one C loop, with direct LS access. Every op is the lift's op on the same operands in the same
 * order, so it is exact for every input.
 *
 * v3 (block copy): iterations 0-2 run lifted from the entry state (their records come from what the prologue
 * loaded); records 3 .. n-2 are copied straight from the input (record k from the pair at r12 + 32(k - 3),
 * r12 at entry); then the pointers go to iteration n-5 and the lifted body runs the last five iterations, the
 * first three without their stores (every register the loop writes then depends only on those iterations'
 * loads, or is carried unchanged from iteration 2) and the last two with them (they rewrite their records, the
 * leftovers after the last one, and read back what the lift reads back). So all registers and the LS come out
 * exactly as the lift leaves them. Entry checks, else v1 (each counted): 8 .. 65536 iterations (r14 a negative
 * multiple of 24, r10 equal to it); the shuffle controls and masks r8 r18 r24 r25 r26 equal the captures' (the
 * record layout rests on them); the store pointers r21 r20 r19 r22 at r23 + 0, 16, 32, 24; r12 16-byte
 * aligned; input (the n pairs from r12) and output (S_0's quad to the end of the last stored quad) inside the
 * LS without wrapping and apart from each other.
 *
 * GOW_EDGE_O7298F0 (read once):
 *   unset/0  lifted loop (the default)
 *   1        native v3 (v1 for the entries it does not take)
 *   2        native v1
 *   3        check v3: run it on a copy of the state, then the lifted loop, and compare all GPRs and the whole LS
 *            at the loop exit; `[edge-o7298f0-check] N calls checked, M differ` every 4096 calls and at exit,
 *            with the calls per placement
 *   4        check v1, the same way
 * GOW_EDGE_O7298F0_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of byte 3
 * of the quad the last iteration stores at r21 + r10 (r10 = -24 there), 3 bit 0 of byte 0 of that quad, 2 bit 0
 * of r39 (a register the loop writes) after the loop, 4 bit 0 of the first byte of record 3 (a copied one when
 * v3 took the call).
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
static inline __m128i go29_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 go29_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = go29_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void go29_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), go29_bswap32x4(x));
}
#define LD(a)    go29_ld(ls, (a))
#define ST(a, v) go29_st(ls, (a), (v))

/* the lifted loop: GOW_O729_*. Game code, so it is not in the repo: gen_edge_frag_lift.py --body writes it
 * from the reference lift at build time (no LS address of its own, straight code: checked there). */
#include "gow_edge_o7298f0_body.h"

/* v1: the whole loop (back edge on r43) */
static void go29_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O729_REGS()
    GOW_O729_LOAD()
    do {
        GOW_O729_BODY()
    } while (r43._u32[0] != 0);
    GOW_O729_STORE()
}

/* the lifted body for `count` iterations, the first `nost` of them without their stores (v3: the first three
 * iterations with their stores; the warm-up, its registers only (the loads still read the LS), and the last
 * two iterations with their stores). */
#undef ST
#define ST(a, v) do { if (i >= nost) go29_st(ls, (a), (v)); } while (0)
static void go29_iters(spu_context* ctx, uint32_t count, uint32_t nost)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_O729_REGS()
    GOW_O729_LOAD()
    for (uint32_t i = 0; i < count; i++) {
        GOW_O729_BODY()
    }
    GOW_O729_STORE()
}
#undef ST
#define ST(a, v) go29_st(ls, (a), (v))

/* the controls and masks the record layout rests on: GOW_O729_CTL, {register, its four SPU words} as the
 * prologue leaves them. Game data, so not in the repo: gen_edge_frag_lift.py --consts computes them at build
 * time from the reference lift's prologue and the blob's bytes in the EBOOT. They are: r18 the output
 * rotation base, r25 the splat of the alignment byte, r8 r24 the shuffles that pick x y z out of the pair's
 * quads, r26 a byte mask. */
#include "gow_edge_o7298f0_consts.h"
static const struct { uint8_t r; uint32_t w[4]; } go29_ctl[GOW_O729_NCTL] = GOW_O729_CTL;
#define GO29_NCTL GOW_O729_NCTL
/* the store and read-back pointers: r23 + this (word 0) */
static const struct { uint8_t r; uint32_t d; } go29_out[] = { { 21, 0 }, { 20, 16 }, { 19, 32 }, { 22, 24 } };

#define GO29_NMIN 8u
#define GO29_NMAX 65536u
static long s29_fb_short;    /* entries with fewer than 8 iterations (or an implausible count): v1 */
static long s29_fb_ctl;      /* entries with other controls, masks or store pointers: v1 */
static long s29_fb_rng;      /* entries with input or output outside the LS, or an unaligned input: v1 */
static long s29_fb_ovl;      /* entries whose output overlaps the input: v1 */
static long s29_pk_recs;     /* records copied */
static long s29_v3_calls;    /* entries v3 took */
static uint64_t s29_cyc_pk;  /* tsc in the copied records (lab statistic) */
static uint32_t s29_last_s3; /* LS address of the last v3 call's record 3 (fault 4), or ~0 */

static inline u128 go29_addw(u128 x, uint32_t v)   /* spu_ai repeated: every word + v */
{
    for (int i = 0; i < 4; i++) x._u32[i] += v;
    return x;
}

static void go29_v3(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    s29_last_s3 = ~0u;
    const uint32_t w14 = g[14]._u32[0];
    const uint32_t nb = 0u - w14, n = nb / 24u;
    if (!w14 || nb % 24u || n < GO29_NMIN || n > GO29_NMAX || g[10]._u32[0] != w14) { s29_fb_short++; go29_v1(ctx); return; }
    for (int i = 0; i < GO29_NCTL; i++)
        if (memcmp(g[go29_ctl[i].r]._u32, go29_ctl[i].w, 16)) { s29_fb_ctl++; go29_v1(ctx); return; }
    const uint32_t o = g[23]._u32[0];
    for (int i = 0; i < (int)(sizeof(go29_out) / sizeof(go29_out[0])); i++)
        if (g[go29_out[i].r]._u32[0] != o + go29_out[i].d) { s29_fb_ctl++; go29_v1(ctx); return; }
    /* output: from S_0's quad to the end of the last iteration's third quad */
    if (o < nb || o >= SPU_LS_SIZE) { s29_fb_rng++; go29_v1(ctx); return; }
    const uint32_t s0 = o - nb;
    const uint64_t olo = s0 & ~0xFu, ohi = (((uint64_t)s0 + 24u * (uint64_t)(n - 1u) + 32u) & ~(uint64_t)0xF) + 16u;
    if (ohi > SPU_LS_SIZE) { s29_fb_rng++; go29_v1(ctx); return; }
    /* input: the pairs the n iterations load, from r12 (pair 3) on */
    const uint32_t p = g[12]._u32[0];
    const uint64_t phi = (uint64_t)p + 32u * (uint64_t)n;
    if ((p & 15u) || p >= SPU_LS_SIZE || phi > SPU_LS_SIZE) { s29_fb_rng++; go29_v1(ctx); return; }
    if (p < ohi && olo < phi) { s29_fb_ovl++; go29_v1(ctx); return; }
    s29_v3_calls++;
    /* iterations 0-2 lifted, from the entry state (records 0-2 come from what the prologue loaded) */
    go29_iters(ctx, 3, 0);
    /* records 3 .. n-2: x y z of each quad of the pair. 16-byte moves; the 4 bytes after each half are
     * overwritten by the next half or record, after record n-2 by the lifted iteration n-2. */
    const uint64_t t0 = __rdtsc();
    {
        const uint8_t* s = ls + p;
        uint8_t* d = ls + s0 + 72u;
        for (uint32_t k = 3; k + 1 < n; k++, s += 32, d += 24) {
            _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s));
            _mm_storeu_si128((__m128i*)(d + 12), _mm_loadu_si128((const __m128i*)(s + 16)));
        }
    }
    s29_cyc_pk += __rdtsc() - t0;
    s29_pk_recs += (long)n - 4;
    s29_last_s3 = s0 + 72u;
    /* the pointers to iteration n-5 (they are at 3), three lifted iterations without stores, the last two
     * with them */
    const uint32_t back = n - 8u;   /* n >= 8 */
    g[12] = go29_addw(g[12], 32u * back);
    g[14] = go29_addw(g[14], 24u * back);
    g[10] = g[14];
    go29_iters(ctx, 5, 3);
}

/* ---- self-test at mode init: whole loop runs, v3 against v1 (the lifted statements) on a scratch context,
 * all GPRs and the whole LS compared ---- */
static uint32_t s29_rng = 0x2545F491u;
static uint32_t go29_rand(void) { s29_rng ^= s29_rng << 13; s29_rng ^= s29_rng >> 17; s29_rng ^= s29_rng << 5; return s29_rng; }
static u128 go29_q(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static u128 go29_rq(void) { return go29_q(go29_rand(), go29_rand(), go29_rand(), go29_rand()); }
static void go29_selftest(void)
{
    long bad_run = 0, runs = 0, took = 0;
    const long sh0 = s29_fb_short, ct0 = s29_fb_ctl, rg0 = s29_fb_rng, ov0 = s29_fb_ovl, pk0 = s29_pk_recs, v30 = s29_v3_calls;
    const uint64_t cp0 = s29_cyc_pk;
    spu_context* ctx = (spu_context*)calloc(1, sizeof(spu_context));
    uint8_t* pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    uint8_t* ref_ls = (uint8_t*)malloc(SPU_LS_SIZE);
    u128* pre_g = (u128*)malloc(sizeof(ctx->gpr));
    u128* ref_g = (u128*)malloc(sizeof(ctx->gpr));
    for (int it = 0; ctx && pre_ls && ref_ls && pre_g && ref_g && it < 800; it++) {
        ctx->ls = ctx->ls_store;
        for (uint32_t a = 0; a < SPU_LS_SIZE; a += 4) { const uint32_t v = go29_rand(); memcpy(ctx->ls + a, &v, 4); }
        u128* gg = ctx->gpr;
        for (int r = 0; r < 128; r++) gg[r] = go29_rq();
        for (int i = 0; i < GO29_NCTL; i++) memcpy(gg[go29_ctl[i].r]._u32, go29_ctl[i].w, 16);
        if (it % 50 == 49) gg[go29_ctl[go29_rand() % GO29_NCTL].r]._u32[go29_rand() & 3] ^= 1u << (go29_rand() & 31);
        const uint32_t n = (it % 7 == 0) ? 1u + go29_rand() % 12u : (it % 23 == 5) ? 1000u + go29_rand() % 2000u
                                                                     : 1u + go29_rand() % 90u;
        /* input pairs at 0x8000 (16-aligned; one in 40 runs not); output at 0x20000 + any residue (one in 30
         * runs over the input) */
        const uint32_t p0 = 0x8000u + 16u * (go29_rand() % 64u) + ((it % 40 == 7) ? 4u * (1u + go29_rand() % 3u) : 0u);
        const uint32_t s0 = (it % 30 == 11) ? p0 + 32u * (go29_rand() % (n + 3u)) + (go29_rand() & 15u)
                                            : 0x20000u + (uint32_t)it % 16u + 16u * (go29_rand() % 8u);
        const uint32_t o = s0 + 24u * n;
        gg[23] = go29_q(o, go29_rand(), go29_rand(), go29_rand());
        for (int i = 0; i < (int)(sizeof(go29_out) / sizeof(go29_out[0])); i++)
            gg[go29_out[i].r] = go29_q(o + go29_out[i].d, go29_rand(), go29_rand(), go29_rand());
        if (it % 60 == 23) gg[19]._u32[0] += 16u;   /* a store pointer off: v1 */
        gg[12] = go29_q(p0 + 96u, go29_rand(), go29_rand(), go29_rand());
        gg[14] = go29_q(0u - 24u * n, go29_rand(), go29_rand(), go29_rand());
        gg[10] = (it & 4) ? gg[14] : go29_q(0u - 24u * n, 0, 0, 0);
        if (it % 70 == 31) gg[10]._u32[0] += 24u;   /* r10 off: v1 */
        memcpy(pre_ls, ctx->ls, SPU_LS_SIZE); memcpy(pre_g, gg, sizeof(ctx->gpr));
        go29_v1(ctx);
        memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); memcpy(ref_g, gg, sizeof(ctx->gpr));
        memcpy(ctx->ls, pre_ls, SPU_LS_SIZE); memcpy(gg, pre_g, sizeof(ctx->gpr));
        const long t0 = s29_v3_calls;
        go29_v3(ctx);
        took += s29_v3_calls - t0;
        runs++;
        bad_run += memcmp(ctx->ls, ref_ls, SPU_LS_SIZE) != 0 || memcmp(gg, ref_g, sizeof(ctx->gpr)) != 0;
    }
    free(pre_ls); free(ref_ls); free(pre_g); free(ref_g); free(ctx);
    fprintf(stderr, "[edge-o7298f0] self-test: whole loop runs, v3 against v1: %ld of %ld differ (%ld taken by v3, %ld "
                    "records copied; left to v1: %ld short, %ld controls, %ld range, %ld overlap)\n",
            bad_run, runs, took, s29_pk_recs - pk0, s29_fb_short - sh0, s29_fb_ctl - ct0, s29_fb_rng - rg0, s29_fb_ovl - ov0);
    s29_fb_short = sh0; s29_fb_ctl = ct0; s29_fb_rng = rg0; s29_fb_ovl = ov0; s29_pk_recs = pk0;
    s29_v3_calls = v30; s29_cyc_pk = cp0;
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s29_mode = -1;

int gow_edge_o7298f0_mode(void)
{
    if (s29_mode < 0) {
        const char* e = getenv("GOW_EDGE_O7298F0");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        s29_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o7298f0] GOW_EDGE_O7298F0=%s (lifted loop)\n", e); }
        else {
            fprintf(stderr, "[edge-o7298f0] GOW_EDGE_O7298F0=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : "v3");
            go29_selftest();
        }
    }
    return s29_mode;
}

static int s29_fault = -1;
static long s29_fault_at, s29_runs;
static void go29_fault(spu_context* ctx)
{
    if (s29_fault < 0) {
        const char* f = getenv("GOW_EDGE_O7298F0_FAULT");
        s29_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        s29_fault_at = k ? atol(k + 1) : 0;
    }
    if (s29_fault <= 0 || s29_runs++ != s29_fault_at) return;
    /* the last iteration stores at r21 + r10 with r10 = -24 (r21 is not written) */
    const uint32_t q = (ctx->gpr[21]._u32[0] - 24u) & (SPU_LS_MASK & ~0xFu);
    uint32_t at = q;
    switch (s29_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[39]._u32[0] ^= 1; break;
    case 4:
        at = s29_last_s3 != ~0u ? s29_last_s3 : 0u;
        ctx->ls[at & SPU_LS_MASK] ^= 1;
        break;
    default: break;
    }
    fprintf(stderr, "[edge-o7298f0] fault %d injected (native run %ld, LS 0x%05X%s)\n", s29_fault, s29_fault_at, at,
            s29_fault == 4 && s29_last_s3 == ~0u ? ": the call was not copied by v3" : "");
    s29_fault = 0;
}

static void go29_run(spu_context* ctx)
{
    if (s29_mode == 2 || s29_mode == 4) { s29_last_s3 = ~0u; go29_v1(ctx); }
    else                                go29_v3(ctx);
    if (__builtin_expect(s29_fault != 0, 0)) go29_fault(ctx);
}

/* Modes 1 and 2: replace the loop; the lift then runs the loop's exit. */
void gow_edge_o7298f0_loop(spu_context* ctx) { go29_run(ctx); }

/* Modes 3 and 4 (check): at the loop head run the native loop on the live state, keep its result, put the
 * state back and let the lifted loop run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} go29_check_state;
static _Thread_local go29_check_state* t29_chk;

static long s29_calls, s29_bad, s29_iters;
static uint64_t s29_cyc_nat, s29_cyc_lift;
static int s29_atexit;

/* Calls checked per placement (the blob's LS address the generated switch passes). */
#define GO29_SITES 48
static unsigned s29_site_lsa[GO29_SITES];
static long s29_site_calls[GO29_SITES], s29_site_bad[GO29_SITES];
static _Thread_local unsigned t29_site;

static void go29_check_report(void)
{
    char buf[GO29_SITES * 28] = "", *p = buf;
    for (int i = 0; i < GO29_SITES && s29_site_calls[i]; i++)
        p += snprintf(p, sizeof(buf) - (size_t)(p - buf), " %X:%ld/%ld", s29_site_lsa[i], s29_site_calls[i],
                      s29_site_bad[i]);
    fprintf(stderr, "[edge-o7298f0-check] calls by placement (blob LS address:calls/differ):%s\n", buf);
    fprintf(stderr, "[edge-o7298f0-check] %ld calls checked, %ld differ; %ld iterations (2 vertices each); tsc: "
                    "native %.1f M, lifted %.1f M (%.1f vs %.1f per iteration)\n",
            s29_calls, s29_bad, s29_iters, s29_cyc_nat / 1e6, s29_cyc_lift / 1e6,
            s29_iters ? (double)s29_cyc_nat / (double)s29_iters : 0.0,
            s29_iters ? (double)s29_cyc_lift / (double)s29_iters : 0.0);
    fprintf(stderr, "[edge-o7298f0-check] v3 took %ld calls, %ld records copied; entries left to v1: %ld short, %ld "
                    "other controls or store pointers, %ld outside the LS or unaligned, %ld overlapping; tsc in the "
                    "copied records %.1f M (%.1f per record)\n",
            s29_v3_calls, s29_pk_recs, s29_fb_short, s29_fb_ctl, s29_fb_rng, s29_fb_ovl,
            s29_cyc_pk / 1e6, s29_pk_recs ? (double)s29_cyc_pk / (double)s29_pk_recs : 0.0);
}

void gow_edge_o7298f0_check_begin_at(spu_context* ctx, unsigned site)
{
    go29_check_state* c = t29_chk;
    if (!c) {
        c = (go29_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        t29_chk = c;
    }
    if (!s29_atexit) { s29_atexit = 1; atexit(go29_check_report); }
    t29_site = site;
    s29_iters += (long)((0u - ctx->gpr[14]._u32[0]) / 24u);
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    go29_run(ctx);
    s29_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_o7298f0_check_end(spu_context* ctx)
{
    go29_check_state* c = t29_chk;
    if (!c || !c->pending) return;
    s29_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = s29_calls++;
    int site = -1;
    for (int i = 0; i < GO29_SITES; i++) {   /* matching or first free slot (not atomic, like s29_calls) */
        if (s29_site_calls[i] && s29_site_lsa[i] != t29_site) continue;
        s29_site_lsa[i] = t29_site;
        s29_site_calls[i]++;
        site = i;
        break;
    }
    t29_site = 0;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-o7298f0-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-o7298f0-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        s29_bad++;
        if (site >= 0) s29_site_bad[site]++;
        fprintf(stderr, "[edge-o7298f0-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) go29_check_report();   /* a game run ends by kill: atexit never runs */
}

/* gow_edge_native_o71c310.c -- native replacement (v1) for the loop of EDGE overlay blob 71c310 (input decode,
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
 * What the loop does (a reading of the lift, not verified field by field; 2 vertices per iteration, r35 a
 * negative byte offset stepping by 32 to 0, r32 = r35 + 32 the back-edge condition, records at r44 + 24 per
 * iteration): both family-A building blocks in one loop. A fixed-point part as 7228c0's (and, cuflt, csflt,
 * fa: float(y >> k) + float(frac) with per-component counts and biases the prologue reads from the stream
 * descriptor) stored to the slot at r78 / r74, and a packed unit-vector part as 71fa90's (fma, squares,
 * adjacent sum, fs, frsqest, fi, fm, placed by table-driven shufb controls) stored to the slots at r65 r75
 * r69 r76 one iteration later.
 *
 * v1: the loop statement for statement in C locals (GOW_O71C_BODY, generated from the reference lift at
 * build time), as one C loop, with direct LS access. Every op is the lift's op (the same inline helpers) on
 * the same operands in the same order, so it is exact for every input. No packed version yet: the
 * fixed-point part's shift counts and biases come from the descriptor per call, so a packed version needs
 * the extraction rebuilt from the live registers (as gow_edge_native_o11430.c does).
 *
 * GOW_EDGE_O71C310 (read once):
 *   unset/0  lifted loop (the default)
 *   1, 2     native v1
 *   3, 4     check v1: run it on a copy of the state, then the lifted loop, and compare all GPRs and the whole
 *            LS at the loop exit; `[edge-o71c310-check] N calls checked, M differ` every 4096 calls and at
 *            exit, with the calls per placement
 * GOW_EDGE_O71C310_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 3 of the quad the last iteration stores at r78 + r35 (r35 = -32 there), 3 bit 0 of byte 0 of that
 * quad, 2 bit 0 of r39 (a register the loop writes) after the loop.
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

/* ---- entry points the lift calls ---------------------------------------------------- */
static int s7c_mode = -1;

int gow_edge_o71c310_mode(void)
{
    if (s7c_mode < 0) {
        const char* e = getenv("GOW_EDGE_O71C310");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted loop */
        if (m < 0 || m > 4) m = 0;
        s7c_mode = m;
        if (!m) { if (e && *e) fprintf(stderr, "[edge-o71c310] GOW_EDGE_O71C310=%s (lifted loop)\n", e); }
        else fprintf(stderr, "[edge-o71c310] GOW_EDGE_O71C310=%d (%s v1)\n", m, m <= 2 ? "native" : "check");
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
    switch (s7c_fault) {
    case 1: ctx->ls[q + 3] ^= 1; break;
    case 3: ctx->ls[q + 0] ^= 1; break;
    case 2: ctx->gpr[39]._u32[0] ^= 1; break;
    default: break;
    }
    fprintf(stderr, "[edge-o71c310] fault %d injected (native run %ld, LS 0x%05X)\n", s7c_fault, s7c_fault_at, q);
    s7c_fault = 0;
}

static void g7c_run(spu_context* ctx)
{
    g7c_v1(ctx);
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

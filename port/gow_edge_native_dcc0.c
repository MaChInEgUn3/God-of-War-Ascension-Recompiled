/* gow_edge_native_dcc0.c -- native replacement for the hot region of the EDGE job module's (d935, lift
 * spu/jobmod_d93503647dd8_e6d98) function DCC0: from loc_0000E210 to the back edge of loop E3C4, i.e. the
 * loops E210, E30C and E3C4 with the two straight-line blocks between them (exit: the fall-through after
 * E3C4's back edge; the three statements after it and the tail call to B600 stay lifted). The d935 lift is
 * built through gen_edge_native_lift.py, which puts a switch before loc_0000E210; GOW_EDGE_DCC0 picks the
 * path. See docs/EDGE_NATIVE.md.
 *
 * What the region does (a reading of the lift): integer work only. Each iteration of E210 and E3C4 loads
 * from addresses that depend on the previous iteration's loads (cntb-driven variable-length steps, shuffle
 * controls from LS tables indexed by the loaded data), so the iterations cannot be packed: this stage runs
 * the lifted statements one iteration at a time, registers in C locals (the lift keeps them in ctx->gpr,
 * which every LS store may alias, so it reloads them after each store), direct LS access, and, in the
 * native version, two byte helpers with fewer host ops than the shared ones (shufb, fsmb), compared with the
 * shared ones by a self-test at mode init.
 *
 * Built as an SPU source like gow_edge_native.c (-O2 -march=x86-64-v3). Integer only: no MXCSR dependence.
 *
 * Clean-room: written from our own lift of the module and our own captures.
 *
 * GOW_EDGE_DCC0 (read once):
 *   0/unset  lifted region (default)
 *   1        native: the lifted statements in C locals with the leaner shufb/fsmb helpers
 *   2        native v1: the same with the shared spu_* helpers
 *   3        check 1: run it on a copy of the state, then the lifted region, and compare all GPRs and the
 *            whole LS at the region exit; tsc per call native vs lifted
 *   4        check 2, the same way
 * GOW_EDGE_DCC0_FAULT=<n>[,<k>] (negative test, native run k only, default the first): 1 flips bit 0 of
 * byte 15 of the first quad the last E3C4 iteration stores, 2 bit 0 of r12 after the region.
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
static inline __m128i gdc_bswap32x4(__m128i x)
{
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
}
static inline u128 gdc_ld(const uint8_t* ls, uint32_t lsa)
{
    const __m128i x = gdc_bswap32x4(_mm_loadu_si128((const __m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu)))));
    u128 r; memcpy(&r, &x, 16); return r;
}
static inline void gdc_st(uint8_t* ls, uint32_t lsa, u128 v)
{
    __m128i x; memcpy(&x, &v, 16);
    _mm_storeu_si128((__m128i*)(ls + (lsa & (SPU_LS_MASK & ~0xFu))), gdc_bswap32x4(x));
}
#define LD(a)    gdc_ld(ls, (a))
#define ST(a, v) gdc_st(ls, (a), (v))

/* ---- leaner byte helpers (same results as spu_shufb / spu_fsmb; the self-test compares them) ----
 * shufb: SPU byte P lives at host byte P^3 (see spu_helpers.h), and the control byte for result byte t sits
 * where that result byte goes, so the source index is (sel ^ 3): pshufb reads only its low 4 bits (and
 * zeroes on bit 7, which the special case overrides anyway). Bit 4 picks b: shifted to bit 7 it is the blend
 * mask. The special selectors (bit 7 set) map their top three bits 100/101/110/111 to 00/00/FF/80. */
static inline u128 gdc_shufb(u128 a, u128 b, u128 c)
{
    __m128i A, B, C; memcpy(&A, &a, 16); memcpy(&B, &b, 16); memcpy(&C, &c, 16);
    const __m128i idx = _mm_xor_si128(C, _mm_set1_epi8(3));
    __m128i r = _mm_blendv_epi8(_mm_shuffle_epi8(A, idx), _mm_shuffle_epi8(B, idx), _mm_slli_epi16(C, 3));
    const __m128i top = _mm_and_si128(_mm_srli_epi16(C, 5), _mm_set1_epi8(7));
    const __m128i sp = _mm_shuffle_epi8(_mm_setr_epi8(0, 0, 0, 0, 0, 0, (char)0xFF, (char)0x80,
                                                      0, 0, 0, 0, 0, 0, 0, 0), top);
    r = _mm_blendv_epi8(r, sp, C);
    u128 out; memcpy(&out, &r, 16); return out;
}
/* fsmb: the low 16 bits of word 0, MSB first, one bit per SPU byte. Host byte h is SPU byte p = h^3: its
 * bit is in the high byte of the halfword for p < 8 (host bytes 0-7) and in the low byte for p >= 8. */
static inline u128 gdc_fsmb(u128 a)
{
    const __m128i v = _mm_shuffle_epi8(_mm_cvtsi32_si128((int)(a._u32[0] & 0xFFFFu)),
                                       _mm_setr_epi8(1,1,1,1, 1,1,1,1, 0,0,0,0, 0,0,0,0));
    const __m128i m = _mm_setr_epi8(0x10,0x20,0x40,(char)0x80, 1,2,4,8, 0x10,0x20,0x40,(char)0x80, 1,2,4,8);
    const __m128i r = _mm_cmpeq_epi8(_mm_and_si128(v, m), m);
    u128 out; memcpy(&out, &r, 16); return out;
}

/* the lifted region: GOW_DC_*. Game code, so it is not in the repo: gen_edge_dcc0_body.py writes it from
 * the d935 lift at build time (and checks its shape). */
#include "gow_edge_dcc0_body.h"

/* The control flow is the lift's: E210: L1; if (r19) goto E210; M1; E30C: L2; if (r31) goto E30C; M2;
 * E3C4: L3; if (r21) goto E3C4. */
#define GDC_REGION()                              \
    do { GOW_DC_L1() } while (GOW_DC_COND1 != 0); \
    GOW_DC_M1()                                   \
    do { GOW_DC_L2() } while (GOW_DC_COND2 != 0); \
    GOW_DC_M2()                                   \
    do { GOW_DC_L3() } while (GOW_DC_COND3 != 0);

/* v1: the shared helpers */
static void gdc_v1(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_DC_REGS()
    GOW_DC_LOAD()
    GDC_REGION()
    GOW_DC_STORE()
}

/* native: the same statements with the leaner shufb/fsmb */
#define spu_shufb gdc_shufb
#define spu_fsmb  gdc_fsmb
static void gdc_fast(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    GOW_DC_REGS()
    GOW_DC_LOAD()
    GDC_REGION()
    GOW_DC_STORE()
}
#undef spu_shufb
#undef spu_fsmb

/* ---- self-test of the leaner helpers against the shared ones (random and edge controls) ---- */
static int sdc_selftest_bad;
static uint64_t sdc_rng = 0x9E3779B97F4A7C15ull;
static inline uint32_t gdc_rand(void)
{
    sdc_rng ^= sdc_rng << 13; sdc_rng ^= sdc_rng >> 7; sdc_rng ^= sdc_rng << 17;
    return (uint32_t)(sdc_rng >> 16);
}
static void gdc_selftest(void)
{
    long bad_sh = 0, bad_fs = 0, n_sh = 0;
    for (int i = 0; i < 200000; i++) {
        u128 a, b, c;
        for (int k = 0; k < 4; k++) { a._u32[k] = gdc_rand(); b._u32[k] = gdc_rand(); c._u32[k] = gdc_rand(); }
        if (i & 1)   /* mostly plain selectors, a few special ones */
            for (int k = 0; k < 4; k++) c._u32[k] &= (gdc_rand() & 7) ? 0x1F1F1F1Fu : 0xFFFFFFFFu;
        const u128 x = spu_shufb(a, b, c), y = gdc_shufb(a, b, c);
        n_sh++;
        if (memcmp(&x, &y, 16)) bad_sh++;
    }
    for (uint32_t v = 0; v < 0x20000; v++) {   /* all low halfwords, with and without upper bits */
        u128 a; a._u32[0] = v | (v & 0x10000u ? 0xABCD0000u : 0); a._u32[1] = gdc_rand(); a._u32[2] = gdc_rand();
        a._u32[3] = gdc_rand();
        const u128 x = spu_fsmb(a), y = gdc_fsmb(a);
        if (memcmp(&x, &y, 16)) bad_fs++;
    }
    fprintf(stderr, "[edge-dcc0] self-test: shufb %ld of %ld differ, fsmb %ld of %d differ\n",
            bad_sh, n_sh, bad_fs, 0x20000);
    if (bad_sh || bad_fs) {
        sdc_selftest_bad = 1;
        fprintf(stderr, "[edge-dcc0] self-test failed: modes 1/3 run v1 instead\n");
    }
}

/* ---- entry points the lift calls ---------------------------------------------------- */
static int sdc_mode = -1;

int gow_edge_dcc0_mode(void)
{
    if (sdc_mode < 0) {
        const char* e = getenv("GOW_EDGE_DCC0");
        int m = e && *e ? atoi(e) : 0;   /* default: the lifted region */
        if (m < 0 || m > 4) m = 0;
        sdc_mode = m;
        if (!m) fprintf(stderr, "[edge-dcc0] GOW_EDGE_DCC0=0 (lifted region)\n");
        else {
            fprintf(stderr, "[edge-dcc0] GOW_EDGE_DCC0=%d (%s %s)\n", m, m <= 2 ? "native" : "check",
                    (m == 2 || m == 4) ? "v1" : "with the leaner shufb/fsmb");
            gdc_selftest();
        }
    }
    return sdc_mode;
}

static int sdc_fault = -1;
static long sdc_fault_at, sdc_runs;
static void gdc_fault(spu_context* ctx)
{
    if (sdc_fault < 0) {
        const char* f = getenv("GOW_EDGE_DCC0_FAULT");
        sdc_fault = f ? atoi(f) : 0;
        const char* k = f ? strchr(f, ',') : NULL;
        sdc_fault_at = k ? atol(k + 1) : 0;
    }
    if (sdc_fault <= 0 || sdc_runs++ != sdc_fault_at) return;
    switch (sdc_fault) {
    case 1: ctx->ls[(ctx->gpr[47]._u32[0] & (SPU_LS_MASK & ~0xFu)) + 15] ^= 1; break;  /* ST(r47 + 0, r11) */
    case 2: ctx->gpr[12]._u32[0] ^= 1; break;
    default: break;
    }
    fprintf(stderr, "[edge-dcc0] fault %d injected (native run %ld)\n", sdc_fault, sdc_fault_at);
    sdc_fault = 0;
}

static void gdc_run(spu_context* ctx)
{
    if (sdc_mode == 2 || sdc_mode == 4 || sdc_selftest_bad) gdc_v1(ctx);
    else                                                     gdc_fast(ctx);
    if (__builtin_expect(sdc_fault != 0, 0)) gdc_fault(ctx);
}

/* Modes 1 and 2: replace the region; the lift then jumps to its exit. */
void gow_edge_dcc0_e210(spu_context* ctx) { gdc_run(ctx); }

/* Modes 3 and 4 (check): at the region head run the native region on the live state, keep its result, put
 * the state back and let the lifted region run; at the exit compare all 128 GPRs and the whole LS. */
typedef struct {
    u128     pre_gpr[128], nat_gpr[128];
    uint8_t* pre_ls;
    uint8_t* nat_ls;
    int      pending;
    uint64_t t_lift0;
} gdc_check_state;
static _Thread_local gdc_check_state* tdc_chk;

static long sdc_calls, sdc_bad;
static uint64_t sdc_cyc_nat, sdc_cyc_lift;
static int sdc_atexit;

static void gdc_check_report(void)
{
    fprintf(stderr, "[edge-dcc0-check] %ld calls checked, %ld differ; tsc: native %.2f M, lifted %.2f M "
                    "(%.0f vs %.0f per call)\n",
            sdc_calls, sdc_bad, sdc_cyc_nat / 1e6, sdc_cyc_lift / 1e6,
            sdc_calls ? (double)sdc_cyc_nat / (double)sdc_calls : 0.0,
            sdc_calls ? (double)sdc_cyc_lift / (double)sdc_calls : 0.0);
}

void gow_edge_dcc0_check_begin(spu_context* ctx)
{
    gdc_check_state* c = tdc_chk;
    if (!c) {
        c = (gdc_check_state*)calloc(1, sizeof(*c));
        c->pre_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        c->nat_ls = (uint8_t*)malloc(SPU_LS_SIZE);
        tdc_chk = c;
    }
    if (!sdc_atexit) { sdc_atexit = 1; atexit(gdc_check_report); }
    memcpy(c->pre_gpr, ctx->gpr, sizeof(c->pre_gpr));
    memcpy(c->pre_ls, ctx->ls, SPU_LS_SIZE);
    const uint64_t t0 = __rdtsc();
    gdc_run(ctx);
    sdc_cyc_nat += __rdtsc() - t0;
    memcpy(c->nat_gpr, ctx->gpr, sizeof(c->nat_gpr));
    memcpy(c->nat_ls, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->gpr, c->pre_gpr, sizeof(c->pre_gpr));
    memcpy(ctx->ls, c->pre_ls, SPU_LS_SIZE);
    c->pending = 1;
    c->t_lift0 = __rdtsc();
}

void gow_edge_dcc0_check_end(spu_context* ctx)
{
    gdc_check_state* c = tdc_chk;
    if (!c || !c->pending) return;
    sdc_cyc_lift += __rdtsc() - c->t_lift0;
    c->pending = 0;
    const long call = sdc_calls++;
    int bad = 0;
    for (int r = 0; r < 128; r++) {
        if (!memcmp(&c->nat_gpr[r], &ctx->gpr[r], 16)) continue;
        if (bad < 8)
            fprintf(stderr, "[edge-dcc0-check] call %ld: r%d lifted %08X %08X %08X %08X native %08X %08X %08X %08X\n",
                    call, r, ctx->gpr[r]._u32[0], ctx->gpr[r]._u32[1], ctx->gpr[r]._u32[2], ctx->gpr[r]._u32[3],
                    c->nat_gpr[r]._u32[0], c->nat_gpr[r]._u32[1], c->nat_gpr[r]._u32[2], c->nat_gpr[r]._u32[3]);
        bad++;
    }
    long nls = 0, first = -1;
    for (long i = 0; i < SPU_LS_SIZE; i++)
        if (c->nat_ls[i] != ctx->ls[i]) { if (first < 0) first = i; nls++; }
    if (nls)
        fprintf(stderr, "[edge-dcc0-check] call %ld: LS differs in %ld bytes, first at 0x%05lX (lifted %02X native %02X)\n",
                call, nls, first, ctx->ls[first], c->nat_ls[first]);
    if (bad || nls) {
        sdc_bad++;
        fprintf(stderr, "[edge-dcc0-check] call %ld: %d GPRs and %ld LS bytes differ\n", call, bad, nls);
    }
    if ((call & 4095) == 4095) gdc_check_report();   /* a game run ends by kill: atexit never runs */
}

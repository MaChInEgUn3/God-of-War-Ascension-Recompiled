/* gow_a67e_noise.c -- native replacement for function 4930 of the General6 job module (a67e, lift
 * spu/jobmod_a67e549fa0ab_e41e8): 4930's prologue and the entry block of 4AB0 it falls into, the hottest code of
 * General6 (~45% of its samples). The a67e lift is built through gen_a67e_noise_lift.py, which renames the lifted
 * 4930 to ..._4930_lifted and puts a switch in its place. See docs/EDGE_NATIVE.md (General6 section).
 *
 * What 4930 computes: Ken Perlin's "improved noise" (2002) in 3D for four points per call. Point i is r(3+i)
 * (x, y, z in words 0-2); word i of the returned r3 is its noise value. The tables live in the module's data:
 * the doubled permutation (512 big-endian words, 151, 160, 137, ...) at LS 0x8680 and 16 gradient quads at
 * 0x8580. The SPU code does each point in its own lanes with splats and scalar gathers; here the four points are
 * the four lanes of every vector (structure of arrays).
 *
 * Exact: every float and integer step is the SPU op the lift runs, in the lift's order and with the same
 * operands (spu_helpers.h, the same inline helpers the lift uses, under the same MXCSR), including the floor
 * built from integer masks and the corner offsets the SPU code forms as x + 0, x - 0, x + -1 or x - 1 (they
 * differ for -0). The order was read from a symbolic trace of the lifted block; all four output words share it.
 * Side effects kept: the 16 callee-saved registers 4930 spills (r80-r94, r126) and the back chain (r1) written
 * below the stack pointer. r3 is the result; r0, r1 and r80-r127 end unchanged, as the lift leaves them. The
 * caller-saved scratch registers (r2, r4-r79) are not reproduced: the SPU ABI makes them dead at the return.
 *
 * Built as an SPU source (-O2 -march=x86-64-v3 -ffp-contract=off, SPU_FP_NATIVE=1): runs inside
 * spu_run_with_halt, i.e. with MXCSR at round toward zero, FTZ and DAZ.
 *
 * Clean-room: written from our own lift of the module, our own captures and the published algorithm.
 *
 * GOW_A67_NOISE (read once):
 *   0        lifted 4930
 *   1/unset  native
 *   3        check: run the native version, then the lifted one from the same state, and compare r3, r0, r1,
 *            r80-r127 and the 0x140 bytes below the stack pointer; print the first difference and a count
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spu_context.h"
#include "spu_helpers.h"

#define A67_PERM 0x8680u   /* image base 0x4000 + 0x4680 */
#define A67_GRAD 0x8580u   /* image base 0x4000 + 0x4580 */

static inline u128 a67_k(uint32_t v) { u128 r; r._u32[0] = r._u32[1] = r._u32[2] = r._u32[3] = v; return r; }

static inline uint32_t a67_w(const uint8_t* ls, uint32_t a)
{
    uint32_t w; memcpy(&w, ls + (a & (SPU_LS_MASK & ~3u)), 4);
    return SPU_BSWAP32(w);
}

/* perm[idx] for each lane (lqx + rotqby of a word-aligned address in the lift) */
static inline u128 a67_perm(const uint8_t* ls, u128 idx, uint32_t plus)
{
    u128 r;
    for (int i = 0; i < 4; i++) r._u32[i] = a67_w(ls, A67_PERM + plus + (idx._u32[i] << 2));
    return r;
}

/* the SPU floor: x - (x < 0 ? 0x3F7FFFFF : 0), then the fraction bits cleared by exponent */
static inline u128 a67_floor(u128 x)
{
    const u128 t = spu_fs(x, spu_and(spu_rotmai(x, -31), a67_k(0x3F7FFFFF)));
    const u128 sh = spu_sfi(spu_andi(spu_rotmi(t, -23), 255), 127);
    u128 m = spu_and(spu_rotm(a67_k(0x7FFFFF), sh), spu_cgti(sh, -31));
    m = spu_or(m, spu_xori(spu_rotmai(spu_ai(sh, -1), -31), -1));
    return spu_andc(t, m);
}

static inline u128 a67_fade(u128 f)
{
    const u128 t3 = spu_fm(spu_fm(f, f), f);
    const u128 p = spu_fma(f, spu_fms(f, a67_k(0x40C00000), a67_k(0x41700000)), a67_k(0x41200000));  /* 6, 15, 10 */
    return spu_fm(t3, p);
}

/* (rx*gx + ry*gy) + rz*gz with gradient quad G[h & 15] per lane */
static inline u128 a67_grad(const uint8_t* ls, u128 h, u128 rx, u128 ry, u128 rz)
{
    u128 gx, gy, gz;
    for (int i = 0; i < 4; i++) {
        const uint32_t a = A67_GRAD + ((h._u32[i] & 15u) << 4);
        gx._u32[i] = a67_w(ls, a); gy._u32[i] = a67_w(ls, a + 4); gz._u32[i] = a67_w(ls, a + 8);
    }
    return spu_fa(spu_fa(spu_fm(rx, gx), spu_fm(ry, gy)), spu_fm(rz, gz));
}

static inline u128 a67_lerp(u128 t, u128 a, u128 b) { return spu_fma(spu_fs(b, a), t, a); }

static void a67_noise(spu_context* ctx)
{
    uint8_t* const ls = ctx->ls;
    u128* const g = ctx->gpr;
    const uint32_t sp = g[1]._u32[0];

    /* 4930's spills: r80-r94 at sp-0x10 .. sp-0xF0, r126 at sp-0x100, the back chain at sp-0x130 */
    for (int r = 80; r <= 94; r++) spu_ls_write128(ctx, sp - 0x10u * (uint32_t)(r - 79), g[r]);
    spu_ls_write128(ctx, sp - 0x100u, g[126]);
    spu_ls_write128(ctx, sp - 0x130u, g[1]);

    u128 x, y, z;
    for (int i = 0; i < 4; i++) {
        x._u32[i] = g[3 + i]._u32[0]; y._u32[i] = g[3 + i]._u32[1]; z._u32[i] = g[3 + i]._u32[2];
    }
    const u128 flx = a67_floor(x), fly = a67_floor(y), flz = a67_floor(z);
    const u128 fx = spu_fs(x, flx), fy = spu_fs(y, fly), fz = spu_fs(z, flz);
    const u128 X = spu_andi(spu_cflts(flx, 173), 255);
    const u128 Y = spu_andi(spu_cflts(fly, 173), 255);
    const u128 Z = spu_andi(spu_cflts(flz, 173), 255);

    const u128 A = spu_a(Y, a67_perm(ls, X, 0)), B = spu_a(Y, a67_perm(ls, X, 4));
    const u128 AA = spu_a(Z, a67_perm(ls, A, 0)), AB = spu_a(Z, a67_perm(ls, A, 4));
    const u128 BA = spu_a(Z, a67_perm(ls, B, 0)), BB = spu_a(Z, a67_perm(ls, B, 4));

    const u128 m1 = a67_k(0xBF800000), p1 = a67_k(0x3F800000), z0 = a67_k(0);
    const u128 xm1a = spu_fa(fx, m1), xz0a = spu_fa(fx, z0), xz0s = spu_fs(fx, z0), xm1s = spu_fs(fx, p1);
    const u128 ym1a = spu_fa(fy, m1), yz0a = spu_fa(fy, z0), yz0s = spu_fs(fy, z0), ym1s = spu_fs(fy, p1);
    const u128 zm1a = spu_fa(fz, m1), zz0a = spu_fa(fz, z0), zz0s = spu_fs(fz, z0), zm1s = spu_fs(fz, p1);

    const u128 g000 = a67_grad(ls, a67_perm(ls, AA, 0), fx, fy, fz);
    const u128 g100 = a67_grad(ls, a67_perm(ls, BA, 0), xm1s, yz0s, zz0s);
    const u128 g010 = a67_grad(ls, a67_perm(ls, AB, 0), xz0s, ym1s, zz0s);
    const u128 g110 = a67_grad(ls, a67_perm(ls, BB, 0), xm1a, ym1a, zz0a);
    const u128 g001 = a67_grad(ls, a67_perm(ls, AA, 4), xz0s, yz0s, zm1s);
    const u128 g101 = a67_grad(ls, a67_perm(ls, BA, 4), xm1a, yz0a, zm1a);
    const u128 g011 = a67_grad(ls, a67_perm(ls, AB, 4), xz0a, ym1a, zm1a);
    const u128 g111 = a67_grad(ls, a67_perm(ls, BB, 4), xm1a, ym1a, zm1a);

    const u128 u = a67_fade(fx), v = a67_fade(fy), w = a67_fade(fz);
    const u128 y1 = a67_lerp(v, a67_lerp(u, g000, g100), a67_lerp(u, g010, g110));
    const u128 y2 = a67_lerp(v, a67_lerp(u, g001, g101), a67_lerp(u, g011, g111));
    g[3] = spu_fma(w, spu_fs(y2, y1), y1);
}

static int s_a67_mode = -1;
int gow_a67e_noise_mode(void)
{
    if (s_a67_mode < 0) {
        const char* e = getenv("GOW_A67_NOISE");
        s_a67_mode = e && *e ? atoi(e) : 1;
        fprintf(stderr, "[a67-noise] mode %d\n", s_a67_mode);
    }
    return s_a67_mode;
}

void gow_a67e_noise(spu_context* ctx) { a67_noise(ctx); }

/* check mode: native first, then the lifted function from the same registers and stack bytes */
static long s_a67_checked, s_a67_bad, s_a67_skipped;
static void a67_check_report(void)
{
    fprintf(stderr, "[a67-noise] check: %ld calls checked, %ld differ, %ld skipped\n",
            s_a67_checked, s_a67_bad, s_a67_skipped);
}
void gow_a67e_noise_check(spu_context* ctx, void (*lifted)(spu_context*))
{
    static int reg;
    if (!reg) { reg = 1; atexit(a67_check_report); }
    enum { WIN = 0x140 };
    const uint32_t sp = ctx->gpr[1]._u32[0] & SPU_LS_MASK & ~0xFu;
    if (sp < WIN) { lifted(ctx); s_a67_skipped++; return; }
    u128 gpr0[128], gprn[128];
    uint8_t win0[WIN], winn[WIN];
    memcpy(gpr0, ctx->gpr, sizeof gpr0);
    memcpy(win0, ctx->ls + sp - WIN, WIN);
    a67_noise(ctx);
    memcpy(gprn, ctx->gpr, sizeof gprn);
    memcpy(winn, ctx->ls + sp - WIN, WIN);
    memcpy(ctx->gpr, gpr0, sizeof gpr0);
    memcpy(ctx->ls + sp - WIN, win0, WIN);
    lifted(ctx);
    if (ctx->pc != ctx->gpr[0]._u32[0]) { s_a67_skipped++; return; }   /* the lift left through the trampoline */
    s_a67_checked++;
    int bad = -1;
    static const int regs[] = { 0, 1, 3 };
    for (int k = 0; k < 3 && bad < 0; k++) if (memcmp(&gprn[regs[k]], &ctx->gpr[regs[k]], 16)) bad = regs[k];
    for (int r = 80; r < 128 && bad < 0; r++) if (memcmp(&gprn[r], &ctx->gpr[r], 16)) bad = r;
    if (bad < 0 && memcmp(winn, ctx->ls + sp - WIN, WIN)) bad = 1000;
    if (bad >= 0) {
        if (s_a67_bad++ < 8) {
            if (bad == 1000) fprintf(stderr, "[a67-noise] check: stack window differs (sp=0x%05X)\n", sp);
            else fprintf(stderr, "[a67-noise] check: r%d native %08X %08X %08X %08X lifted %08X %08X %08X %08X"
                         " (in r3 %08X %08X %08X)\n", bad,
                         gprn[bad]._u32[0], gprn[bad]._u32[1], gprn[bad]._u32[2], gprn[bad]._u32[3],
                         ctx->gpr[bad]._u32[0], ctx->gpr[bad]._u32[1], ctx->gpr[bad]._u32[2], ctx->gpr[bad]._u32[3],
                         gpr0[3]._u32[0], gpr0[3]._u32[1], gpr0[3]._u32[2]);
        }
    }
    if ((s_a67_checked & 0xFFFFF) == 0) a67_check_report();
}

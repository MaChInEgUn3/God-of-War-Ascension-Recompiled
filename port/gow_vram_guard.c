/* gow_vram_guard.c -- refuse a VRAM-cache release that has nothing to release.
 *
 * GoW:A's texture cache keeps one 0x80-byte descriptor per VRAM block
 * (+0x0C size, +0x20 bytes of the object currently resident in it) and a
 * 16-byte side entry per descriptor (+2 flags, +8 LRU frame stamp). Under
 * memory pressure the compactor (0x372B70) picks the least recently used
 * object and calls the release 0x370E48(r4 = object), which subtracts the
 * object's evictable size (obj+0x1C, its top mips) from the size of the
 * descriptor obj+0x44 points at, zeroes desc+0x20 and clears the side entry's
 * resident flags.
 *
 * In our runs the compactor sometimes picks an object whose descriptor holds
 * only the part without the top mips (size obj+0x20 - obj+0x1C) and has
 * nothing resident (desc+0x20 == 0). The subtraction then drives the size
 * negative, nothing is freed, the pressure loop picks the same object again
 * and again, and the next defrag copy is sized from the negative block
 * ("invalid size", CopyLocalMemory offset errors, blits past VRAM). The
 * game's own other release site (0x371004) checks desc+0x20 != 0 first; this
 * applies that check to every release: the side entry is marked not resident
 * and dropped from the LRU (stamp 0), and the release is skipped.
 *
 * It is a mitigation: the cause is an ordering the console never produces
 * (likely the frame-stamped LRU against wall-clock streaming at ~1.5 fps).
 * GOW_RELEASE_GUARD=0 turns it off. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern uint8_t* vm_base;
__declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);

static uint32_t rd32(uint32_t ea) { const uint8_t* p = vm_base + ea; return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t rd16(uint32_t ea) { const uint8_t* p = vm_base + ea; return (uint16_t)(p[0] << 8 | p[1]); }
static void wr32(uint32_t ea, uint32_t v) { uint8_t* p = vm_base + ea; p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void wr16(uint32_t ea, uint16_t v) { uint8_t* p = vm_base + ea; p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

#define SIDE_TABLE_PTR 0x0096E174u   /* -> 16-byte side entries */
#define DESC_BASE_PTR  0x0096E178u   /* -> first descriptor */

static struct { uint32_t obj, size, want, frame; uint16_t flags; } s_note[256];
/* Probes placed with patch_lift.py: how often a guest function is entered. 0 = 0x37BD48 and 1 = 0x328AFC run
 * several times per frame during play and serve as the poll points of the specimen trace below; 2 = 0x3CA8EC
 * (fills the level loader's handler table at 0x00997770, once), 3 = 0x3C9644 and 4 = 0x3C8F64 (the loader's
 * completion step and the caller of its handlers: flat once a level is up). */
static unsigned s_probe[8];
extern void ppu_frame_funcs_mark(void);   /* toolkit, runtime/ppu/ppu_loader.cpp: PPU_FRAME_FUNCS */
/* GOW_RELEASE_STATS: follow one object. The specimen is the first object released 6 times once the level has
 * been up for a while; from then on every poll point (the probes, each release, each entry of 0x370B48) compares
 * its side flags, its descriptor's size and resident bytes and the title's pending-object word *0x96DF44 with the
 * last look and prints a line when one changed. */
static uint32_t s_spec; static unsigned s_maxidx;
static void spec_poll(const char* where)
{
    static uint32_t l_desc, l_res, l_size; static uint16_t l_flags; static int l_pend = -1; static unsigned lines;
    if (!s_spec || !vm_base || lines > 700) return;
    const uint32_t obj = s_spec, d = rd32(obj + 0x44), st = rd32(SIDE_TABLE_PTR), db = rd32(DESC_BASE_PTR);
    const uint32_t side = (d && st && d >= db) ? st + ((d - db) >> 7) * 16u : 0;
    const uint16_t fl = side ? rd16(side + 2) : 0;
    const uint32_t res = d ? rd32(d + 0x20) : 0, size = d ? rd32(d + 0xC) : 0;
    const int pend = rd32(0x0096DF44u) == obj;
    if (d == l_desc && fl == l_flags && res == l_res && size == l_size && pend == l_pend) return;
    uint32_t tb = d ? rd32(d + 0x28) : 0; float timer; memcpy(&timer, &tb, 4);
    lines++;
    fprintf(stderr, "[vram-spec] frame %u at %s: obj %08X desc %08X size %X resident %X want %X timer %.3f | side flags "
            "%04X (was %04X) stamp %u | pending-word %08X tid %lu%c", rd32(0x00975888u), where, obj, d, size, res,
            d ? rd32(d + 0x1C) : 0, timer, fl, l_flags, side ? rd32(side + 8) : 0, rd32(0x0096DF44u),
            GetCurrentThreadId(), 10);
    l_desc = d; l_flags = fl; l_res = res; l_size = size; l_pend = pend;
}
/* Called where func_003A7B3C is about to write a texture's side flags 0x0200 and 0x4000 (label hooks in
 * patch_lift.py): want = the bit it will write, val / thr = the two floats it compared (want is 1 when
 * val > thr, else the entry's 0x0200 as it stands). Logged for the specimen only. */
/* GOW_RELEASE_GUARD=3 (experiment): the last frame in which 0x3A7B3C wrote "wanted" for each side entry, so a
 * release can be refused for a texture that was wanted a few frames ago (the bits drop for single frames). */
static uint32_t s_lastwant[8192];
static int s_guard_mode = -1;
int gow_want_note(int site, uint32_t side, uint32_t want, double val, double thr)
{
    static unsigned lines;
    if (s_guard_mode < 0) { const char* e = getenv("GOW_RELEASE_GUARD"); s_guard_mode = e ? atoi(e) : 1; }
    if (s_guard_mode == 3 && (want & 1u) && vm_base) {
        const uint32_t st0 = rd32(SIDE_TABLE_PTR);
        if (st0 && side >= st0 && ((side - st0) >> 4) < 8192u) s_lastwant[(side - st0) >> 4] = rd32(0x00975888u);
    }
    if (!s_spec || !vm_base || lines > 3000) return 0;
    const uint32_t d = rd32(s_spec + 0x44), st = rd32(SIDE_TABLE_PTR), db = rd32(DESC_BASE_PTR);
    if (!d || !st || d < db || side != st + ((d - db) >> 7) * 16u) return 0;
    uint32_t mb = rd32(side + 0xC); float metric; memcpy(&metric, &mb, 4);
    {   /* GOW_WANT_MARK=<metric>: a frame whose first look at the specimen has a metric below this is marked for
         * the runtime's PPU_FRAME_FUNCS diff (which functions ran in that frame and not in the one before). */
        static double below = -1.0;
        if (below < 0.0) { const char* e = getenv("GOW_WANT_MARK"); below = e ? atof(e) : 0.0; }
        if (below > 0.0 && rd32(side + 8) != rd32(0x00975888u) && metric < below) {
            ppu_frame_funcs_mark();
            fprintf(stderr, "[vram-want] frame %u marked: first metric %g%c", rd32(0x00975888u), (double)metric, 10);
        }
    }
    lines++;
    fprintf(stderr, "[vram-want] frame %u site %d tid %lu: want %u value %.4f threshold %.4f | flags-before %04X "
            "stamp-before %u metric %g bytes %u %u resident %X%c", rd32(0x00975888u), site, GetCurrentThreadId(),
            want & 1u, val, thr, rd16(side + 2), rd32(side + 8), (double)metric, vm_base[side], vm_base[side + 1],
            rd32(d + 0x20), 10);
    return 0;
}
/* GOW_DIP_MARK=1 (experiment): a burst of releases (5 within 30 frames) opens a window of 100 frames; every frame
 * in it is marked for the runtime's PPU_FRAME_FUNCS statistics, to name what the title runs after a batch release. */
static uint32_t s_dip_until, s_rel_frames[5]; static unsigned s_rel_n;
static void dip_release_seen(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("GOW_DIP_MARK"); on = (e && *e == '1'); }
    if (!on || !vm_base) return;
    {   /* not during the level load: bursts count from 150 s after the first release on */
        static time_t first;
        if (!first) first = time(NULL);
        if (time(NULL) - first < 150) return;
    }
    const uint32_t fr = rd32(0x00975888u);
    s_rel_frames[s_rel_n++ % 5u] = fr;
    if (s_rel_n >= 5) {
        uint32_t oldest = fr;
        for (int k = 0; k < 5; k++) if (s_rel_frames[k] < oldest) oldest = s_rel_frames[k];
        if (fr - oldest <= 30 && fr >= s_dip_until) {
            s_dip_until = fr + 100;
            fprintf(stderr, "[vram-dip] release burst at frame %u: marking 100 frames%c", fr, 10);
        }
    }
}
int gow_probe_count(int which, uint32_t r3)
{
    static const char* const nm[8] = { "37BD48", "328AFC", "3CA8EC", "3C9644", "3C8F64", "p5", "p6", "p7" };
    if (which >= 0 && which < 8) { s_probe[which]++; spec_poll(nm[which]); }
    if (s_dip_until && vm_base && rd32(0x00975888u) < s_dip_until) ppu_frame_funcs_mark();
    (void)r3;
    return 0;
}
/* Called on entry to 0x370E48; nonzero = skip the release. */
int gow_release_veto(uint32_t obj)
{
    static int enabled = -1;
    if (enabled < 0) { const char* e = getenv("GOW_RELEASE_GUARD"); enabled = !(e && *e == '0'); }
    /* The whole 32-bit guest space is backed, and the game puts these tables in
     * either the 0x2xxxxxxx or the 0x8xxxxxxx range from run to run (descriptors
     * at 0x2154B4C0 or 0x8154B4C0): a range check here silently disabled the
     * guard in the runs that used the high one. */
    /* GOW_RELEASE_STATS=<s>: every <s> seconds, how often the title released an object, how many bytes of
     * top mips those were, and how many releases the guard refused (the log lines above stop after 8). */
    static int stats = -1; static time_t st_t; static unsigned st_calls, st_veto; static unsigned long long st_bytes;
    if (stats < 0) { const char* e = getenv("GOW_RELEASE_STATS"); stats = e ? atoi(e) : 0; st_t = time(NULL); }
    dip_release_seen();
    if (stats > 0 && vm_base && obj) {
        st_calls++; st_bytes += rd32(obj + 0x1C);
        {   /* choose the specimen: 100 s after the first release, the first object to be released 6 times */
            static time_t first; static struct { uint32_t obj; unsigned n; } cnt[128];
            const uint32_t d0 = rd32(obj + 0x44), db0 = rd32(DESC_BASE_PTR);
            if (d0 && d0 >= db0 && ((d0 - db0) >> 7) > s_maxidx && ((d0 - db0) >> 7) < 65536u) s_maxidx = (d0 - db0) >> 7;
            if (!first) first = time(NULL);
            if (!s_spec && time(NULL) - first >= 100) {
                const unsigned k = (obj >> 4) & 127u;
                if (cnt[k].obj != obj) { cnt[k].obj = obj; cnt[k].n = 0; }
                if (++cnt[k].n >= 6) {
                    s_spec = obj;
                    fprintf(stderr, "[vram-spec] specimen %08X (evictable %X total %X)%c", obj, rd32(obj + 0x1C),
                            rd32(obj + 0x20), 10);
                }
            }
            spec_poll("release");
        }
        {   /* a sample of what a release looks like, once the scene has settled: releases 200 to 239 */
            static unsigned seen;
            if (++seen > 200 && seen <= 240) {
                const uint32_t d = rd32(obj + 0x44), st = rd32(SIDE_TABLE_PTR), db = rd32(DESC_BASE_PTR);
                const uint32_t side = (d && st && d >= db) ? st + ((d - db) >> 7) * 16u : 0;
                uint32_t tb = d ? rd32(d + 0x28) : 0; float timer; memcpy(&timer, &tb, 4);
                {   const unsigned k = (obj >> 4) & 255u;
                    if (s_note[k].obj == obj)
                        fprintf(stderr, "[vram-release-note] obj %08X was the pending object at an entry of 0x370B48: r4 %X "
                                "against desc+1C %X at frame %u, now frame %u%c", obj, s_note[k].size, s_note[k].want,
                                s_note[k].frame, rd32(0x00975888u), 10); }
                fprintf(stderr, "[vram-release-sample] obj %08X evictable %X total %X flags12 %04X | desc %08X size %X "
                        "resident %X timer %.3f | side flags %04X stamp %u | g(96DDCC) %u g(96E15C) %08X%c",
                        obj, rd32(obj + 0x1C), rd32(obj + 0x20), rd16(obj + 0x12), d, d ? rd32(d + 0xC) : 0,
                        d ? rd32(d + 0x20) : 0, timer, side ? rd16(side + 2) : 0, side ? rd32(side + 8) : 0,
                        rd32(0x0096DDCCu), rd32(0x0096E15Cu), 10);
            }
        }
        const time_t now = time(NULL);
        if (now - st_t >= stats) {
            /* The compactor (0x372B70) releases while (A + B) / total > 0.75 (constant at 0x372B68), with
             * A = *0x96DDDC, B = *0x96E164, total = *0x96DDE0 (mode word *0x96DDCC: 0 here). */
            const uint32_t pa = rd32(0x0096DDDCu), pb = rd32(0x0096E164u), pt = rd32(0x0096DDE0u);
            {   /* census of the side entries up to the highest descriptor index a release has named */
                const uint32_t st = rd32(SIDE_TABLE_PTR); unsigned c4 = 0, c8 = 0, c2 = 0, c1 = 0, c02 = 0, used = 0;
                for (unsigned i = 0; st && i <= s_maxidx; i++) {
                    const uint16_t f = rd16(st + i * 16u + 2);
                    if (f) used++;
                    if (f & 0x4000) c4++; if (f & 0x8000) c8++; if (f & 0x2000) c2++; if (f & 0x1000) c1++;
                    if (f & 0x0200) c02++;
                }
                fprintf(stderr, "[vram-census] side entries 0..%u: %u with flags; 0x8000 %u, 0x4000 %u, 0x2000 %u, 0x1000 %u, "
                        "0x0200 %u%c", s_maxidx, used, c8, c4, c2, c1, c02, 10);
            }
            fprintf(stderr, "[vram-release] %lld s: %u releases, %.1f MB of top mips, %u refused by the guard | "
                    "pool A %.1f MB + B %.1f MB of %.1f MB = %.3f (releases above 0.750) | probes %u %u %u %u %u | pending %u g6FC %u g6F8 %08X%c",
                    (long long)(now - st_t), st_calls, st_bytes / 1048576.0, st_veto,
                    pa / 1048576.0, pb / 1048576.0, pt / 1048576.0, pt ? (double)((unsigned long long)pa + pb) / pt : 0.0, s_probe[0], s_probe[1], s_probe[2], s_probe[3], s_probe[4], rd32(0x009976E0u), rd32(0x0082B6FCu), rd32(0x0082B6F8u), 10);
            st_t = now; st_calls = st_veto = 0; st_bytes = 0;
        }
    }
    if (!enabled || !vm_base || !obj) return 0;
    uint32_t desc = rd32(obj + 0x44);
    if (!desc) return 0;
    {   /* GOW_RELEASE_GUARD=3[,<frames>] (experiment): refuse the release of a texture that 0x3A7B3C marked
         * wanted within the last <frames> frames (default 8). Nothing else is touched: the entry stays as it is. */
        static int keep = -1; static unsigned kept;
        if (keep < 0) { const char* e = getenv("GOW_RELEASE_GUARD"); const char* c = e ? strchr(e, ',') : NULL;
                        keep = (e && atoi(e) == 3) ? (c ? atoi(c + 1) : 8) : 0; }
        if (keep > 0) {
            const uint32_t st0 = rd32(SIDE_TABLE_PTR), db0 = rd32(DESC_BASE_PTR);
            if (st0 && desc >= db0 && ((desc - db0) >> 7) < 8192u) {
                const uint32_t lw = s_lastwant[(desc - db0) >> 7], fr = rd32(0x00975888u);
                if (lw && fr - lw <= (uint32_t)keep) {
                    if (stats > 0) st_veto++;
                    if (kept++ < 8)
                        fprintf(stderr, "[vram-guard-want] release of obj 0x%08X refused: wanted %u frames ago%c",
                                obj, fr - lw, 10);
                    return 1;
                }
            }
        }
    }
    /* Also refuse any release that would drive the block size negative (size below
     * the object's evictable size), whatever path got there: a faster SPU
     * configuration produced 'invalid size' with no veto of the check below. */
    const int underflow = rd32(desc + 0xC) < rd32(obj + 0x1C);
    if (rd32(desc + 0x20) != 0 && !underflow) return 0;

    uint32_t side_tab = rd32(SIDE_TABLE_PTR), desc_base = rd32(DESC_BASE_PTR);
    if (side_tab && desc >= desc_base) {
        uint32_t side = side_tab + ((desc - desc_base) >> 7) * 16u;
        wr16(side + 2, (uint16_t)(rd16(side + 2) & ~0x6000u));   /* not resident */
        wr32(side + 8, 0);                                       /* out of the LRU */
    }
    static int n = 0, nu = 0;
    if (stats > 0) st_veto++;
    if (!underflow || rd32(desc + 0x20) == 0) {
        if (n++ < 8)
            fprintf(stderr, "[vram-guard] release of obj 0x%08X skipped: desc 0x%08X (size 0x%X) has nothing "
                    "resident (obj evictable 0x%X)\n", obj, desc, rd32(desc + 0xC), rd32(obj + 0x1C));
    } else if (nu++ < 8)
        fprintf(stderr, "[vram-guard-underflow] release of obj 0x%08X skipped: desc 0x%08X size 0x%X < obj "
                "evictable 0x%X (resident 0x%X)\n", obj, desc, rd32(desc + 0xC), rd32(obj + 0x1C),
                rd32(desc + 0x20));
    return 1;
}


/* Called on entry to 0x370B48, a handler in the level loader's table (0x00997770). Its arguments are r3 and r4
 * only; the object it works on is the word at 0x0096DF44 (0 = nothing to do): it clears that object's side flag
 * 0x1000 and the word, and when r4 equals the object's descriptor +0x1C it ORs 0x4000 into the 16-bit word at
 * r3 + 2. The third parameter is whatever r5 held and is ignored. With GOW_RELEASE_STATS the entries are logged. */
int gow_loaded_note(uint32_t r3, uint32_t r4, uint32_t r5_unused)
{
    static unsigned calls;
    (void)r5_unused;
    if (!vm_base) return 0;
    const uint32_t obj = rd32(0x0096DF44u);
    spec_poll("370B48");
    if (obj) {
        const uint32_t d = rd32(obj + 0x44);
        unsigned k = (obj >> 4) & 255u;
        s_note[k].obj = obj; s_note[k].size = r4; s_note[k].want = d ? rd32(d + 0x1C) : 0;
        s_note[k].frame = rd32(0x00975888u); s_note[k].flags = 0;
    }
    if (getenv("GOW_RELEASE_STATS") && (++calls <= 60 || calls % 500 == 0))
        fprintf(stderr, "[vram-loaded] call %u r3 %08X r4 %08X pending-object %08X frame %u%c",
                calls, r3, r4, obj, rd32(0x00975888u), 10);
    return 0;
}

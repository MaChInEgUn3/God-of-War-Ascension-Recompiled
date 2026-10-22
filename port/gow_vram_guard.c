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

extern uint8_t* vm_base;

static uint32_t rd32(uint32_t ea) { const uint8_t* p = vm_base + ea; return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t rd16(uint32_t ea) { const uint8_t* p = vm_base + ea; return (uint16_t)(p[0] << 8 | p[1]); }
static void wr32(uint32_t ea, uint32_t v) { uint8_t* p = vm_base + ea; p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void wr16(uint32_t ea, uint16_t v) { uint8_t* p = vm_base + ea; p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

#define SIDE_TABLE_PTR 0x0096E174u   /* -> 16-byte side entries */
#define DESC_BASE_PTR  0x0096E178u   /* -> first descriptor */

/* Called on entry to 0x370E48; nonzero = skip the release. */
int gow_release_veto(uint32_t obj)
{
    static int enabled = -1;
    if (enabled < 0) { const char* e = getenv("GOW_RELEASE_GUARD"); enabled = !(e && *e == '0'); }
    if (!enabled || !vm_base || !obj || obj >= 0x40000000u) return 0;
    uint32_t desc = rd32(obj + 0x44);
    if (!desc || desc >= 0x40000000u) return 0;
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

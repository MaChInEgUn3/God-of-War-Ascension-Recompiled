/*
 * GoW Ascension port -- LLE of the firmware SPURS library (libsre.prx).
 *
 * The title's cellSpurs/cellSync imports are normally answered by the HLE
 * library. With GOW_LLE_LIBSRE built in, the user's own decrypted libsre.prx
 * (lifted locally by tools/lift_prx.py + tools/ppu_lifter.py, see
 * docs/FIRMWARE_LLE.md) is placed in guest RAM at 0x30000000 and its exports
 * are published, so ps3_hle_call() routes every NID libsre exports into the
 * real Sony code instead of the HLE handler.
 *
 * The firmware-derived image and lifted C stay on this machine (never commit
 * or distribute them). Mode, selected by the GOW_LLE environment variable:
 *
 *   unset / off   libsre is not loaded at all (pure HLE, the previous baseline)
 *   all-hle       libsre is loaded but every export it publishes is forced back
 *                 to HLE -- proves image placement + import stubs only
 *   real          libsre handles every export, except the NIDs in the default
 *                 keep-HLE list (the Edge Zlib LFQueue push, whose SPU task we
 *                 do not run) and GOW_LLE_FORCE_HLE=<nid,nid,...> (hex)
 *
 * GOW_LLE_IMAGE overrides the path of libsre_image.bin.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>

#ifdef _WIN32
#  include <stdlib.h>
#endif

extern "C" {
#include "prx_loader.h"

extern const prx_func_entry libsre_function_table[];
extern const uint64_t       libsre_function_table_count;
extern const prx_export     libsre_exports[];
extern const uint32_t       libsre_export_count;

void ppu_register_function(uint64_t addr, void (*fn)(void*));

void ppu_guard_page(uint32_t guest_ea);   /* runtime/ppu/ppu_loader.cpp */

/* libs/spurs/cellSpurs.c */
extern int g_spurs_lle;

/* spu_channels.c: a DMA GET from this EA marks the lifted image resident. */
void spu_overlay_register_source(uint32_t content_ea, int image_id);
}

#define LIBSRE_BASE 0x30000000u

/* NIDs libsre exports that must stay on HLE even in "real" mode. */
static const char* kKeepHle[] = {
    "8A85674D",   /* _cellSpursLFQueuePushBody: Edge Zlib HLE inflates the PSARC blocks */
    /* The whole taskset/task/eventflag/LFQueue/barrier family stays on HLE: the
     * tasks' SPU ELFs (Edge Zlib...) are not lifted, and the HLE task registry
     * needs the HLE taskset -- real+HLE state cannot be mixed. */
#include "lle_keep_hle.inc"
};

static void reg(uint32_t addr, void (*fn)(void*)) { ppu_register_function(addr, fn); }

/* GOW_LLE_DUMP=<seconds>[,<ea>]: after N seconds dump the live CellSpurs
 * instance (workload ready counts/state/contention) every 5 s, 3 times. */
extern "C" uint8_t* vm_base;
static uint32_t be32(uint32_t a) { const uint8_t* p = vm_base + a; return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void dump_spurs(uint32_t ea)
{
    fprintf(stderr, "[lle-dump] CellSpurs @0x%08X\n", ea);
    static const char* names[] = { "readyCount1", "idleSpuCount", "curContention", "wklUnk(0x30)",
                                   "minContention", "maxContention", "flag/sig/nSpus", "(0x70)",
                                   "state1", "status1", "event1", "enabled/msg" };
    for (int row = 0; row < 12; row++) {
        fprintf(stderr, "  +%02X %-14s", row * 0x10, names[row]);
        for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", vm_base[ea + row * 0x10 + i]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "  +90 status1 / +A0 event1 / +B0 enabled=%08X sysSrvMsg(+BD)=%02X nSpus(+76)=%u\n",
            be32(ea + 0xB0), vm_base[ea + 0xBD], vm_base[ea + 0x76]);
    for (int w = 0; w < 16; w++) {
        uint32_t a = ea + 0xB00 + w * 0x20;
        fprintf(stderr, "  wkl[%2d] addr=%08X%08X arg=%08X%08X size=%08X uid=%02X prio=%02X%02X%02X%02X%02X%02X%02X%02X\n",
                w, be32(a), be32(a + 4), be32(a + 8), be32(a + 12), be32(a + 16), vm_base[a + 20],
                vm_base[a + 24], vm_base[a + 25], vm_base[a + 26], vm_base[a + 27],
                vm_base[a + 28], vm_base[a + 29], vm_base[a + 30], vm_base[a + 31]);
    }
    fflush(stderr);
}
#ifdef _WIN32
#include <windows.h>
static DWORD WINAPI dump_thread(LPVOID p)
{
    uint32_t secs = (uint32_t)(uintptr_t)p;
    uint32_t ea = 0x1101AD00u;
    if (const char* e = getenv("GOW_LLE_DUMP_EA")) ea = (uint32_t)strtoul(e, 0, 16);
    Sleep(secs * 1000);
    for (int i = 0; i < 3; i++) { dump_spurs(ea); Sleep(5000); }
    return 0;
}
#endif

#ifdef _WIN32
/* GOW_LLE_WATCH=1: poll the first 0xC0 bytes of the live CellSpurs every ms and
 * log every byte that changes, so the order of events against the SPURSTRACE
 * lines shows who sets readyCount / contention / state. */
static DWORD WINAPI watch_thread(LPVOID)
{
    uint32_t ea = 0x1101AD00u;
    if (const char* e = getenv("GOW_LLE_DUMP_EA")) ea = (uint32_t)strtoul(e, 0, 16);
    uint8_t prev[0xC0]; bool have = false;
    for (;;) {
        Sleep(1);
        if (!vm_base) continue;
        if (!have) { memcpy(prev, vm_base + ea, sizeof prev); have = true; continue; }
        for (uint32_t i = 0; i < sizeof prev; i++) {
            uint8_t v = vm_base[ea + i];
            if (v != prev[i]) {
                fprintf(stderr, "[lle-watch] +%02X: %02X -> %02X\n", i, prev[i], v);
                prev[i] = v;
            }
        }
    }
    return 0;
}
#endif

extern "C" void gow_lle_libsre_load(void)
{
    /* GOW_GUARD=<hex ea>: arm the runtime's page-guard write watch on that word
     * (reports the guest function and call chain of every write to its line). */
    if (const char* g = getenv("GOW_GUARD")) {
        ppu_guard_page((uint32_t)strtoul(g, 0, 16));
    }
#ifdef _WIN32
    if (getenv("GOW_LLE_WATCH")) CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    if (const char* d = getenv("GOW_LLE_DUMP"))
        CreateThread(NULL, 0, dump_thread, (void*)(uintptr_t)strtoul(d, 0, 0), 0, NULL);
#endif
    const char* mode = getenv("GOW_LLE");
    if (!mode || !*mode || !strcmp(mode, "off")) {
        fprintf(stderr, "[lle] libsre: off (pure HLE)\n");
        return;
    }
    const bool allhle = !strcmp(mode, "all-hle");

    const char* path = getenv("GOW_LLE_IMAGE");
    if (!path || !*path) path = "local/lle/out/libsre_image.bin";   /* relative to the working dir */
    uint32_t sz = 0;
    uint8_t* img = prx_image_load_file(path, &sz);
    if (!img) {
        fprintf(stderr, "[lle] libsre image '%s' not found -- staying on HLE\n", path);
        return;
    }

    prx_module m;
    memset(&m, 0, sizeof m);
    m.name         = "libsre";
    m.base         = LIBSRE_BASE;
    m.image        = img;
    m.image_size   = sz;
    m.funcs        = libsre_function_table;
    m.func_count   = libsre_function_table_count;
    m.exports      = libsre_exports;
    m.export_count = libsre_export_count;
    prx_load_result r = prx_load_module(&m, reg);
    free(img);
    if (!r.ok) return;
    g_spurs_lle = 1;   /* HLE taskset family adopts the instance libsre creates */

    /* The real SPURS kernel (lifted image 11) DMAs each workload's policy
     * module into LS 0xA00 and branches there. Tell the dispatcher which lifted
     * image a GET from each module's EA makes resident:
     *   10 = the WWS policy module in the EBOOT,
     *   12 = libsre's system-service module, 13 = libsre's taskset module. */
    spu_overlay_register_source(0x0077CA80u, 10);
    spu_overlay_register_source(LIBSRE_BASE + 0x21480u, 12);
    spu_overlay_register_source(LIBSRE_BASE + 0x23680u, 13);

    /* Build the force-HLE list consumed by ppu_hle.cpp (substring match on the
     * 8-hex-digit NID; the variable name is historical). */
    std::string force;
    if (allhle) {
        char b[16];
        for (uint32_t i = 0; i < libsre_export_count; i++) {
            snprintf(b, sizeof b, "%08X,", libsre_exports[i].nid);
            force += b;
        }
    } else {
        for (const char* k : kKeepHle) { force += k; force += ','; }
        if (const char* extra = getenv("GOW_LLE_FORCE_HLE")) force += extra;
    }
#ifdef _WIN32
    _putenv_s("YDKJ_FORCE_HLE", force.c_str());
#else
    setenv("YDKJ_FORCE_HLE", force.c_str(), 1);
#endif
    fprintf(stderr, "[lle] libsre mode=%s exports=%u force-HLE entries=%zu\n",
            mode, prx_export_registry_count(), (size_t)std::count(force.begin(), force.end(), ','));
}

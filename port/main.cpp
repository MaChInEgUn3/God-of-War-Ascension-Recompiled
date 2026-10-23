/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
/* K32GetProcessMemoryInfo for the heartbeat's memory columns (kernel32, no psapi.lib). */
#include <psapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
int      cellGcm_park_spin(unsigned max_us);  /* -1 not parked, 1 released, 0 timeout */
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(PS3RECOMP_RSX_VULKAN)
extern "C" int  rsx_vulkan_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_vulkan_backend_present(void);
extern "C" int  rsx_vulkan_backend_pump_messages(void);
#  define rsx_backend_init    rsx_vulkan_backend_init
#  define rsx_backend_present rsx_vulkan_backend_present
#  define rsx_backend_pump    rsx_vulkan_backend_pump_messages
#  define RSX_BACKEND_NAME    "Vulkan"
#elif defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

static void present_guest_frame(void)
{
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

/* GOW_CLOCK_WATCH=<s>: the frame clock records which step it is in; a watchdog
 * names the step when the clock makes no progress for s seconds (a blocked
 * guest vblank/flip handler or present stops every FIFO drain with it). */
extern "C" void cellGcm_walk_report(void);
extern "C" void ppu_dump_thread_stack(unsigned tid, const char* tag);
extern "C" volatile int g_rsx_phase;
#if defined(PS3RECOMP_RSX_VULKAN)
extern "C" volatile int g_vk_fence_waiting;
#else
static volatile int g_vk_fence_waiting = -1;
#endif
static volatile LONG g_clock_phase = 0, g_clock_beats = 0;
static const char* const k_clock_phase[] = { "sleep", "TickVBlank", "TickFlip", "present(tick)",
    "process_fifo(tick)", "present(outer)", "process_fifo(outer)", "pump", "present(flip)" };
#define CLOCK_PHASE(p) (g_clock_phase = (p), InterlockedIncrement(&g_clock_beats))
static DWORD WINAPI clock_watch(LPVOID arg)
{
    const DWORD ms = (DWORD)(uintptr_t)arg;
    LONG last = -1; int reported = 0;
    unsigned last_flip = ~0u; int flip_reported = 0;
    for (;;) {
        Sleep(ms);
        /* Flips stopped while the clock still beats: report the walker once. */
        unsigned fc = cellGcm_flip_request_count();
        if (fc == last_flip && fc) {
            if (!flip_reported++) {
                fprintf(stderr, "[clock-watch] no flip for %lu ms (count %u)\n", (unsigned long)ms, fc);
                cellGcm_walk_report();
            }
            /* ...and where the main thread is, three snapshots one period apart. */
            if (flip_reported <= 3) ppu_dump_thread_stack(1, "stall-main");
        } else flip_reported = 0;
        last_flip = fc;
        LONG b = g_clock_beats;
        if (b == last) {
            if (!reported++) {
                LONG p = g_clock_phase;
                fprintf(stderr, "[clock-watch] frame clock stuck for %lu ms in %s\n", (unsigned long)ms,
                        p >= 0 && p < (LONG)(sizeof k_clock_phase / sizeof *k_clock_phase) ? k_clock_phase[p] : "?");
                fflush(stderr);
                if (p == 4 || p == 6) {
                    cellGcm_walk_report();
                    fprintf(stderr, "[clock-watch] rsx_phase=0x%X vk_fence_waiting=%d\n",
                            g_rsx_phase, g_vk_fence_waiting);
                }
            }
        } else reported = 0;
        last = b;
    }
    return 0;
}

/* GOW_HEARTBEAT=<s>: every s seconds, the flip count and GoW:A's game frame
 * counter (u32 at 0x9710C8), so a long run that never stalls still says how far
 * the game got. */
static DWORD WINAPI heartbeat(LPVOID arg)
{
    const DWORD ms = (DWORD)(uintptr_t)arg;
    const ULONGLONG t0 = GetTickCount64();
    for (;;) {
        Sleep(ms);
        const uint8_t* p = vm_base ? vm_base + 0x9710C8u : nullptr;
        const unsigned frame = p ? ((unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]) : 0;
        /* ws/priv: this process's working set and private commit (MB), to tell a run
         * that grows from one the rest of the machine squeezed. */
        PROCESS_MEMORY_COUNTERS_EX mc = {};
        mc.cb = sizeof mc;
        K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&mc, sizeof mc);
        fprintf(stderr, "[heartbeat] t=%llus flips=%u game_frame=%u presents=%ld ws=%lluMB priv=%lluMB\n",
                (unsigned long long)((GetTickCount64() - t0) / 1000), cellGcm_flip_request_count(), frame,
                (long)g_frames_presented, (unsigned long long)(mc.WorkingSetSize >> 20),
                (unsigned long long)(mc.PrivateUsage >> 20));
        fflush(stderr);
    }
    return 0;
}

/* GOW_MAIN_PROF=<s>: sample the main PPU thread's guest call chain every 2 ms and
 * print, every s seconds, the most frequent chains (innermost caller first) with
 * their share of samples -- where a frame's time goes (waiting on SPU work vs
 * game code). */
extern "C" int ppu_thread_chain(unsigned tid, uint32_t* fn, uint32_t* ra, int max);
static DWORD WINAPI main_prof(LPVOID arg)
{
    const ULONGLONG win = (ULONGLONG)(uintptr_t)arg;
    enum { NB = 512, DEPTH = 3 };
    static uint32_t key[NB][DEPTH]; static unsigned cnt[NB];
    ULONGLONG w0 = GetTickCount64(); unsigned total = 0, fail = 0;
    for (;;) {
        Sleep(2);
        uint32_t fn[DEPTH] = {0, 0, 0}, ra[DEPTH];
        uint32_t lfn = 0, lr = 0;
        ppu_thread_chain(1, &lfn, &lr, -1);   /* live LR: the most recent call */
        const int n = ppu_thread_chain(1, fn, ra, DEPTH);
        if (n > 0) { fn[0] = ra[0]; fn[2] = lr; }  /* key: innermost call site, its fn, live LR */
        total++;
        if (n <= 0) fail++;
        else {
            int b = -1;
            for (int i = 0; i < NB; i++) {
                if (cnt[i] && key[i][0] == fn[0] && key[i][1] == fn[1] && key[i][2] == fn[2]) { b = i; break; }
                if (!cnt[i] && b < 0) b = i;
            }
            if (b >= 0) { if (!cnt[b]) memcpy(key[b], fn, sizeof fn); cnt[b]++; }
        }
        if (GetTickCount64() - w0 >= win) {
            fprintf(stderr, "[main-prof] %u samples (%u%% no chain):\n", total, total ? 100 * fail / total : 0);
            for (int r = 0; r < 10; r++) {
                int best = -1;
                for (int i = 0; i < NB; i++) if (cnt[i] && (best < 0 || cnt[i] > cnt[best])) best = i;
                if (best < 0) break;
                fprintf(stderr, "[main-prof]   %5.1f%%  ra %08X in %08X  lr %08X\n", 100.0 * cnt[best] / total,
                        key[best][0], key[best][1], key[best][2]);
                cnt[best] = 0;
            }
            memset(cnt, 0, sizeof cnt); total = fail = 0; w0 = GetTickCount64();
            fflush(stderr);
        }
    }
    return 0;
}

static DWORD WINAPI frame_clock(LPVOID)
{
    { const char* m = getenv("GOW_MAIN_PROF");
      if (m && atoi(m) > 0) CreateThread(NULL, 0, main_prof, (LPVOID)(uintptr_t)(atoi(m) * 1000), 0, NULL); }
    { const char* w = getenv("GOW_CLOCK_WATCH");
      if (w && atoi(w) > 0) CreateThread(NULL, 0, clock_watch, (LPVOID)(uintptr_t)(atoi(w) * 1000), 0, NULL); }
    { const char* h = getenv("GOW_HEARTBEAT");
      if (h && atoi(h) > 0) CreateThread(NULL, 0, heartbeat, (LPVOID)(uintptr_t)(atoi(h) * 1000), 0, NULL); }
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    ULONGLONG next_tick = GetTickCount64();

    for (;;) {
        CLOCK_PHASE(0);
        /* GCM_PARK_SPIN_US=<us> (default 4000, 0 = off): when the last walk ended on
         * a self-JUMP park, yield-poll that word instead of sleeping the tick, so
         * the walker follows each EDGE segment patch at once (the first level ran
         * at ~1 frame/s on 4 ms naps per hole). */
        { static int s_ps = -1;
          if (s_ps < 0) { const char* e = getenv("GCM_PARK_SPIN_US"); s_ps = e ? atoi(e) : 4000; }
          if (s_ps <= 0 || cellGcm_park_spin((unsigned)s_ps) < 0) Sleep(4); }
        ULONGLONG now = GetTickCount64();

        int fired = 0;
        while ((long long)(now - next_tick) >= 0 && fired < 240) {
            CLOCK_PHASE(1);
            cellGcmTickVBlank();
            CLOCK_PHASE(2);
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                CLOCK_PHASE(3);
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            CLOCK_PHASE(4);
            if (rsx_ok) cellGcm_rsx_process_fifo();
            /* ~60 Hz. GOW_VBLANK_MS=<ms> (diagnostic) changes the vblank period:
             * whether the game logic is paced by vsync shows as a frame rate that
             * scales with it. */
            { static int s_vb = -1; if (s_vb < 0) { const char* e = getenv("GOW_VBLANK_MS");
                  s_vb = (e && atoi(e) > 0) ? atoi(e) : 16; }
              next_tick += (ULONGLONG)s_vb; }
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                CLOCK_PHASE(5);
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            CLOCK_PHASE(6);
            cellGcm_rsx_process_fifo();

            CLOCK_PHASE(7);
            if (rsx_backend_pump() != 0) {
                rsx_ok = 0;              /* window closed */
                continue;
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            CLOCK_PHASE(8);
            if (fc != last_flip) {
                present_guest_frame();
                last_flip = fc;
            } else if (fc == 0) {
                rsx_backend_present();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

extern "C" void cellGame_init_from_paramsfo(const char* sfo_path);

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is committed up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left. */
    vm_base = (uint8_t*)calloc(1, 0xE0000000u);
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

#ifdef _WIN32
/* GOW_SCAN=<hex bytes>: poll committed guest memory for a byte pattern and log each new
 * hit once (guest EA + ms since start). Answers "did this data ever reach guest memory". */
/* GOW_PEEK=<hex ea>,<n words>[;<hex ea>,<n>...]: log those big-endian words whenever they
 * change (polled every 20 ms). Up to 8 regions of up to 32 words; one line per changed region. */
static DWORD WINAPI guest_peek_thread(LPVOID)
{
    enum { MAXR = 8 };
    uint32_t ea[MAXR], n[MAXR], prev[MAXR][32] = {{0}}; int nr = 0;
    for (const char* e = getenv("GOW_PEEK"); e && *e && nr < MAXR; ) {
        char* p = nullptr;
        ea[nr] = (uint32_t)strtoul(e, &p, 16);
        n[nr] = (p && *p == ',') ? (uint32_t)strtoul(p + 1, &p, 0) : 4;
        if (n[nr] > 32) n[nr] = 32;
        nr++;
        e = p ? strchr(p, ';') : nullptr;
        if (e) e++;
    }
    DWORD t0 = GetTickCount();
    for (;;) {
        Sleep(20);
        for (int r = 0; r < nr; r++) {
            uint32_t cur[32];
            for (uint32_t i = 0; i < n[r]; i++) {
                const uint8_t* q = vm_base + ea[r] + i * 4;
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(q, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) { cur[i] = 0; continue; }
                cur[i] = (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3];
            }
            if (memcmp(cur, prev[r], n[r] * 4)) {
                char line[512]; int k = snprintf(line, sizeof line, "[GOW_PEEK] t=%lums @%08X:",
                                                 (unsigned long)(GetTickCount() - t0), ea[r]);
                for (uint32_t i = 0; i < n[r] && k < (int)sizeof line - 10; i++)
                    k += snprintf(line + k, sizeof line - k, " %08X", cur[i]);
                line[k++] = 10; line[k] = 0;
                fputs(line, stderr);      /* one write: less interleaving with other threads */
                memcpy(prev[r], cur, n[r] * 4);
            }
        }
    }
}

/* GOW_HEAPWALK=<hex sentinel>: walk the game heap's circular free list (next at +4,
 * size at +0) every 50 ms and report the first link that leaves the list: a next of
 * 0 or outside [sentinel-256MB, sentinel+256MB), or a walk longer than 1M nodes.
 * Prints the node holding the bad link, its predecessor and their words. */
static DWORD WINAPI guest_heapwalk_thread(LPVOID)
{
    enum { MAXS = 8 };
    uint32_t sv[MAXS]; int ns = 0;          /* GOW_HEAPWALK=<hex>[,<hex>...] */
    for (const char* e = getenv("GOW_HEAPWALK"); e && *e && ns < MAXS; ) {
        char* p = nullptr; sv[ns++] = (uint32_t)strtoul(e, &p, 16);
        e = (p && *p == ',') ? p + 1 : nullptr;
    }
    auto rd = [](uint32_t ea) -> uint32_t {
        const uint8_t* q = vm_base + ea;
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(q, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0xDEADDEADu;
        return (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3];
    };
    DWORD t0 = GetTickCount(); int reported[MAXS] = {0}, ok[MAXS] = {0};
    for (;;) {
        Sleep(50);
        for (int k = 0; k < ns; k++) {
            const uint32_t s = sv[k];
            uint32_t prev = s, n = rd(s + 4), steps = 0;
            if ((n == 0xDEADDEADu || n == 0) && !ok[k]) continue;   /* heap not built yet */
            while (n != s && steps < 1000000) {
                if (n == 0 || n == 0xDEADDEADu || n + 0x10000000u < s || n > s + 0x10000000u) break;
                prev = n; n = rd(n + 4); steps++;
            }
            if (n == s) { if (!ok[k]) fprintf(stderr, "[heapwalk] t=%lums heap 0x%08X list ok (%u free nodes)\n",
                                              (unsigned long)(GetTickCount() - t0), s, steps);
                          ok[k] = 1; continue; }
            /* re-walk once: the game mutates the list concurrently */
            Sleep(5);
            uint32_t p2 = s, n2 = rd(s + 4), st2 = 0;
            while (n2 != s && st2 < 1000000) {
                if (n2 == 0 || n2 == 0xDEADDEADu || n2 + 0x10000000u < s || n2 > s + 0x10000000u) break;
                p2 = n2; n2 = rd(n2 + 4); st2++;
            }
            if (n2 == s || p2 != prev) continue;
            if (reported[k]++ < 3)
                fprintf(stderr, "[heapwalk] t=%lums heap 0x%08X BAD link after %u nodes: node 0x%08X -> next 0x%08X "
                        "(node words %08X %08X %08X %08X)\n", (unsigned long)(GetTickCount() - t0), s, steps,
                        prev, n, rd(prev), rd(prev + 4), rd(prev + 8), rd(prev + 12));
        }
    }
}

static DWORD WINAPI guest_scan_thread(LPVOID)
{
    const char* hex = getenv("GOW_SCAN");
    uint8_t pat[64]; size_t plen = 0;
    for (; hex[0] && hex[1] && plen < sizeof pat; hex += 2) {
        unsigned v; char b[3] = { hex[0], hex[1], 0 };
        if (sscanf(b, "%x", &v) != 1) break;
        pat[plen++] = (uint8_t)v;
    }
    if (!plen) return 0;
    DWORD t0 = GetTickCount();
    static uint32_t seen[64]; int nseen = 0;
    for (;;) {
        Sleep(250);
        uint8_t* p = vm_base;
        while (p < vm_base + VM_SIZE) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(p, &mbi, sizeof mbi)) break;
            if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
                uint8_t* r = (uint8_t*)mbi.BaseAddress;
                for (size_t i = 0; i + plen <= mbi.RegionSize; ) {
                    uint8_t* q = (uint8_t*)memchr(r + i, pat[0], mbi.RegionSize - plen + 1 - i);
                    if (!q) break;
                    i = (size_t)(q - r) + 1;
                    if (memcmp(q, pat, plen)) continue;
                    uint32_t ea = (uint32_t)(q - vm_base);
                    bool dup = false;
                    for (int k = 0; k < nseen; k++) dup |= seen[k] == ea;
                    if (!dup && nseen < 64) {
                        seen[nseen++] = ea;
                        fprintf(stderr, "[GOW_SCAN] hit ea=0x%08X t=%lums\n", ea, (unsigned long)(GetTickCount() - t0));
                    }
                }
            }
            p = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
        }
    }
}
#endif

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

extern "C" void gow_mlaa_hle_register(void);   /* gow_mlaa_hle.c */

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
    /* Title defaults for runtime knobs (an explicit value in the environment wins).
     * GCM_PARK_WAIT: GoW:A parks the RSX on a self-JUMP and appends the next FIFO
     * segment behind it before patching the park; following `put` instead skipped
     * the GET_REPORTs that release the RsxSync job list. */
    if (!getenv("GCM_PARK_WAIT")) _putenv_s("GCM_PARK_WAIT", "1");
    /* GCM_SEMA_ACQUIRE: the RSX must block on NV406E SEMAPHORE_ACQUIRE until the
     * SPU post-process tasks release their label; running ahead let RsxSync post
     * the next batch over the tasks' control block and a batch was lost. */
    if (!getenv("GCM_SEMA_ACQUIRE")) _putenv_s("GCM_SEMA_ACQUIRE", "1");
    /* GCM_FIFO_CIRCLE_PASSES=0: the walker legitimately spends whole drains
     * inside GoW:A's FIFO (JUMP chains waiting for a patch); the "circling"
     * resync to `put` threw the rest of the frame away and the game froze. */
    if (!getenv("GCM_FIFO_CIRCLE_PASSES")) _putenv_s("GCM_FIFO_CIRCLE_PASSES", "0");
    /* VK_TEX_OFF=1 until guest shaders exist (glslang missing): the Vulkan
     * backend decodes the whole mip chain on the CPU at every BEGIN that has a
     * dirty texture unit, and GoW:A re-sends its texture state per draw, so one
     * drain took minutes and froze the frame clock. Set VK_TEX_OFF=0 to decode. */
    if (!getenv("VK_TEX_OFF")) _putenv_s("VK_TEX_OFF", "1");
    /* SPURS_PM_HANG=10: the port runs a SPURS instance's policy modules on one
     * host thread, so one stuck run stops every workload; name it when it happens
     * (SPURS_PM_HANG=0 turns the report off). */
    if (!getenv("SPURS_PM_HANG")) _putenv_s("SPURS_PM_HANG", "10");
    /* SPURS_PASS_GREEDY=1: no 1 ms pause after a kernel pass that found work. Each
     * frame is a chain of dependent jobs across workloads; a timer tick per hop
     * halved the frame rate (measured 2.95 -> 6.25 game frames/s). */
    if (!getenv("SPURS_PASS_GREEDY")) _putenv_s("SPURS_PASS_GREEDY", "1");
    /* SPURS_PM_RERUN_US=2000: after a module run that executed jobs, run the same
     * workload again at once (claim kept) for up to 2 ms. The WWS policy module
     * hands the SPU back after every job, so an EDGE list advanced one job per
     * kernel pass. Swapped 2+2 A/B in the level, tail (600-1080 s): 1.494/1.390/
     * 1.402/1.419 vs 1.392/1.271/1.294/1.298 game frames/s, no hang. Set 0 to go back. */
    if (!getenv("SPURS_PM_RERUN_US")) _putenv_s("SPURS_PM_RERUN_US", "2000");
    /* SPURS_KERN_THREADS=2: two host kernel threads run the "Big" instance's policy
     * modules side by side (measured 8.8 -> ~12 game frames/s). The instance has 6
     * SPUs, but 3, 4 and 6 threads measured slower (9.2-10.1): the frame's job chain
     * has too little parallel work and the extra threads only add lock contention. */
    /* SPURS_KERN_SPUS=0,1,5: a third kernel thread plays SPU 5, where BrBDblBufWrkld
     * (the audio DSP rack, priority 1 there and 0 elsewhere) lives, as on hardware.
     * Sharing two threads, the free-running audio workload took ~57% of one and the
     * EDGE jobs waited behind it. Level, frame split from PS3_POLLTOP (two runs each):
     * main-thread wait 458-483 -> 389-390 ms/frame, game frames ~80-85 -> ~100 per
     * minute. It used to corrupt 3 of 8 level runs between 510 and 930 s; those
     * were the RSX FIFO park race (SPU_PUT_HEAD_LAST below) and the VRAM guard
     * being off for high-range descriptor tables, which the extra speed exposed
     * more often. With both fixed: 7 runs of 1080 s, 0 corrupted, level rate
     * (analysis/level_rate.py) 1.18-1.25 vs 1.10-1.18 game frames/s with two
     * threads, the same order with the two instances swapped.
     * SPURS_KERN_SPUS=0,1,2,5 (the default now): SPU 2 too, where EDGE0-2 (priority
     * 8 on SPUs 0-3) may run. Without SPURS_PM_RERUN_US it measured neutral; with it,
     * tail 1.542/1.467/1.508/1.479 vs 1.429/1.371/1.402/1.400 (swapped 2+2), no hang
     * or bad size in 8 runs. SPURS_KERN_SPUS=0,1,5 or 0,1 goes back. */
    if (!getenv("SPURS_KERN_SPUS") && !getenv("SPURS_KERN_THREADS")) _putenv_s("SPURS_KERN_SPUS", "0,1,2,5");
    if (!getenv("SPURS_KERN_THREADS")) _putenv_s("SPURS_KERN_THREADS", "2");
    /* PS3_VERBOSE=0: the per-event [WAIT]/[evt] lines default ON whenever stderr is
     * redirected, and at hundreds per second from every thread through one FILE
     * lock they skew exactly the runs that measure timing. Set PS3_VERBOSE=1 to
     * get them back. */
    if (!getenv("PS3_VERBOSE")) _putenv_s("PS3_VERBOSE", "0");
    /* PPU_TRACE_ADDRS=0: the lift instruments every guest function entry
     * (PPU_TRACE_FUNCS=all); with no filter the first 4000 entries print an [FN]
     * line each. Filter to address 0 (none) unless a trace is asked for. */
    if (!getenv("PPU_TRACE_ADDRS")) _putenv_s("PPU_TRACE_ADDRS", "0");
    /* VK_DRAW_BATCH=1: record draws into one submission instead of a submit and
     * fence wait per draw. In the first level the per-draw wait held the FIFO
     * walker on the GPU 46% of the time and the SPU geometry jobs on RSX ring
     * space. A/B with matched scene entries (instance B): 1.593 vs 1.141 and
     * 2.089 vs 1.852 game frames/s, the same order in the other two pairs, no
     * hang or bad frame in 8 runs. VK_DRAW_BATCH=0 goes back. */
    if (!getenv("VK_DRAW_BATCH")) _putenv_s("VK_DRAW_BATCH", "1");
    /* VK_ASYNC=1: batch flushes submit without waiting (up to 3 in flight). Alone it
     * was neutral -- the SPU side was as long as the walker -- but with the EDGE
     * lists run back to back on four kernel threads (below) it pays: tail 1.590/
     * 1.523/1.525/1.512 vs 1.502/1.435/1.456/1.415 (swapped 2+2), no hang. */
    if (!getenv("VK_ASYNC")) _putenv_s("VK_ASYNC", "1");
    /* SPURS_PM_ARG_BE=1: hand the wwsjob PM its 64-bit workload argument as the
     * hardware does (high half in word 0). With the low half in word 0 the PM
     * wrote status rows into BatchJobList's list 1 and the level stopped on
     * "BATCHJOB: Waiting for batch job list 1 to finish (22/21)". */
    if (!getenv("SPURS_PM_ARG_BE")) _putenv_s("SPURS_PM_ARG_BE", "1");
    /* SPU_PUT_HEAD_LAST=1: an EDGE job releases an RSX FIFO park (a self-JUMP)
     * with the same SPU PUT that carries the new segment, park word first. With
     * a plain front-to-back copy the walker saw the release and read on into
     * words not written yet (valid up to +0x24, stale at +0x28), decoded junk,
     * hit a JUMP to unmapped IO and stopped: the RSX ring label never advanced
     * and the EDGE job waiting for ring space froze the game (3 of 12 runs).
     * Storing the head last, behind a store fence, publishes the release after
     * the segment. 0 restores the plain copy. */
    if (!getenv("SPU_PUT_HEAD_LAST")) _putenv_s("SPU_PUT_HEAD_LAST", "1");
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
    /* Windows 11 ignores a process's timer resolution request while it has no
     * visible window (and may run it under EcoQoS). Offscreen, every Sleep(1) of
     * the SPURS kernel threads took ~15 ms (measured with PS3_FTRACE: 12-16 ms
     * per pause, 22-75% of each thread's time), so a thread that ran out of work
     * missed the next EDGE list. Opt out of both. GOW_TIMER_FIX=0 skips it. */
    { const char* e = getenv("GOW_TIMER_FIX");
      if (!e || *e != '0') {
          PROCESS_POWER_THROTTLING_STATE st = {};
          st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
          st.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION |
                           PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
          st.StateMask = 0;          /* honour the resolution; no execution-speed throttling */
          if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &st, sizeof st))
              fprintf(stderr, "[timer] SetProcessInformation(ProcessPowerThrottling) failed: %lu\n",
                      (unsigned long)GetLastError());
      } }
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
#endif

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    { extern void ps3_sampler_start(void); ps3_sampler_start(); } /* PS3_SAMPLE=<ms> */
#ifdef _WIN32
    gow_mlaa_hle_register();   /* after the SPU ctors: replaces the lifted MLAA tasks */
    if (getenv("GOW_SCAN")) CreateThread(NULL, 0, guest_scan_thread, NULL, 0, NULL);
    if (getenv("GOW_HEAPWALK")) CreateThread(NULL, 0, guest_heapwalk_thread, NULL, 0, NULL);
    if (getenv("GOW_PEEK")) CreateThread(NULL, 0, guest_peek_thread, NULL, 0, NULL);
#endif

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    /* Real title id / name from the disc's PARAM.SFO (cellGame otherwise keeps
     * its BLES00000 placeholder, which the game then uses to build file names
     * such as INTRO/BOOT_<TITLEID>.TXT). */
    {
        static const char* const kSfo[] = { "%s/PS3_GAME/PARAM.SFO", "%s/../PARAM.SFO", "%s/PARAM.SFO" };
        char sfo[1100];
        int found = 0;
        for (size_t i = 0; i < sizeof kSfo / sizeof kSfo[0] && !found; i++) {
            snprintf(sfo, sizeof sfo, kSfo[i], ppu_vfs_root);
            if (FILE* f = fopen(sfo, "rb")) { fclose(f); found = 1; }
        }
        if (found) cellGame_init_from_paramsfo(sfo);
    }

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}

/*
 * ps3recomp game project -- game-specific hooks and overrides.
 *
 * main.cpp is the runner and is game-agnostic. This file is where a port puts
 * everything that is not: the PRX loading hook it must answer, overrides for
 * firmware functions the HLE library gets wrong for this title, and patches
 * over recompiled code.
 *
 * It starts out as the empty version of all of that, which is what a game with
 * no lifted PRX and no overrides needs.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

/* ---------------------------------------------------------------------------
 * PRX loading
 *
 * main.cpp calls this once, after the lifted function table is registered and
 * vm_base is live, before the game runs. A title that lifts a real system PRX
 * -- libsre, which is cellSpurs and cellSync -- loads its image into guest RAM
 * here and registers its exports. Most ports never need to: the HLE library
 * answers those modules instead.
 * -----------------------------------------------------------------------*/

#ifdef GOW_LLE_LIBSRE
extern "C" void gow_lle_libsre_load(void);
#endif

extern "C" uint32_t ppu_hle_inject_base;   /* libs/video/cellGcmSys.c */

extern "C" void ps3_load_prx_modules(void)
{
    /* GoW Ascension reserves a 512 MB heap pool at 0x20000000 (as real lv2 does),
     * which is where the runtime's GCM label/control/offset-table window used to
     * live. Move that window clear of the pool. */
    { const char* e = getenv("GOW_INJECT_BASE");
      ppu_hle_inject_base = e ? (uint32_t)strtoul(e, nullptr, 16) : 0x70000000u; }
#ifdef GOW_LLE_LIBSRE
    gow_lle_libsre_load();   /* firmware SPURS (libsre) via LLE, see lle_libsre.cpp */
#endif
}

/* ---------------------------------------------------------------------------
 * Example: Override a specific NID
 *
 * The module system resolves function imports by NID.  To replace a default
 * HLE stub with custom logic, register your function at init time.
 * -----------------------------------------------------------------------*/

/*
 * Example: override cellGcmSetFlipMode (NID 0xA53D12AE) to force vsync.
 *
 *   static void my_cellGcmSetFlipMode(u32 mode)
 *   {
 *       printf("[hook] cellGcmSetFlipMode: forcing VSYNC (was %u)\n", mode);
 *       // Force vsync regardless of what the game requested
 *       cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);
 *   }
 *
 * Then in your init code:
 *
 *   ps3::modules::override_nid(0xA53D12AE,
 *       reinterpret_cast<void*>(my_cellGcmSetFlipMode));
 */

/* ---------------------------------------------------------------------------
 * Example: Hook into module loading
 *
 * You can intercept cellSysmoduleLoadModule to perform custom actions
 * when a particular module is loaded.
 * -----------------------------------------------------------------------*/

/*
 *   static s32 my_loadmodule_hook(u16 id)
 *   {
 *       printf("[hook] Module 0x%04X loaded\n", id);
 *
 *       if (id == CELL_SYSMODULE_AUDIO) {
 *           // Perform custom audio backend initialization
 *           init_my_audio_backend();
 *       }
 *
 *       // Call the original implementation
 *       return cellSysmoduleLoadModule(id);
 *   }
 */

/* ---------------------------------------------------------------------------
 * Game-specific patches
 *
 * Sometimes you need to patch recompiled code at specific addresses.
 * Use the patch system to modify behavior without editing generated code.
 * -----------------------------------------------------------------------*/

/*
 *   // Skip an integrity check at guest address 0x00812FA0
 *   ps3::patches::nop_range(0x00812FA0, 0x00812FB0);
 *
 *   // Replace a function call with a direct return
 *   ps3::patches::force_return(0x009A1000, 0);  // return 0
 */

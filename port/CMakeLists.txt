cmake_minimum_required(VERSION 3.20)
project(MyGameRecomp
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Recompiled PS3 game project (ps3recomp)"
)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer Ninja if available
if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{CMAKE_GENERATOR})
    message(STATUS "Tip: use -G Ninja for faster builds")
endif()

# ---------------------------------------------------------------------------
# The two directories this project is built from
# ---------------------------------------------------------------------------
# PS3RECOMP_DIR is the toolkit checkout: include/, runtime/, libs/, tools/.
# RECOMP_DIR is where the lifter's output for THIS game lives. They are
# separate on purpose -- a port keeps its generated code in its own tree and
# points at whatever toolkit checkout it is building against:
#
#   cmake -B build -G Ninja \
#         -DPS3RECOMP_DIR=/path/to/ps3recomp \
#         -DRECOMP_DIR=/path/to/my_game/recompiled
#
if(NOT DEFINED PS3RECOMP_DIR)
    # Default: this project is still sitting inside ps3recomp/templates/project/
    get_filename_component(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
endif()
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)

set(RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recompiled" CACHE PATH
    "Directory holding the lifter's output: ppu_recomp.h and the lifted sources")
get_filename_component(RECOMP_DIR "${RECOMP_DIR}" ABSOLUTE)

message(STATUS "ps3recomp SDK:   ${PS3RECOMP_DIR}")
message(STATUS "Lifted output:   ${RECOMP_DIR}")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR
        "No ppu_recomp.h in ${RECOMP_DIR}.\n"
        "That header and the lifted sources beside it are what "
        "tools/ppu_lifter.py writes. Run the lifter, drop its output there, "
        "or point -DRECOMP_DIR= at wherever it already is.")
endif()

# The lifted translation units. A game's lift is hundreds of generated files,
# so they are globbed rather than listed; re-run CMake after a fresh lift.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_DIR}/*.c"
    "${RECOMP_DIR}/*.cpp"
)
if(NOT RECOMP_SOURCES)
    message(FATAL_ERROR
        "${RECOMP_DIR} has a ppu_recomp.h but no .c or .cpp beside it. The "
        "lifted functions and the function table live in those; without them "
        "there is nothing to run.")
endif()
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")

# clang-cl guards every thread-local access with an on-demand initialization check
# (/Zc:tlsGuards, meant for C++ thread_local with dynamic initializers). Nothing here
# has one, and the lifted code stores the trampoline TLS slot on every SPU and PPU
# step, so the check and its second TLS lookup run millions of times a second.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(/Zc:tlsGuards-)
endif()

# SPU single-precision ops as native instructions under round-toward-zero (the
# SPU's rounding), instead of exact emulation through doubles. Exact for
# ordinary operands; other lanes still take the exact path. spu_helpers.h,
# spu_run_with_halt. SPU_FP_CHECK=1 at run time compares every op.
option(GOW_SPU_FP_NATIVE "Native SPU single-precision ops (round toward zero)" ON)
if(GOW_SPU_FP_NATIVE)
    add_compile_definitions(SPU_FP_NATIVE=1)
endif()

# ---------------------------------------------------------------------------
# ps3recomp runtime library
# ---------------------------------------------------------------------------
# Built from source as a subdirectory rather than imported as a prebuilt
# archive. A static library carries none of its own link requirements, so an
# imported one arrives without SDL2, pthreads, the Metal frameworks or the
# Winsock/XInput imports -- the link then fails on every platform for a
# different reason. add_subdirectory hands all of that over with the target.
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses. PORT_GENERATE_HLE_NIDS=ON generates it even when the lift has
# one: a toolkit newer than the lift (build_v017 against the v015 lift) may have
# renamed handlers the lift's table still names.
option(PORT_GENERATE_HLE_NIDS "Generate the NID table from PS3RECOMP_DIR, ignoring RECOMP_DIR's" OFF)
if(NOT PORT_GENERATE_HLE_NIDS AND EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    # The lift's own table (if any) came in with the *.cpp glob; it must not
    # link beside the generated one.
    list(FILTER RECOMP_SOURCES EXCLUDE REGEX "/ppu_hle_nids\\.cpp$")
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
        COMMENT "Generating the HLE NID -> handler table")
    message(STATUS "HLE NID table:   generated (every module)")
endif()

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
# The PPU boot scaffold is compiled here rather than taken from the runtime
# library: every one of these files #includes the lifter's per-game
# ppu_recomp.h, so they can only be built against a particular lift.
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

# ---------------------------------------------------------------------------
# Lifted SPU images (optional)
# ---------------------------------------------------------------------------
# A title that runs SPU work gets its images lifted into <SPU_DIR>/<image>/ by
# tools/spu_lifter.py, with a registration TU generated by
# tools/build_spu_workloads.py. cellSpurs dispatches them by content
# fingerprint, so the generated table is keyed to the images THIS build ships:
# a table generated from another release registers signatures that can never
# match, which is worse than shipping none. Both the table and the job-module
# registry self-register from constructors and are therefore optional -- a tree
# that has not lifted its SPU images yet configures and links without them.
set(SPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spu" CACHE PATH
    "Directory holding the lifted SPU images (<SPU_DIR>/<image>/spu_recomp.c)")
option(PORT_ENABLE_SPU "Compile and register the title's lifted SPU images" ON)

set(SPU_SOURCES "")
if(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(SPU_LIFTED)
        list(LENGTH SPU_LIFTED SPU_IMAGE_COUNT)
        message(STATUS "Lifted SPU:      ${SPU_IMAGE_COUNT} image(s)")
        # The musttail indirect-branch fast path. Excluded from the runtime
        # library on purpose (see the SDK root CMakeLists): it must be built
        # here, by clang, or the tail call degrades to a plain call and a guest
        # loop that branches indirectly walks the thread stack off its guard
        # page a few thousand iterations in.
        list(APPEND SPU_SOURCES ${SPU_LIFTED}
             "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/gow_mlaa_hle.c")   # HLE of the MLAA taskset
        # Native EDGE loops (gow_edge_native.c, docs/EDGE_NATIVE.md). The d935 lift is
        # built through gen_edge_native_lift.py, which adds the GOW_EDGE_NATIVE switch
        # (default native); the native file's lifted-body macros are game code,
        # generated from the lift into the build tree. Neither is committed.
        set(EDGE_LIFT "${SPU_DIR}/jobmod_d93503647dd8_e6d98/spu_recomp.c")
        if(EXISTS "${EDGE_LIFT}")
            find_package(Python3 COMPONENTS Interpreter REQUIRED)
            set(EDGE_SWITCHED "${SPU_DIR}/jobmod_d93503647dd8_e6d98/spu_recomp_edge.c")   # beside the lift, for its includes
            add_custom_command(OUTPUT "${EDGE_SWITCHED}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_edge_native_lift.py" "${EDGE_LIFT}" "${EDGE_SWITCHED}"
                DEPENDS "${EDGE_LIFT}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_edge_native_lift.py"
                COMMENT "Adding the native EDGE switch to the d935 lift")
            list(REMOVE_ITEM SPU_SOURCES "${EDGE_LIFT}")
            list(APPEND SPU_SOURCES "${EDGE_SWITCHED}")
            set(EDGE_BODY "${CMAKE_CURRENT_BINARY_DIR}/gow_edge_e550_body.h")
            add_custom_command(OUTPUT "${EDGE_BODY}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_edge_e550_body.py" "${EDGE_LIFT}" "${EDGE_BODY}"
                DEPENDS "${EDGE_LIFT}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_edge_e550_body.py"
                COMMENT "Generating the E550 loop body from the d935 lift")
            list(APPEND SPU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/gow_edge_native.c" "${EDGE_BODY}")
            # 73E0's region at loc_79B0 (gow_edge_native_73e0.c, switch GOW_EDGE_73E0, default lifted)
            set(EDGE_BODY73 "${CMAKE_CURRENT_BINARY_DIR}/gow_edge_73e0_body.h")
            add_custom_command(OUTPUT "${EDGE_BODY73}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_edge_73e0_body.py" "${EDGE_LIFT}" "${EDGE_BODY73}"
                DEPENDS "${EDGE_LIFT}" "${CMAKE_CURRENT_SOURCE_DIR}/gen_edge_73e0_body.py"
                COMMENT "Generating the 73E0 region from the d935 lift")
            list(APPEND SPU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/gow_edge_native_73e0.c" "${EDGE_BODY73}")
        endif()
        # Generated beside the lift; each is present only once its generator
        # has been run, so each is included only if it is actually here.
        foreach(gen IN ITEMS spu_workloads.c jobmods_register.c edgefrags_register.c taskset_policy_bytes.c)
            if(EXISTS "${SPU_DIR}/${gen}")
                list(APPEND SPU_SOURCES "${SPU_DIR}/${gen}")
            else()
                message(STATUS "  (no ${gen} -- not registered)")
            endif()
        endforeach()
    else()
        message(STATUS "Lifted SPU:      none found in ${SPU_DIR}")
    endif()
endif()

# ---------------------------------------------------------------------------
# LLE of the firmware SPURS library (libsre.prx) -- optional, local only
# ---------------------------------------------------------------------------
# <LLE_DIR> holds ppu_recomp_000.cpp/ppu_recomp.h (ppu_lifter --raw --symbol-prefix
# libsre_) and libsre_exports.c (prx_exports.py). Derived from the user's own
# firmware: never commit or distribute it. See lle_libsre.cpp for the runtime
# modes (GOW_LLE=off|all-hle|real).
set(LLE_DIR "D:/GOW A/work/lle/recomp2" CACHE PATH "Lifted libsre output")
option(PORT_ENABLE_LLE_LIBSRE "Link the lifted firmware libsre (LLE SPURS)" ON)
set(LLE_SOURCES "")
if(PORT_ENABLE_LLE_LIBSRE AND EXISTS "${LLE_DIR}/ppu_recomp_000.cpp"
   AND EXISTS "${LLE_DIR}/libsre_exports.c")
    set(LLE_SOURCES "${LLE_DIR}/ppu_recomp_000.cpp" "${LLE_DIR}/libsre_exports.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/lle_libsre.cpp")
    message(STATUS "LLE libsre:      ${LLE_DIR}")
else()
    message(STATUS "LLE libsre:      disabled")
endif()

add_executable(${PROJECT_NAME}
    main.cpp
    stubs.cpp
    gow_vram_guard.c   # vetoes the VRAM-cache release with nothing resident (see patch_lift.py)
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
    ${LLE_SOURCES}
)

if(LLE_SOURCES)
# Scoped to the touched TUs: a target-wide define/include would recompile the
# ~7-minute lifted game sources.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/stubs.cpp" PROPERTIES
        COMPILE_DEFINITIONS GOW_LLE_LIBSRE=1)
    set_source_files_properties("${LLE_DIR}/libsre_exports.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/lle_libsre.cpp" PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/prx")
    if(MSVC)
        set_source_files_properties("${LLE_DIR}/ppu_recomp_000.cpp" PROPERTIES
            COMPILE_OPTIONS "/w;/bigobj")
    else()
        set_source_files_properties("${LLE_DIR}/ppu_recomp_000.cpp" PROPERTIES
            COMPILE_OPTIONS "-w;-ffp-contract=off")
    endif()
endif()

# The lifted SPU sources use bare #include "spu_context.h". Scope that include
# directory (and their flags) to those TUs alone: adding it to the whole target
# changes the compile command of every lifted PPU TU, and those are large enough
# that the needless rebuild is measured in tens of minutes.
#   SPU_USE_DISPATCH_MT -- route SPU_IB_DISPATCH through the musttail path.
#   -O2                 -- the target-wide -O1 exists for the huge PPU TUs; the
#                          SPU sources are small and decode-hot, so they get
#                          full optimization. The last -O on the line wins.
#   SPU_HOST_ARCH       -- host ISA for the lifted SPU code (default x86-64-v3 =
#                          AVX2 + FMA, any x86 CPU since ~2013; -msse4.2 for older
#                          ones). spu_helpers.h takes its pshufb paths (shufb, the
#                          quadword byte rotates) only under __SSE4_1__, and
#                          fma/fms/fnms are fmaf() -- a libm call without FMA.
#                          The default SSE2 target ran all of those as loops/calls.
#   -ffp-contract=off   -- only the explicit fmaf()s fuse, as on the SPU.
#   SPU_LS_PROBES=0     -- (GOW_SPU_LS_PROBES OFF) no diagnostic probe or
#   SPU_FP_CHECK_HOOK=0    SPU_LS_WATCH check on each LS access of lifted code,
#                          and no SPU_FP_CHECK call after each float op; those
#                          env diagnostics then see only runtime code.
option(GOW_SPU_LS_PROBES "LS probes and the SPU_FP_CHECK hook in lifted SPU code" OFF)
set(SPU_HOST_ARCH "-march=x86-64-v3" CACHE STRING "Host ISA flag for lifted SPU sources")
if(SPU_SOURCES)
    set(_spu_defs SPU_USE_DISPATCH_MT)
    if(NOT GOW_SPU_LS_PROBES)
        list(APPEND _spu_defs SPU_LS_PROBES=0 SPU_FP_CHECK_HOOK=0)
    endif()
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu"
        COMPILE_DEFINITIONS "${_spu_defs}")
    if(NOT MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # clang-cl ignores a bare -ffp-contract=off ("unknown argument ignored")
        # and contracts a*b+c within a statement by default.
        if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            set(_spu_nocontract "/clang:-ffp-contract=off")
        else()
            set(_spu_nocontract "-ffp-contract=off")
        endif()
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS
            "-O2;${SPU_HOST_ARCH};${_spu_nocontract}")
    endif()
endif()

# app.rc carries the icon, the version block and the manifest, and needs a
# resource compiler. Listing it as a source on a host that has none is not a
# link error, it is a "no rule to make" for a file nothing can consume.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${RECOMP_DIR}"                            # the generated ppu_recomp.h
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"        # win32_compat.h and friends
    "${PS3RECOMP_DIR}/libs/video"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"              # generated gow_edge_e550_body.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ps3recomp_runtime
)

# The runtime is C++, and on Apple also Objective-C. Letting CMake infer the
# link language from the generated C would leave the C++ standard library out.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Windows system import libraries. dbghelp symbolizes crash-handler addresses
# against the PDB, user32 and gdi32 are what the backend's window and its GDI
# present are made of, and winmm is timeBeginPeriod. None of the four names
# anything a POSIX linker can find; the D3D12 backend's own imports come in
# through the #pragma comment(lib) in libs/video/rsx_d3d12_backend.c.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dbghelp
        user32
        gdi32
        winmm
    )
endif()

# ---------------------------------------------------------------------------
# Compiler-specific flags
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fno-strict-aliasing
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3 /wd4100 /wd4244 /wd4267
        /Zc:__cplusplus         # report correct __cplusplus value
        # ppu_recomp.h defines PPU_FENCE, the lowering of the guest's
        # sync/lwsync/eieio/isync, and in C that is <stdatomic.h>. The lifter
        # writes C++ by default, but --single-file writes ppu_recomp.c, and a
        # port that took it would not compile without this. The runtime's own
        # build already carries the same flag for the same reason.
        /experimental:c11atomics
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# The lifted translation units are machine-generated and can run to hundreds of
# thousands of lines each, so they get their own flags:
#
#   - warnings off. They are about generated code and nobody will act on them,
#     and at this volume they bury the diagnostics that matter.
#   - no floating-point contraction. arm64 fuses a multiply and an add by
#     default; a PPC fmadd is spelt as one where the lifter means one.
#   - /bigobj on MSVC. A single lifted file routinely passes the 65,536-section
#     limit of the default object format.
if(MSVC)
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off")
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

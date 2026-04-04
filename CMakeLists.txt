cmake_minimum_required(VERSION 3.26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(synth C)

# ---------------------------------------------------------------------------
# Language standard & default build type
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug | Release)" FORCE)
endif()

# ---------------------------------------------------------------------------
# Optimisation profile
#   none     – no extra flags (default)
#   sse2     – SSE2 + no-math-errno
#   avx2     – AVX2 + finite-math + no-math-errno
#   fast     – O3 + march=native + ffast-math + vectorise
#   overkill – everything; binary tied to build machine
# ---------------------------------------------------------------------------
set(OPT_PROFILE "none" CACHE STRING
    "Optimisation profile: none | sse2 | avx2 | fast | overkill")
set_property(CACHE OPT_PROFILE PROPERTY STRINGS none sse2 avx2 fast overkill)

message(STATUS "[OPT_PROFILE] ${OPT_PROFILE}")

# ---------------------------------------------------------------------------
# Source layout
# ---------------------------------------------------------------------------
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB_RECURSE SYNTH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
if(NOT SYNTH_SOURCES)
    message(FATAL_ERROR "No sources found in src/*.c")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# ---------------------------------------------------------------------------
# Git metadata
# ---------------------------------------------------------------------------
find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --long --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_DESCRIBE_RES
        OUTPUT_VARIABLE GIT_DESCRIBE_OUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_COMMIT_RES
        OUTPUT_VARIABLE GIT_COMMIT_OUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_STATUS_OUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(PROJECT_GIT_DESCRIBE $<IF:$<EQUAL:${GIT_DESCRIBE_RES},0>,${GIT_DESCRIBE_OUT},unknown>)
    set(PROJECT_GIT_COMMIT   $<IF:$<EQUAL:${GIT_COMMIT_RES},0>,${GIT_COMMIT_OUT},unknown>)

    if(GIT_DESCRIBE_RES EQUAL 0)
        set(PROJECT_GIT_DESCRIBE "${GIT_DESCRIBE_OUT}")
    else()
        set(PROJECT_GIT_DESCRIBE "unknown")
    endif()

    if(GIT_COMMIT_RES EQUAL 0)
        set(PROJECT_GIT_COMMIT "${GIT_COMMIT_OUT}")
    else()
        set(PROJECT_GIT_COMMIT "unknown")
    endif()

    if(GIT_STATUS_OUT STREQUAL "")
        set(PROJECT_GIT_DIRTY "0")
    else()
        set(PROJECT_GIT_DIRTY "1")
    endif()
else()
    set(PROJECT_GIT_DESCRIBE "no-git")
    set(PROJECT_GIT_COMMIT   "no-git")
    set(PROJECT_GIT_DIRTY    "0")
endif()

# ---------------------------------------------------------------------------
# Build timestamp & version header
# ---------------------------------------------------------------------------
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

set(GENERATED_INCLUDE_DIR ${CMAKE_BINARY_DIR}/generated_include)
file(MAKE_DIRECTORY ${GENERATED_INCLUDE_DIR})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
    ${GENERATED_INCLUDE_DIR}/version.h
    @ONLY
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(synth ${SYNTH_SOURCES})
target_include_directories(synth PRIVATE
    ${PROJECT_INCLUDE_DIR}
    ${GENERATED_INCLUDE_DIR}
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)

# ---------------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------------
message(STATUS "[PortMIDI]")
add_subdirectory(${CMAKE_SOURCE_DIR}/external/portmidi external/portmidi_build)
target_include_directories(synth PRIVATE ${CMAKE_SOURCE_DIR}/external/portmidi)
target_link_libraries(synth PRIVATE portmidi)
message(STATUS "[----------------]\n")

message(STATUS "[SDL3]")
add_subdirectory(${CMAKE_SOURCE_DIR}/external/sdl external/sdl_build)
target_include_directories(synth PRIVATE ${CMAKE_SOURCE_DIR}/external/sdl)
target_link_libraries(synth PRIVATE SDL3::SDL3)
message(STATUS "[----------------]\n")

message(STATUS "[cJSON]")
set(ENABLE_CJSON_TEST            OFF)
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF)
set(ENABLE_CJSON_UTILS           OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/cjson external/cjson_build)
target_link_libraries(synth PRIVATE cjson)
message(STATUS "[----------------]\n")

# ---------------------------------------------------------------------------
# Base compiler flags — Debug vs Release
#
#   Debug   : no optimisation, full debug symbols, runtime checks (MSVC)
#   Release : O2, debug symbols kept (/Zi|-g) for profiling, NDEBUG defined
# ---------------------------------------------------------------------------
target_compile_options(synth PRIVATE
    # --- MSVC ---
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:
        /W4 /Od /RTC1 /Zi>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:
        /W4 /O2 /Zi /DNDEBUG>

    # --- GCC / Clang ---
    $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:
        -Wall -Wextra -Wpedantic -O0 -g>
    $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>,$<CONFIG:Release>>:
        -Wall -Wextra -Wpedantic -O2 -g -DNDEBUG>
)

# ---------------------------------------------------------------------------
# Optimisation profiles — applied on top of the base flags above
# ---------------------------------------------------------------------------

# --- none ---
if(OPT_PROFILE STREQUAL "none")
    message(STATUS "[OPT_PROFILE] No extra optimisation flags.")

# --- sse2 ---
elseif(OPT_PROFILE STREQUAL "sse2")
    message(STATUS "[OPT_PROFILE] SSE2: -msse2 / /arch:SSE2, -fno-math-errno / /fp:except-")
    target_compile_options(synth PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:
            -msse2
            -fno-math-errno>
        $<$<C_COMPILER_ID:MSVC>:
            /arch:SSE2
            /fp:except->
    )

# --- avx2 ---
elseif(OPT_PROFILE STREQUAL "avx2")
    message(STATUS "[OPT_PROFILE] AVX2: -mavx2 / /arch:AVX2, finite-math, no-math-errno")
    target_compile_options(synth PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:
            -mavx2
            -fno-math-errno
            -ffinite-math-only>
        $<$<C_COMPILER_ID:MSVC>:
            /arch:AVX2
            /fp:except->
    )

# --- fast ---
elseif(OPT_PROFILE STREQUAL "fast")
    message(STATUS "[OPT_PROFILE] fast: O3 + march=native + ffast-math + vectorise")
    target_compile_options(synth PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:
            -O3
            -march=native
            -mtune=native
            -ffast-math
            -fno-math-errno
            -ftree-vectorize
            -ftree-loop-vectorize>
        $<$<C_COMPILER_ID:MSVC>:
            /O3
            /fp:fast
            /fp:except-
            /arch:AVX2
            /Qvec>
    )

# --- overkill ---
elseif(OPT_PROFILE STREQUAL "overkill")
    message(STATUS "[OPT_PROFILE] overkill: everything enabled; binary is build-machine specific")

    if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        message(STATUS "[OPT_PROFILE] NOTE: MSVC has no /arch:native equivalent; using /arch:AVX2 instead.")
    endif()

    target_compile_options(synth PRIVATE
        # GCC / Clang --------------------------------------------------------
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:
            -O3
            -march=native
            -mtune=native
            -ffast-math           # implies: -fno-math-errno -ffinite-math-only
                                  #          -fassociative-math -freciprocal-math
                                  #          -fno-signed-zeros  -fno-trapping-math
            -fno-math-errno       # explicit (redundant under -ffast-math, clear intent)
            -ffinite-math-only
            -fassociative-math
            -freciprocal-math
            -fno-signed-zeros
            -fno-trapping-math
            -ftree-vectorize
            -ftree-loop-vectorize
            -funroll-loops
            -fprefetch-loop-arrays
            -fomit-frame-pointer>

        # MSVC ---------------------------------------------------------------
        $<$<C_COMPILER_ID:MSVC>:
            /O3
            /Ox               # max speed (subsumes /O2, /Ob2, /Oi, /Ot, /Oy)
            /fp:fast          # implies finite + associative + reciprocal approx
            /fp:except-       # disable FP exception semantics
            /arch:AVX2        # best available fixed target (no /arch:native in MSVC)
            /Qvec             # auto-vectoriser
            /Oi               # intrinsic functions
            /GL>              # whole-program optimisation (pair with /LTCG at link)
    )

    # Whole-program optimisation link flag (MSVC only)
    target_link_options(synth PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/LTCG>
    )

else()
    message(WARNING "[OPT_PROFILE] Unknown profile '${OPT_PROFILE}' — ignored. "
                    "Valid: none | sse2 | avx2 | fast | overkill")
endif()

# ---------------------------------------------------------------------------
# Platform-dependent vectorisable math library
#
#   Linux  + GCC   → libmvec (glibc SIMD math); requires -ftree-loop-vectorize
#                    and -fno-math-errno so glibc can emit SIMD calls.
#   Linux  + Clang → libm only; Clang's libmvec integration is incomplete
#                    in most distros; linking libmvec would likely be a no-op.
#   Windows + MSVC → SVML ships inside the MSVC toolchain and is activated
#                    implicitly by /Qvec + /fp:fast; no separate lib needed.
#   Windows + non-MSVC → try SLEEF; else Intel SVML/MKL; else AMD AOCL
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(STATUS "[MathLib] Linux + GCC: linking -lmvec -lm (glibc SIMD math)")
        target_link_libraries(synth PRIVATE m mvec)

        # libmvec call-site conditions: loop vectoriser + no errno side-effects
        # These are safe to add unconditionally; -ffast-math/-fno-math-errno in
        # an OPT_PROFILE will simply duplicate/override them (harmless).
        target_compile_options(synth PRIVATE
            $<$<C_COMPILER_ID:GNU>:
                -ftree-loop-vectorize
                -fno-math-errno>
        )
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        message(STATUS "[MathLib] Linux + Clang: linking -lm only "
                       "(libmvec/Clang integration is distro-dependent; skipping)")
        target_link_libraries(synth PRIVATE m)
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        message(STATUS "[MathLib] Windows + MSVC: SVML activated via /Qvec + /fp:fast (implicit)")
        target_compile_definitions(synth PRIVATE USE_SVML)
    else()
        message(STATUS "[MathLib] Windows + non-MSVC: try SLEEF, then Intel SVML/MKL, then AMD AOCL")

        # 1) Try SLEEF
        set(SLEEF_DIR "" CACHE PATH "Path to SLEEF install (Windows)")
        find_package(sleef QUIET)

        if(NOT sleef_FOUND)
            if(SLEEF_DIR)
                set(CMAKE_PREFIX_PATH "${SLEEF_DIR};${CMAKE_PREFIX_PATH}")
                find_package(sleef QUIET)
            elseif(DEFINED ENV{SLEEF_ROOT})
                set(CMAKE_PREFIX_PATH "$ENV{SLEEF_ROOT};${CMAKE_PREFIX_PATH}")
                find_package(sleef QUIET)
            endif()
        endif()

        if(sleef_FOUND)
            message(STATUS "Using SLEEF (Windows)")
            if(TARGET SLEEF::sleef)
                target_link_libraries(synth PRIVATE SLEEF::sleef)
            else()
                if(TARGET sleef)
                    target_link_libraries(synth PRIVATE sleef)
                else()
                    if(DEFINED sleef_LIBRARIES)
                        target_link_libraries(synth PRIVATE ${sleef_LIBRARIES})
                    endif()
                    if(DEFINED sleef_INCLUDE_DIRS)
                        target_include_directories(synth PRIVATE ${sleef_INCLUDE_DIRS})
                    endif()
                endif()
            endif()
            target_compile_definitions(synth PRIVATE USE_SLEEF)
        else()
            message(STATUS "SLEEF not found; probing for Intel compiler or MKL")

            # 2) Detect Intel compilers (IFX/ICC/ICX)
            if(CMAKE_C_COMPILER_ID MATCHES "Intel" OR CMAKE_C_COMPILER_ID MATCHES "IntelLLVM")
                message(STATUS "Intel compiler detected (${CMAKE_C_COMPILER_ID}); enabling SVML usage flags")
                target_compile_options(synth PRIVATE
                    $<$<OR:$<C_COMPILER_ID:Intel>,$<C_COMPILER_ID:IntelLLVM>>:
                        -qsvml>
                )
                target_compile_definitions(synth PRIVATE USE_SVML)
            else()
                # 3) Try Intel MKL via find_package or MKLROOT
                find_package(MKL QUIET)
                if(MKL_FOUND)
                    message(STATUS "Found Intel MKL; linking MKL")
                    if(TARGET MKL::MKL)
                        target_link_libraries(synth PRIVATE MKL::MKL)
                    else()
                        target_link_libraries(synth PRIVATE ${MKL_LIBRARIES})
                        target_include_directories(synth PRIVATE ${MKL_INCLUDE_DIRS})
                    endif()
                    target_compile_definitions(synth PRIVATE USE_MKL)
                else()
                    # 4) Check for AMD AOCL (AOCL-CPU / aocl-libm) if AMD CPU/toolchain
                    #    Detect AMD via CMAKE_HOST_SYSTEM_PROCESSOR or /proc/cpuinfo (best-effort).
                    message(STATUS "MKL not found; probing for AMD AOCL")

                    set(IS_AMD_CPU FALSE)
                    if(DEFINED CMAKE_HOST_SYSTEM_PROCESSOR)
                        string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _proc_lower)
                        if(_proc_lower MATCHES "amd" OR _proc_lower MATCHES "x86_64" OR _proc_lower MATCHES "amd64")
                            # x86_64 doesn't guarantee AMD, but we'll also check /proc/cpuinfo on systems that have it
                            set(IS_AMD_CPU TRUE)
                        endif()
                    endif()

                    if(EXISTS "/proc/cpuinfo")
                        file(READ "/proc/cpuinfo" _cpuinfo)
                        string(TOLOWER "${_cpuinfo}" _cpuinfo_lower)
                        if(_cpuinfo_lower MATCHES "authenticamd" OR _cpuinfo_lower MATCHES "amd")
                            set(IS_AMD_CPU TRUE)
                        endif()
                    endif()

                    if(IS_AMD_CPU)
                        message(STATUS "AMD CPU detected (best-effort); trying to find AOCL / AMD libm / BLAS")
                        # Prefer CMake configs if present
                        find_package(AOCL QUIET) # hypothetical; many AOCL installs may not provide CMake package
                        if(AOCL_FOUND)
                            message(STATUS "Found AOCL via find_package(AOCL); linking")
                            if(TARGET AOCL::AOCL)
                                target_link_libraries(synth PRIVATE AOCL::AOCL)
                            else()
                                target_link_libraries(synth PRIVATE ${AOCL_LIBRARIES})
                                target_include_directories(synth PRIVATE ${AOCL_INCLUDE_DIRS})
                            endif()
                            target_compile_definitions(synth PRIVATE USE_AOCL)
                        else()
                            # Fallback: check AMD environment var AOCLROOT or common install paths
                            if(DEFINED ENV{AOCLROOT})
                                set(_AOCL_ROOT $ENV{AOCLROOT})
                            elseif(DEFINED ENV{AMD_AOCL_ROOT})
                                set(_AOCL_ROOT $ENV{AMD_AOCL_ROOT})
                            else()
                                set(_AOCL_ROOT "")
                            endif()

                            if(_AOCL_ROOT)
                                find_library(AOCL_LIBM aocl_libm HINTS "${_AOCL_ROOT}/lib" "${_AOCL_ROOT}/lib/x86_64")
                                find_library(AOCL_BLIS blis HINTS "${_AOCL_ROOT}/lib" "${_AOCL_ROOT}/lib/x86_64")
                                if(AOCL_LIBM)
                                    message(STATUS "Found AMD libm: ${AOCL_LIBM}; linking it")
                                    target_link_libraries(synth PRIVATE ${AOCL_LIBM})
                                    target_compile_definitions(synth PRIVATE USE_AOCL)
                                else()
                                    message(WARNING "AMD AOCL libm not found under AOCLROOT; skipping AOCL")
                                endif()
                            else()
                                message(WARNING "No AOCL/MKL/SLEEF found and AMD toolchain/root not set; continuing without vectorised math")
                            endif()
                        endif()
                    else()
                        message(WARNING "No SLEEF, Intel MKL, or AMD AOCL detected; continuing without vectorised math")
                    endif() # IS_AMD_CPU
                endif() # MKL_FOUND
            endif() # Intel compiler check
        endif() # SLEEF_FOUND
    endif() # MSVC check
endif() # Windows vs Linux

# ---------------------------------------------------------------------------
# GLSL → SPIR-V shader compilation
# ---------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE glslc
    HINTS D:/c_cpp_dependencies/shaderc/build/glslc/Release/
)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found (required to compile GLSL -> SPIR-V).")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    ${SHADER_SRC_DIR}/*.vert
    ${SHADER_SRC_DIR}/*.frag
)

set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

set(SHADER_OUTS)
foreach(SRC IN LISTS SHADER_SOURCES)
    get_filename_component(NAME ${SRC} NAME)
    set(OUT ${SHADER_OUT_DIR}/${NAME}.spv)
    add_custom_command(
        OUTPUT  ${OUT}
        COMMAND ${GLSLC_EXECUTABLE} ${SRC} -o ${OUT}
        DEPENDS ${SRC}
        COMMENT "Compiling shader ${NAME} -> ${OUT}"
        VERBATIM
    )
    list(APPEND SHADER_OUTS ${OUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTS})
add_dependencies(synth shaders)

# ---------------------------------------------------------------------------
# Install & tooling
# ---------------------------------------------------------------------------
install(TARGETS synth RUNTIME DESTINATION bin)

add_custom_target(format
    COMMAND clang-format -i
        ${SYNTH_SOURCES}
        ${PROJECT_INCLUDE_DIR}/*.h
        ${GENERATED_INCLUDE_DIR}/*.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Run clang-format on sources and headers"
)

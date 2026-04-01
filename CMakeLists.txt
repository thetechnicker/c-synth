cmake_minimum_required(VERSION 3.10)
# Export compile_commands.json (useful for clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(synth C)

# Options
option(ENABLE_SSE             "Enable SSE2 optimizations"                    OFF)
option(ENABLE_FAST_MATH       "Enable -ffast-math / /fp:fast"                OFF)
option(ENABLE_NO_MATH_ERRNO   "Enable -fno-math-errno / /fp:except-"         OFF)
option(ENABLE_FINITE_MATH     "Enable -ffinite-math-only"                    OFF)
option(ENABLE_ASSOCIATIVE_MATH "Enable -fassociative-math"                  OFF)
option(ENABLE_O3              "Use -O3 / /O3 in Release (default is -O2 / /O2)" OFF)

# Standard & build type
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()


# Source layout
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB_RECURSE SYNTH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
if(NOT SYNTH_SOURCES)
  message(FATAL_ERROR "No sources found in src/*.c")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

set(PROJECT_NAME "${PROJECT_NAME}")

# --- Git metadata via `git describe` ---
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
  set(PROJECT_GIT_COMMIT "no-git")
  set(PROJECT_GIT_DIRTY "0")
endif()

# Build timestamp (ISO 8601)
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

# Configure a version header into the build tree
set(GENERATED_INCLUDE_DIR ${CMAKE_BINARY_DIR}/generated_include)
file(MAKE_DIRECTORY ${GENERATED_INCLUDE_DIR})
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
  ${GENERATED_INCLUDE_DIR}/version.h
  @ONLY
)

include_directories(${GENERATED_INCLUDE_DIR})

# --- Create target ---
add_executable(synth ${SYNTH_SOURCES})
target_include_directories(synth PRIVATE ${PROJECT_INCLUDE_DIR} ${GENERATED_INCLUDE_DIR})

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/external/portmidi external/portmidi_build)
set(PORTMIDI_TARGET portmidi)
target_include_directories(synth PRIVATE ${CMAKE_SOURCE_DIR}/external/portmidi)
target_link_libraries(synth PRIVATE ${PORTMIDI_TARGET})

add_subdirectory(${CMAKE_SOURCE_DIR}/external/sdl external/sdl_build)
set(SDL_TARGET SDL3::SDL3)
target_include_directories(synth PRIVATE ${CMAKE_SOURCE_DIR}/external/sdl)
target_link_libraries(synth PRIVATE ${SDL_TARGET})

set(ENABLE_CJSON_TEST            OFF)
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF)
set(ENABLE_CJSON_UTILS           OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/cjson external/cjson_build)
target_link_libraries(synth PRIVATE cjson)


# ---------------------------------------------------------------------------
# Compiler flags per-config and per-compiler
# ---------------------------------------------------------------------------
target_compile_options(synth PRIVATE
  # MSVC
  $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/W4 /Od /RTC1 /Zi>
  $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/W4 /DNDEBUG /Zi>
  # GNU/Clang
  $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-Wall -Wextra -Wpedantic -O0 -g>
  $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-Wall -Wextra -Wpedantic>
)

# ---------------------------------------------------------------------------
# Release optimisation level: -O2 (default) or -O3 (opt-in via ENABLE_O3)
# ---------------------------------------------------------------------------
if(ENABLE_O3)
  message(STATUS "[OptLevel] Release: -O3 / /O3 (ENABLE_O3=ON)")
  target_compile_options(synth PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O3>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O3>
  )
else()
  message(STATUS "[OptLevel] Release: -O2 / /O2 (default; set ENABLE_O3=ON for -O3)")
  target_compile_options(synth PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-O2>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
  )
endif()

# ---------------------------------------------------------------------------
# Math optimisation flags
#
#  Debug:   each flag is individually opt-in via CMake options above.
#  Release: all flags + SSE2 are forced on unconditionally.
#
#  GCC/Clang breakdown:
#    -ffast-math          implies -fno-math-errno, -ffinite-math-only,
#                         -fassociative-math and more — use alone OR combine
#                         the individual flags; mixing is harmless but noisy.
#    -fno-math-errno      skip setting errno after math calls (safe for audio)
#    -ffinite-math-only   assume no NaN/Inf inputs/outputs
#    -fassociative-math   allow reordering of floating-point ops
#
#  MSVC equivalents:
#    /fp:fast             ≈ -ffast-math (covers finite + associative)
#    /fp:except-          ≈ -fno-math-errno (disable FP exception semantics)
#    Note: MSVC has no direct /ffinite-math-only or /fassociative-math toggle;
#          /fp:fast already implies both.
# ---------------------------------------------------------------------------

# --- Debug: individually toggleable ---
if(ENABLE_FAST_MATH)
  message(STATUS "[MathOpt] -ffast-math ON (Debug)")
  target_compile_options(synth PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-ffast-math>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/fp:fast /fp:except->
  )
endif()

if(ENABLE_NO_MATH_ERRNO)
  message(STATUS "[MathOpt] -fno-math-errno ON (Debug)")
  target_compile_options(synth PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-fno-math-errno>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/fp:except->
  )
endif()

if(ENABLE_FINITE_MATH)
  message(STATUS "[MathOpt] -ffinite-math-only ON (Debug)")
  target_compile_options(synth PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-ffinite-math-only>
    # MSVC: /fp:fast already covers this; no separate flag needed
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/fp:fast>
  )
endif()

if(ENABLE_ASSOCIATIVE_MATH)
  message(STATUS "[MathOpt] -fassociative-math ON (Debug)")
  target_compile_options(synth PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-fassociative-math>
    # MSVC: /fp:fast already covers this; no separate flag needed
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/fp:fast>
  )
endif()

# --- Release: force math flags + SSE2 unconditionally ---
target_compile_options(synth PRIVATE
  $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:
    -ffast-math
    -fno-math-errno
    -ffinite-math-only
    -fassociative-math
    -msse2
  >
  $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:
    /fp:fast
    /fp:except-
    /arch:SSE2
  >
)
target_compile_definitions(synth PRIVATE
  $<$<CONFIG:Release>:ENABLE_SSE=1>
)

# --- SSE2: manual opt-in for Debug (or any non-Release config) ---
if(ENABLE_SSE)
  message(STATUS "[MathOpt] SSE2 ON (manual)")
  target_compile_definitions(synth PRIVATE ENABLE_SSE=1)
  target_compile_options(synth PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-msse2>
    $<$<C_COMPILER_ID:MSVC>:/arch:SSE2>
  )
endif()

# ---------------------------------------------------------------------------

# Link math lib on Unix-like systems
if(UNIX)
  target_link_libraries(synth PRIVATE m)
endif()

# Find glslc
find_program(GLSLC_EXECUTABLE glslc D:/c_cpp_dependencies/shaderc/build/glslc/Release/)
if(NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found (required to compile GLSL -> SPIR-V).")
endif()

# Shader sources
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
    OUTPUT ${OUT}
    COMMAND ${GLSLC_EXECUTABLE} ${SRC} -o ${OUT}
    DEPENDS ${SRC}
    COMMENT "Compiling shader ${NAME} -> ${OUT}"
    VERBATIM
  )

  list(APPEND SHADER_OUTS ${OUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTS})
add_dependencies(synth shaders)

install(TARGETS synth RUNTIME DESTINATION bin)

add_custom_target(format
  COMMAND clang-format -i ${SYNTH_SOURCES} ${PROJECT_INCLUDE_DIR}/*.h ${GENERATED_INCLUDE_DIR}/*.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Run clang-format on sources and headers"
)

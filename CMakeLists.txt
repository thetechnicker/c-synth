cmake_minimum_required(VERSION 3.10)
# Export compile_commands.json (useful for clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(synth C)

# Options
option(ENABLE_SSE "Enable SSE optimizations" OFF)

# Standard & build type
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()


# Source layout
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB_RECURSE SYNTH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
if(NOT SYNTH_SOURCES)
  message(FATAL_ERROR "No sources found in src/*.c")
endif()

#if(MSVC)
#  # Use static CRT: "MultiThreaded" = /MT, "MultiThreadedDebug" = /MTd
#  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
#endif()

# --- Git metadata via `git describe` ---
find_package(Git QUIET)

if(GIT_FOUND)
  # Prefer annotated tag description; fall back to commit hash if no tags
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

# Ensure generated headers are visible
include_directories(${GENERATED_INCLUDE_DIR})
# Optionally install the generated header
install(DIRECTORY ${GENERATED_INCLUDE_DIR}/ DESTINATION include/synth_generated FILES_MATCHING PATTERN "version.h")

# --- Create target ---
add_executable(synth ${SYNTH_SOURCES})
target_include_directories(synth PRIVATE ${PROJECT_INCLUDE_DIR} ${GENERATED_INCLUDE_DIR})

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/portmidi external/portmidi_build)
# Common PortMidi target names used by community ports: portmidi or libportmidi
# Adjust if the submodule defines a different target.
set(PORTMIDI_TARGET portmidi)
# Expose include dir if needed (some trees install headers next to target)
target_include_directories(synth PRIVATE ${CMAKE_SOURCE_DIR}/external/portmidi)
target_link_libraries(synth PRIVATE ${PORTMIDI_TARGET})
#target_compile_definitions(synth PRIVATE USE_PORTMIDI=1)


# Compiler flags per-config and per-compiler
target_compile_options(synth PRIVATE
  # MSVC
  $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/W4 /Od /RTC1 /Zi>
  $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/W4 /O2 /DNDEBUG /Zi>
  # GNU/Clang
  $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Debug>>:-Wall -Wextra -Wpedantic -O0 -g>
  $<$<AND:$<C_COMPILER_ID:GNU,Clang>,$<CONFIG:Release>>:-Wall -Wextra -Wpedantic -O2>
)

# SSE optional
if(ENABLE_SSE)
  target_compile_definitions(synth PRIVATE ENABLE_SSE=1)
  target_compile_options(synth PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-msse2>
    $<$<C_COMPILER_ID:MSVC>:/arch:SSE2>
  )
endif()

# Link math lib on Unix-like systems
if(UNIX)
  target_link_libraries(synth PRIVATE m)
endif()

# Install binary
install(TARGETS synth RUNTIME DESTINATION bin)

# Convenience target: clang-format (requires clang-format in PATH)
add_custom_target(format
  COMMAND clang-format -i ${SYNTH_SOURCES} ${PROJECT_INCLUDE_DIR}/*.h ${GENERATED_INCLUDE_DIR}/*.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Run clang-format on sources and headers"
)


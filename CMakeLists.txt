cmake_minimum_required(VERSION 3.10)
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

# Target
add_executable(synth ${SYNTH_SOURCES})
target_include_directories(synth PRIVATE ${PROJECT_INCLUDE_DIR})

# Compiler flags
target_compile_options(synth PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -O2>
  $<$<C_COMPILER_ID:MSVC>:/W4 /O2>
)

# SSE optional
if(ENABLE_SSE)
  target_compile_options(synth PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-msse2>)
  target_compile_definitions(synth PRIVATE ENABLE_SSE=1)
endif()

# Link math lib on Unix
if(UNIX)
  target_link_libraries(synth PRIVATE m)
endif()

# Export compile_commands.json (useful for clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Install
install(TARGETS synth RUNTIME DESTINATION bin)
install(DIRECTORY ${PROJECT_INCLUDE_DIR}/ DESTINATION include)

# Convenience target: clang-format (requires clang-format in PATH)
add_custom_target(format
  COMMAND clang-format -i ${SYNTH_SOURCES} ${PROJECT_INCLUDE_DIR}/*.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Run clang-format on sources and headers"
)

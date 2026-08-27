cmake_minimum_required(VERSION 3.20)

# Johns SDK ELF (fps_elf-shaped), not utility_daemon .plugin.
# Compile with prospero-cmake on Linux CI — not MSVC on the Windows dev PC.
if(NOT PS5SDK)
  if(DEFINED ENV{PS5SDK} AND NOT "$ENV{PS5SDK}" STREQUAL "")
    set(PS5SDK "$ENV{PS5SDK}")
  endif()
endif()
if(NOT PS5_PAYLOAD_SDK)
  if(DEFINED ENV{PS5_PAYLOAD_SDK} AND NOT "$ENV{PS5_PAYLOAD_SDK}" STREQUAL "")
    set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}")
  endif()
endif()

if(NOT CMAKE_TOOLCHAIN_FILE)
  if(PS5_PAYLOAD_SDK AND EXISTS "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake"
        CACHE FILEPATH "PS5 payload SDK toolchain")
  elseif(PS5_PAYLOAD_SDK AND EXISTS "${PS5_PAYLOAD_SDK}/cmake/toolchain-prospero.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${PS5_PAYLOAD_SDK}/cmake/toolchain-prospero.cmake"
        CACHE FILEPATH "PS5 payload SDK toolchain")
  elseif(PS5SDK AND EXISTS "${PS5SDK}/cmake/toolchain-ps5.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${PS5SDK}/cmake/toolchain-ps5.cmake"
        CACHE FILEPATH "PS5 SDK toolchain")
  endif()
endif()

if(NOT PS5SDK AND NOT PS5_PAYLOAD_SDK)
  message(FATAL_ERROR
    "PS5SDK or PS5_PAYLOAD_SDK must be set. "
    "Build with Johns SDK: bash overlay/build.sh — "
    "do not configure with a host compiler on Windows.")
endif()

project(overlay C)

if(NOT CMAKE_C_COMPILER_ID MATCHES "[Cc]lang")
  message(FATAL_ERROR
    "overlay is meant to be built with clang (got ${CMAKE_C_COMPILER_ID}). "
    "Use ${PS5_PAYLOAD_SDK}/bin/prospero-cmake.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(overlay
  source/start.c
  source/main.c
  source/util.c
  source/detour.c
  source/resolve.c
  source/hooks.c
  source/draw.c
  source/font.c
  source/hud.c
  source/http_client.c
  source/worker.c
  source/notify.c
  third_party/cjson/cJSON.c
)

target_include_directories(overlay PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson"
)

if(PS5_PAYLOAD_SDK)
  target_include_directories(overlay SYSTEM PRIVATE "${PS5_PAYLOAD_SDK}/include")
endif()

target_compile_definitions(overlay PRIVATE
  CJSON_HIDE_SYMBOLS
  NITEPR5_OVERLAY=1
)

# fps_elf links kernel + SceGnmDriver. Pad + sockets for DualSense and :1745.
# Hijacker is not a link dependency (no game RAM R/W). kernel_* helpers are
# only for mprotect / localhost sockets after inject.
target_link_libraries(overlay PRIVATE
  kernel
  SceGnmDriver
  ScePad
  SceNet
  SceSysmodule
  SceUserService
  SceLibcInternal
  kernel_sys
)

# PROC_ELF jumps to e_entry with RDI = payload_args. Default Johns CRT
# (__crt_start) often never reaches main() inside a live eboot. overlay_start
# raises ucred, toasts, then calls CRT. Last -e wins over the SDK default.
target_link_options(overlay PRIVATE -Wl,-e,overlay_start -Wl,-u,__crt_start)

set_target_properties(overlay PROPERTIES
  OUTPUT_NAME "overlay"
  PREFIX ""
  SUFFIX ".elf"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

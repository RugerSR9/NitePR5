cmake_minimum_required(VERSION 3.20)

# Refuse host toolchains. Compile with Johns SDK (prospero-cmake) on Linux CI —
# not MSVC on the Windows dev PC. See plugin/build.sh and .github/workflows/plugin.yml.
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
  # Johns SDK install: toolchain/prospero.cmake (used by bin/prospero-cmake).
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
    "Build with Johns SDK: bash plugin/build.sh (GitHub Actions) — "
    "do not configure with a host compiler on Windows.")
endif()

project(nitepr5 C)

if(NOT CMAKE_C_COMPILER_ID MATCHES "[Cc]lang")
  message(FATAL_ERROR
    "nitepr5 is meant to be built with clang (got ${CMAKE_C_COMPILER_ID}). "
    "Use ${PS5_PAYLOAD_SDK}/bin/prospero-cmake.")
endif()

set(PLUGIN_TITLE_ID "NTPR50001")

# PLUGIN_VERSION should always be in #.## format, ex. 0.501 should be set as 0.50
# Stay on 0.57 until HUD works — do not bump wrap.
set(PLUGIN_VERSION "0.57")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(nitepr5
  source/main.c
  source/http.c
  source/dbg_client.c
  source/freeze.c
  source/cheats.c
  source/notify.c
  source/fs_state.c
  source/inject.c
  source/got_patch.c
  third_party/cjson/cJSON.c
  third_party/elfldr/pt.c
  third_party/elfldr/elfldr.c
  third_party/hde64/hde64.c
)

target_include_directories(nitepr5 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/elfldr"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/hde64"
)

if(PS5_PAYLOAD_SDK)
  target_include_directories(nitepr5 SYSTEM PRIVATE "${PS5_PAYLOAD_SDK}/include")
endif()

target_compile_definitions(nitepr5 PRIVATE
  PLUGIN_TITLE_ID="${PLUGIN_TITLE_ID}"
  PLUGIN_VERSION="${PLUGIN_VERSION}"
  CJSON_HIDE_SYMBOLS
  NITEPR5_ELFLDR_INJECT_ONLY
)

# utility_daemon link set. No hijacker, no ScePad.
# kernel + kernel_sys: Johns elfldr (kernel_mprotect / overlap_sockets) for B3 inject.
target_link_libraries(nitepr5 PRIVATE
  SceLibcInternal
  SceSystemService
  SceNet
  SceSysmodule
  SceUserService
  SceNetCtl
  kernel
  kernel_sys
)

set_target_properties(nitepr5 PROPERTIES
  OUTPUT_NAME "nitepr5"
  PREFIX ""
  SUFFIX ".elf"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Wrap ELF as etaHEN .plugin: etaHEN_PLUGIN\0TID\0version\0 + ELF bytes.
add_custom_command(TARGET nitepr5 POST_BUILD
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_plugin.py"
          "$<TARGET_FILE:nitepr5>" "${PLUGIN_TITLE_ID}" "${PLUGIN_VERSION}"
  COMMENT "Wrapping ELF as etaHEN .plugin (NTPR50001 0.57)"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  VERBATIM
)

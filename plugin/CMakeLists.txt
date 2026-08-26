cmake_minimum_required(VERSION 3.20)

# Refuse host toolchains. This tree is built later with etaHEN-Plugins /
# PS5 payload SDK clang — not MSVC on the Windows dev PC.
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

if(NOT PS5SDK AND NOT PS5_PAYLOAD_SDK)
  message(FATAL_ERROR
    "PS5SDK or PS5_PAYLOAD_SDK must be set. "
    "Build NitePR5 with etaHEN-Plugins / PS5 payload SDK clang — "
    "do not configure with a host compiler on Windows.")
endif()

if(NOT CMAKE_TOOLCHAIN_FILE)
  if(PS5_PAYLOAD_SDK AND EXISTS "${PS5_PAYLOAD_SDK}/cmake/toolchain-prospero.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${PS5_PAYLOAD_SDK}/cmake/toolchain-prospero.cmake"
        CACHE FILEPATH "PS5 payload SDK toolchain")
  elseif(PS5_PAYLOAD_SDK AND EXISTS "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake"
        CACHE FILEPATH "PS5 payload SDK toolchain")
  elseif(PS5SDK AND EXISTS "${PS5SDK}/cmake/toolchain-prospero.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${PS5SDK}/cmake/toolchain-prospero.cmake"
        CACHE FILEPATH "PS5 SDK toolchain")
  endif()
endif()

project(nitepr5 C)

set(PLUGIN_TITLE_ID "NPR500001")
set(PLUGIN_VERSION "0.40")

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
  third_party/cjson/cJSON.c
)

target_include_directories(nitepr5 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson"
)

target_compile_definitions(nitepr5 PRIVATE
  PLUGIN_TITLE_ID="${PLUGIN_TITLE_ID}"
  PLUGIN_VERSION="${PLUGIN_VERSION}"
  CJSON_HIDE_SYMBOLS
)

target_link_libraries(nitepr5 PRIVATE
  SceLibcInternal
  SceSystemService
  SceNet
  SceSysmodule
  SceUserService
  SceNetCtl
  kernel_sys
)

set_target_properties(nitepr5 PROPERTIES OUTPUT_NAME "nitepr5")

add_custom_command(TARGET nitepr5 POST_BUILD
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_plugin.py"
          "$<TARGET_FILE:nitepr5>" "${PLUGIN_TITLE_ID}" "${PLUGIN_VERSION}"
  COMMENT "Wrapping ELF as etaHEN .plugin (NPR500001 0.40)"
  VERBATIM
)

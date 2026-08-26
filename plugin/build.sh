#!/usr/bin/env bash
# Cross-compile plugin/ with Johns SDK (prospero-cmake). Run from CI or Linux:
#   export PS5_PAYLOAD_SDK=/path/to/sdk
#   bash plugin/build.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${HERE}/build"
OUT="${BUILD}/nitepr5.plugin"

if [[ ! -f "${HERE}/CMakeLists.txt" ]]; then
  echo "error: ${HERE}/CMakeLists.txt is missing" >&2
  exit 1
fi

if [[ -z "${PS5_PAYLOAD_SDK:-}" ]]; then
  echo "error: PS5_PAYLOAD_SDK is not set" >&2
  echo "Download ps5-payload-sdk.zip from ps5-payload-dev/sdk and unzip it." >&2
  exit 1
fi

CMAKE="${PS5_PAYLOAD_SDK}/bin/prospero-cmake"
if [[ ! -f "${CMAKE}" ]]; then
  echo "error: ${CMAKE} not found (is PS5_PAYLOAD_SDK the SDK root?)" >&2
  exit 1
fi
chmod +x "${CMAKE}" 2>/dev/null || true

echo "PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK}"
"${CMAKE}" -S "${HERE}" -B "${BUILD}"
cmake --build "${BUILD}"

if [[ ! -f "${OUT}" ]]; then
  echo "error: expected ${OUT} after build" >&2
  find "${BUILD}" \( -name '*.plugin' -o -name 'nitepr5*' \) -print >&2 || true
  exit 1
fi

echo "built ${OUT}"

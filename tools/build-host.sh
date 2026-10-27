#!/usr/bin/env bash
#
# build-host.sh - build sensornode-core cho host x86.
#
# Cách dùng:
#   tools/build-host.sh
#
# Tuỳ biến qua biến môi trường:
#   CLEAN       =1 thì xoá thư mục build trước khi cấu hình lại
#
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

case "${1:-}" in
    -h|--help|help) usage; exit 0 ;;
esac

# Terminal đã source SDK thì CC/CXX/CXXFLAGS đang trỏ tới toolchain ARM.
[ -z "${OECORE_TARGET_SYSROOT:-}" ] \
    || die "Yocto SDK environment is active in this shell, open a new one."

if [ "${CLEAN:-0}" = "1" ] && [ -d "$HOST_BUILD_DIR" ]; then
    log "CLEAN=1 -> removing $HOST_BUILD_DIR"
    rm -rf "$HOST_BUILD_DIR"
fi

# build/ từng được cấu hình với toolchain file của SDK
if grep -q '^CMAKE_TOOLCHAIN_FILE' "${HOST_BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
    log "build/ was configured with a cross toolchain, removing it"
    rm -rf "$HOST_BUILD_DIR"
fi

cmake -S "$REPO_ROOT" -B "$HOST_BUILD_DIR"
cmake --build "$HOST_BUILD_DIR" -j"$(nproc)"
ok "Build done: ${HOST_BUILD_DIR}"

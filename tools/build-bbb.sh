#!/usr/bin/env bash
#
# build-bbb.sh - cross-build sensornode-core cho BBB bằng SDK Yocto.
#
# Cách dùng:
#   tools/build-bbb.sh
#
# Tuỳ biến qua biến môi trường:
#   SDK_ENV     script environment-setup-* của SDK (mặc định tự tìm trong /opt)
#   CLEAN       =1 thì xoá thư mục build trước khi cấu hình lại
#
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

TARGET_TRIPLE="arm-poky-linux-gnueabi"   # dùng để xác thực toolchain

case "${1:-}" in
    -h|--help|help) usage; exit 0 ;;
esac

# -----------------------------------------------------------------------------
# 1. Môi trường cross-compiler
# -----------------------------------------------------------------------------
ensure_cross_env() {
    if [ -z "${OECORE_TARGET_SYSROOT:-}" ]; then
        local env_script="${SDK_ENV:-}"
        if [ -z "$env_script" ]; then
            env_script="$(ls -d /opt/*/*/environment-setup-cortexa8hf-neon-poky-linux-gnueabi 2>/dev/null \
                          | sort -V | tail -n1 || true)"
        fi
        [ -n "$env_script" ] && [ -f "$env_script" ] \
            || die "SDK not found."

        log "Sourcing $env_script"
        set +u
        source "$env_script"
        set -u
    fi

    [ -d "$OECORE_TARGET_SYSROOT" ] || die "Sysroot does not exist: $OECORE_TARGET_SYSROOT"
    case "${CXX:-}" in
        *${TARGET_TRIPLE}*) : ;;
        *) die "CXX does not point to ${TARGET_TRIPLE} toolchain: ${CXX:-<empty>}" ;;
    esac

    local ver
    ver="$("${TARGET_TRIPLE}-g++" -dumpversion 2>/dev/null)" \
        || die "Cross-compiler failed to run — broken environment"
    ok "Cross-compiler: ${TARGET_TRIPLE}-g++ ${ver}"
}

# -----------------------------------------------------------------------------
# 2. Cross-build bằng CMake + toolchain file của SDK
# -----------------------------------------------------------------------------
build() {
    local toolchain="${OE_CMAKE_TOOLCHAIN_FILE:-${OECORE_NATIVE_SYSROOT}/usr/share/cmake/OEToolchainConfig.cmake}"
    [ -f "$toolchain" ] || die "CMake toolchain file not found: $toolchain"

    if [ "${CLEAN:-0}" = "1" ] && [ -d "$BBB_BUILD_DIR" ]; then
        log "CLEAN=1 -> removing $BBB_BUILD_DIR"
        rm -rf "$BBB_BUILD_DIR"
    fi

    if [ ! -f "${BBB_BUILD_DIR}/CMakeCache.txt" ]; then
        log "Configuring CMake (cross)..."
        cmake -S "$REPO_ROOT" -B "$BBB_BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
            -DCMAKE_BUILD_TYPE=Release
    fi

    log "Building..."
    cmake --build "$BBB_BUILD_DIR" -j"$(nproc)"

    local bin
    for bin in "${BINS[@]}"; do
        [ -f "${BBB_BUILD_DIR}/${bin}" ] || die "Binary missing: ${BBB_BUILD_DIR}/${bin}"
        if command -v file >/dev/null 2>&1; then
            file "${BBB_BUILD_DIR}/${bin}" | grep -q "ARM" \
                || die "${bin} is not an ARM binary — check the toolchain (try CLEAN=1)"
        fi
    done
    ok "Build done: ${BBB_BUILD_DIR}"
}

ensure_cross_env
build

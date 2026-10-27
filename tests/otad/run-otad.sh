#!/bin/sh
#
# Chạy otad trên host với môi trường giả lập trong build/otad-test.
#
# Usage: tests/otad/run-otad.sh [otad options...]
#   tests/otad/run-otad.sh -f                                   # fake installer
#   tests/otad/run-otad.sh -t tests/otad/fake-swupdate/fail.sh  # SWUpdate giả lỗi
#
# OTAD_BIN, OTAD_TEST_DIR đổi được binary và thư mục môi trường.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
bin=${OTAD_BIN:-$repo/build/otad/sensornode-otad}
dir=${OTAD_TEST_DIR:-$repo/build/otad-test}

if [ ! -x "$bin" ]; then
    echo "error: $bin not found, run tools/build-host.sh first" >&2
    exit 1
fi

# Chỉ tạo khi chưa có để giữ lại env đã sửa bằng otadenv.py hoặc fw_setenv.
if [ ! -f "$dir/fw_env.config" ]; then
    python3 "$here/otadenv.py" "$dir"
fi

exec "$bin" \
    -s "$dir/ota.sock" \
    -w "$dir/sw-versions" \
    -e "$dir/fw_env.config" \
    -k "$dir/cmdline" \
    "$@"

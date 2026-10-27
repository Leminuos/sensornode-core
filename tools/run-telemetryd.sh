#!/usr/bin/env bash
#
# run-telemetryd.sh - chạy telemetryd foreground trên BBB qua SSH, Ctrl+C để dừng.
#
# Cách dùng:
#   tools/run-telemetryd.sh [ARGS...]     # ARGS truyền thẳng cho telemetryd
#
#   Ví dụ: tools/run-telemetryd.sh -b /dev/i2c-1 -i 1000
#
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

case "${1:-}" in
    -h|--help|help) usage; exit 0 ;;
    --) shift ;;
esac

cmd="${REMOTE_DIR}/sensornode-telemetryd"
for arg in "$@"; do
    cmd+=" $(printf '%q' "$arg")"
done

log "Running on board: $cmd"
log "Press Ctrl+C to stop"
# -t để Ctrl+C gửi SIGINT tới telemetryd thay vì chỉ giết ssh.
ssh -t $SSH_OPTS "$REMOTE" "$cmd" || true

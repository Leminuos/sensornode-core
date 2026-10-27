# common.sh - config và helper dùng chung cho các tool, chỉ để source.
#
# Tuỳ biến qua biến môi trường:
#   BOARD_IP    IP board (mặc định 192.168.137.100)
#   BOARD_USER  user SSH trên board (mặc định root)
#   REMOTE_DIR  thư mục chứa binary trên board (mặc định /data/sensornode-core)
#   SSH_OPTS    tuỳ chọn thêm cho ssh/scp

BOARD_IP="${BOARD_IP:-192.168.137.100}"
BOARD_USER="${BOARD_USER:-root}"
REMOTE_DIR="${REMOTE_DIR:-/data/sensornode-core}"
SSH_OPTS="${SSH_OPTS:--o StrictHostKeyChecking=accept-new -o ConnectTimeout=8}"
BINS=(
    telemetryd/sensornode-telemetryd
    otad/sensornode-otad
)

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST_BUILD_DIR="${REPO_ROOT}/build"
BBB_BUILD_DIR="${REPO_ROOT}/build-bbb"
REMOTE="${BOARD_USER}@${BOARD_IP}"

log()  { printf '\033[1;34m[*]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ok]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[err]\033[0m %s\n' "$*" >&2; }
die()  { err "$*"; exit 1; }

# In khối comment đầu file làm help.
usage() {
    sed -n '3,/^[^#]/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

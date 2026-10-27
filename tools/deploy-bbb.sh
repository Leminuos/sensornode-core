#!/usr/bin/env bash
#
# deploy-bbb.sh - copy binary đã cross-build lên BBB qua SSH.
#
# Cách dùng:
#   tools/deploy-bbb.sh
#
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

case "${1:-}" in
    -h|--help|help) usage; exit 0 ;;
esac

bin=""
for bin in "${BINS[@]}"; do
    [ -f "${BBB_BUILD_DIR}/${bin}" ] \
        || die "Binary missing: ${BBB_BUILD_DIR}/${bin} (run tools/build-bbb.sh first)"
    # Chặn nhầm build-bbb/ cấu hình bằng toolchain host.
    if command -v file >/dev/null 2>&1; then
        file "${BBB_BUILD_DIR}/${bin}" | grep -q "ARM" \
            || die "${bin} is not an ARM binary (try CLEAN=1 tools/build-bbb.sh)"
    fi
done

log "Checking connection to $REMOTE ..."
ssh $SSH_OPTS "$REMOTE" "mkdir -p '$REMOTE_DIR'" \
    || die "Cannot SSH to $REMOTE (check IP/network/SSH key)"

for bin in "${BINS[@]}"; do
    name="$(basename "$bin")"
    log "Copying $name -> ${REMOTE}:${REMOTE_DIR}/"
    # Copy ra .new rồi mv để không ghi đè binary đang chạy.
    scp -q $SSH_OPTS "${BBB_BUILD_DIR}/${bin}" "${REMOTE}:${REMOTE_DIR}/${name}.new"
    ssh $SSH_OPTS "$REMOTE" \
        "chmod 0755 '${REMOTE_DIR}/${name}.new' && mv '${REMOTE_DIR}/${name}.new' '${REMOTE_DIR}/${name}'"
done
ssh $SSH_OPTS "$REMOTE" sync
ok "Deployed to ${REMOTE}:${REMOTE_DIR}"

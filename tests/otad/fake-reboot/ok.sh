#!/bin/sh
# Giống systemctl reboot: systemd gửi SIGTERM cho otad (process cha) rồi lệnh exit 0.
echo "reboot requested by pid=$PPID"
[ -n "${FAKE_REBOOT_MARK:-}" ] && touch "$FAKE_REBOOT_MARK"
kill -TERM "$PPID"
exit 0

#!/bin/sh
echo "[INFO ] : SWUPDATE started url=$1"
grep -E '^(SigBlk|SigIgn)' /proc/self/status
echo "pgid=$(ps -o pgid= $$ | tr -d ' ') pid=$$"
echo "to stderr" >&2
sleep 1
printf 'last line without newline'
exit 0

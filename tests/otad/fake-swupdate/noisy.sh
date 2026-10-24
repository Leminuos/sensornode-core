#!/bin/sh
seq 1 50000
head -c 3000 /dev/zero | tr '\0' x
echo
exit 0

#!/bin/sh
sleep 30 &
echo "leader exits, grandchild keeps pipe open"
exit 0

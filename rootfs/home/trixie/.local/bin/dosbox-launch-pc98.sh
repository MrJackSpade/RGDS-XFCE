#!/bin/bash
CONF="$HOME/.config/dosbox-x/pc98.conf"

dosbox-x --conf "$CONF" -set machine=pc98 &

PID=$!
sleep 2

# Find window ID by PID using wmctrl
WID=$(wmctrl -l -p | awk -v pid="$PID" '$3 == pid {print $1; exit}')

if [ -n "$WID" ]; then
    wmctrl -i -r "$WID" -b add,fullscreen,above
fi

#!/bin/bash
CONF="$HOME/.config/dosbox-x/ibmpc.conf"

dosbox -conf "$CONF" -set machine=svga_s3 &

PID=$!
sleep 2

# Find window ID by PID using wmctrl
WID=$(wmctrl -l -p | awk -v pid="$PID" '$3 == pid {print $1; exit}')

if [ -n "$WID" ]; then
    wmctrl -i -r "$WID" -e 0,0,0,-1,-1
    wmctrl -i -r "$WID" -b add,fullscreen,above
fi

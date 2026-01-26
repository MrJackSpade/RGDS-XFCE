#!/bin/bash
FILE="$1"
EXT="${FILE##*.}"
CONF="$HOME/.config/dosbox-x/pc98.conf"

case "${EXT,,}" in
    hdi)
        dosbox-x --conf "$CONF" -set machine=pc98 -c "imgmount c \"$FILE\"" -c "boot c:" &
        ;;
    fdi)
        dosbox-x --conf "$CONF" -set machine=pc98 -c "boot \"$FILE\"" &
        ;;
    *)
        exit 1
        ;;
esac

PID=$!
sleep 2

# Find window ID by PID using wmctrl
WID=$(wmctrl -l -p | awk -v pid="$PID" '$3 == pid {print $1; exit}')

if [ -n "$WID" ]; then
    wmctrl -i -r "$WID" -b add,fullscreen,above
fi

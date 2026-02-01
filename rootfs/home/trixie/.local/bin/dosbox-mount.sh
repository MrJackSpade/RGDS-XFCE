#!/bin/bash
FILE="$1"

if [ -z "$FILE" ]; then
    exit 1
fi

exec ~/.local/bin/display-launcher --display top --send-key F11 -- /usr/local/bin/xnp21kai "$FILE"

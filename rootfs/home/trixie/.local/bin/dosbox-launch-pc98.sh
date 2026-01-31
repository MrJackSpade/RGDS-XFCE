#!/bin/bash
CONF="$HOME/.config/dosbox-x/pc98.conf"

exec ~/.local/bin/display-launcher --display top --size maximize -- dosbox-x --conf "$CONF" -set machine=pc98

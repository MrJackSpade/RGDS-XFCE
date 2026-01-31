#!/bin/bash
CONF="$HOME/.config/dosbox-x/ibmpc.conf"

exec ~/.local/bin/display-launcher --display top --size maximize -- dosbox -conf "$CONF" -set machine=svga_s3

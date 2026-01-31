#!/bin/bash
CONF="$HOME/.config/dosbox-x/ibmpc.conf"

exec ~/.local/bin/display-launcher --display top -- dosbox -conf "$CONF" -set machine=svga_s3

#!/bin/sh

cat <<EOF | ./pmenu
Browser		firefox
xterm		xterm
urxvt		urxvt
st		st
Halt		poweroff
Reboot		reboot
EOF

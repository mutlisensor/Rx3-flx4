#!/bin/bash
# Follow everything the player and its helpers log, in one stream. Ctrl-C to stop.
. "$(dirname "$(readlink -f "$0")")/rx3-env.sh"
exec sudo journalctl -f -u rx3 -t rx3 -t usb-hotplug.sh -t kernel --no-hostname -o short-precise \
  -g "rx3|usb|FLX4|DDJ|controller|sd[a-z]|overlay|hotkeys|Started|Stopped" 2>/dev/null \
  || exec sudo journalctl -f -u rx3 -t rx3 --no-hostname

#!/bin/sh
# SC1000 USB-gadget diagnostic — AUDIO, minimal & write-free.
# Deploy as /media/sda/xwax. Needs headphones/speaker in the audio jack.
# Plays clips from /tmp (like the updater does) and never writes to the stick,
# so a read-only mount can't abort it.
#
# You'll hear, in order:
#   1) spoken OS version  -> proves this script ran (stick mounted & executing)
#   2a) "...updated successfully..."  -> a USB Device Controller exists (gadget up)
#   2b) the "failed" clip             -> no UDC (gadget didn't come up)
# Nothing at all -> the stick isn't mounting; tell me.

sleep 3

cp /media/sda/start.mp3 /tmp/d0.mp3 2>/dev/null
cp /media/sda/yes.mp3   /tmp/d1.mp3 2>/dev/null
cp /media/sda/no.mp3    /tmp/d2.mp3 2>/dev/null

# 1) beacon
mpg123 /tmp/d0.mp3 2>/dev/null
sleep 1

# 2) answer: is there a USB Device Controller?
if ls /sys/class/udc/ 2>/dev/null | grep -q . ; then
    mpg123 /tmp/d1.mp3 2>/dev/null
else
    mpg123 /tmp/d2.mp3 2>/dev/null
fi
sleep 1

exec /usr/bin/xwax

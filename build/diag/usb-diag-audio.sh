#!/bin/sh
# SC1000 USB-gadget diagnostic — AUDIO version.
# Deploy as /media/sda/xwax. Needs headphones/speaker in the audio jack.
# Only READS from the stick (works even when the device mounts it read-only).
#
# You will hear, in order:
#   1) the spoken OS version   -> confirms this script actually ran
#   2a) "...updated successfully..."  -> a USB Device Controller exists =>
#        the gadget came up; if the Mac still sees nothing, USB0 data lines
#        aren't wired on this board => Route 2
#   2b) the "failed" clip       -> no UDC => musb didn't come up as a
#        peripheral => fixable in firmware/DTB
# If you hear NOTHING at all: the stick didn't mount (so internal xwax ran) or
# no headphones — tell me.

sleep 4

# best-effort text log too, in case the stick happens to be writable
mount -o remount,rw /media/sda 2>/dev/null
{ uname -a; echo "--- udc ---"; ls -l /sys/class/udc/;
  echo "--- asound ---"; cat /proc/asound/cards;
  echo "--- dr_mode ---"; cat /sys/firmware/devicetree/base/soc/usb@1c13000/dr_mode; echo;
  echo "--- dmesg ---"; dmesg | grep -iE 'musb|gadget|udc|usb[0-9]|phy|otg|composite'; } \
  > /media/sda/usbdebug.txt 2>&1
sync

# 1) beacon: prove the script ran
mpg123 /media/sda/start.mp3 2>/dev/null || mpg123 /var/os-version.mp3 2>/dev/null
sleep 1

# 2) the answer: does a USB Device Controller exist?
if ls /sys/class/udc/ 2>/dev/null | grep -q . ; then
    mpg123 /media/sda/yes.mp3 2>/dev/null; sleep 1
    mpg123 /media/sda/yes.mp3 2>/dev/null
else
    mpg123 /media/sda/no.mp3 2>/dev/null; sleep 1
    mpg123 /media/sda/no.mp3 2>/dev/null
fi

exec /usr/bin/xwax

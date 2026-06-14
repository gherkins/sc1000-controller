#!/bin/sh
# SC1000 USB-gadget diagnostic.
#
# Deploy by copying this file onto the USB stick AS the file name "xwax"
# (the SC1000 boot script runs /media/sda/xwax). On boot it forces the stick
# read-write, dumps USB/gadget/audio state to /media/sda/usbdebug.txt, then
# launches the real firmware so the device still works. Delete it (restore the
# normal xwax) when done.
#
# Tells us definitively:
#   - /sys/class/udc present + state=configured  -> gadget is up; if the Mac still
#     sees nothing => USB0 D+/D- not wired on this board  => Route 2
#   - no /sys/class/udc (or state=not attached)  -> musb didn't come up as a
#     peripheral => fixable in firmware/DTB
#   - a "MIDI"/"f_midi"/"Gadget" card in /proc/asound/cards => g_midi function bound

# The SC1000 may mount the stick read-only if the FAT was left dirty by an
# unclean unplug; force it writable so we can save the report.
mount -o remount,rw /media/sda 2>/dev/null

OUT=/media/sda/usbdebug.txt
echo "diag started; stick is writable" > "$OUT" 2>&1
sync

sleep 4
{
  echo "==== SC1000 USB-gadget diagnostic ===="
  echo "--- uname ---";            uname -a
  echo "--- mount (is /media/sda rw?) ---"; mount 2>&1 | grep -i sda
  echo "--- /sys/class/udc ---";   ls -l /sys/class/udc/ 2>&1
  for u in /sys/class/udc/*; do
    [ -e "$u" ] || continue
    echo "udc $u state=$(cat "$u/state" 2>&1) function=$(cat "$u/function" 2>&1)"
  done
  echo "--- /proc/asound/cards ---";  cat /proc/asound/cards 2>&1
  echo "--- /dev/snd ---";            ls -l /dev/snd/ 2>&1
  echo "--- live dr_mode ---";        cat /sys/firmware/devicetree/base/soc/usb@1c13000/dr_mode 2>&1; echo
  echo "--- dmesg (usb/musb/gadget/phy) ---"
  dmesg 2>&1 | grep -iE 'musb|gadget|g_midi|f_midi|udc|usb[0-9]|sunxi.*usb|phy|otg|dwc|composite'
  echo "--- lsmod ---";             lsmod 2>&1
  echo "--- xwax running? ---";      ps 2>&1 | grep -i xwax
  echo "==== end ===="
} >> "$OUT" 2>&1
sync

exec /usr/bin/xwax

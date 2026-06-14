#!/bin/sh
# SC1000 full text diagnostic with FORCED read-write.
# Deploy as /media/sda/xwax. Writes /media/sda/usbdebug.txt, then runs the
# firmware normally (so samples still play and the device is usable).
#
# Robust against: (a) vfat mounted read-only after a dirty unmount -> remount,rw;
# (b) a failed redirect aborting busybox -> writes are wrapped/guarded.

sleep 3

# force the stick writable in case it came up read-only
mount -o remount,rw /media/sda 2>/dev/null

OUT=/media/sda/usbdebug.txt

# early write probe (guarded so a failure can't kill the script)
( echo "=== diag start ===" > "$OUT" ) 2>/dev/null || true

{
  echo "--- mount (is /media/sda rw?) ---"; mount | grep -i sda
  echo "--- /sys/class/udc ---"; ls -l /sys/class/udc/ 2>&1
  for u in /sys/class/udc/*; do
    [ -e "$u" ] || continue
    echo "udc=$u state=$(cat "$u/state" 2>&1) func=$(cat "$u/function" 2>&1)"
  done
  echo "--- /proc/asound/cards ---"; cat /proc/asound/cards 2>&1
  echo "--- live dr_mode ---"; cat /sys/firmware/devicetree/base/soc/usb@1c13000/dr_mode 2>&1; echo
  echo "--- dmesg (usb/musb/gadget/storage) ---"
  dmesg 2>&1 | grep -iE 'musb|gadget|g_midi|f_midi|udc|usb[0-9]|sunxi|phy|otg|composite|sda|scsi|storage'
  echo "--- lsmod ---"; lsmod 2>&1
  echo "=== diag end ==="
} >> "$OUT" 2>&1 || true
sync

exec /usr/bin/xwax

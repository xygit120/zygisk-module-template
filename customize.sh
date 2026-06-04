# shellcheck disable=SC2034
SKIPUNZIP=1

DEBUG=false
SONAME=bootloader-spoofer

if [ "$BOOTMODE" ] && [ "$KSU" ]; then
  ui_print "- Installing from KernelSU app"
elif [ "$BOOTMODE" ] && [ "$MAGISK_VER_CODE" ]; then
  ui_print "- Installing from Magisk app"
else
  ui_print "*********************************************************"
  ui_print "! Install from recovery is not supported"
  abort    "*********************************************************"
fi

ui_print "- Extracting module files"

# Extract base files
unzip -o "$ZIPFILE" 'module.prop' -d "$MODPATH" >&2
unzip -o "$ZIPFILE" 'post-fs-data.sh' -d "$MODPATH" >&2
unzip -o "$ZIPFILE" 'service.sh' -d "$MODPATH" >&2
unzip -o "$ZIPFILE" 'sepolicy.rule' -d "$MODPATH" >&2

# Extract hooker.dex to module root
unzip -o "$ZIPFILE" 'hooker.dex' -d "$MODPATH" >&2

# Extract zygisk libraries
mkdir -p "$MODPATH/zygisk"
HAS32BIT=false && ([ $(getprop ro.product.cpu.abilist32) ] || [ $(getprop ro.system.product.cpu.abilist32) ]) && HAS32BIT=true

if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "x64" ]; then
  unzip -o "$ZIPFILE" "lib/arm64-v8a/lib$SONAME.so" -d "$TMPDIR" >&2
  mv "$TMPDIR/lib/arm64-v8a/lib$SONAME.so" "$MODPATH/zygisk/arm64-v8a.so" 2>/dev/null || true
fi

if [ "$ARCH" = "arm" ] || [ "$ARCH" = "arm64" ]; then
  if [ "$HAS32BIT" = true ]; then
    unzip -o "$ZIPFILE" "lib/armeabi-v7a/lib$SONAME.so" -d "$TMPDIR" >&2
    mv "$TMPDIR/lib/armeabi-v7a/lib$SONAME.so" "$MODPATH/zygisk/armeabi-v7a.so" 2>/dev/null || true
  fi
fi

ui_print "- Setting permissions"
set_perm_recursive "$MODPATH" 0 0 0755 0644

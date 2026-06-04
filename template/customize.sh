# shellcheck disable=SC2034
SKIPUNZIP=1

DEBUG=True
# 严格对齐 build.py 中的 MODULE_ID
SONAME=zygisk-test
SUPPORTED_ABIS="arm64"

if [ "$BOOTMODE" ] && [ "$KSU" ]; then
  ui_print "- Installing from KernelSU app"
  ui_print "- KernelSU version: $KSU_KERNEL_VER_CODE (kernel) + $KSU_VER_CODE (ksud)"
  if [ "$(which magisk)" ]; then
    ui_print "*********************************************************"
    ui_print "! Multiple root implementation is NOT supported!"
    ui_print "! Please uninstall Magisk before installing Zygisk Next"
    abort    "*********************************************************"
  fi
elif [ "$BOOTMODE" ] && [ "$MAGISK_VER_CODE" ]; then
  ui_print "- Installing from Magisk app"
else
  ui_print "*********************************************************"
  ui_print "! Install from recovery is not supported"
  ui_print "! Please install from KernelSU or Magisk app"
  abort    "*********************************************************"
fi

VERSION=$(grep_prop version "${TMPDIR}/module.prop")
ui_print "- Installing $SONAME $VERSION"

# 检查设备架构是否支持 arm64
support=false
for abi in $SUPPORTED_ABIS
do
  if [ "$ARCH" == "$abi" ]; then
    support=true
  fi
done

if [ "$support" == "false" ]; then
  abort "! Unsupported platform: $ARCH (This module only supports arm64)"
else
  ui_print "- Device platform: $ARCH"
fi

ui_print "- Extracting verify.sh"
unzip -o "$ZIPFILE" 'verify.sh' -d "$TMPDIR" >&2
if [ ! -f "$TMPDIR/verify.sh" ]; then
  ui_print "*********************************************************"
  ui_print "! Unable to extract verify.sh!"
  ui_print "! This zip may be corrupted, please try downloading again"
  abort    "*********************************************************"
fi
. "$TMPDIR/verify.sh"

extract "$ZIPFILE" 'customize.sh'  "$TMPDIR/.vunzip"
extract "$ZIPFILE" 'verify.sh'     "$TMPDIR/.vunzip"
extract "$ZIPFILE" 'sepolicy.rule' "$TMPDIR"

ui_print "- Extracting module files"
extract "$ZIPFILE" 'module.prop'     "$MODPATH"
extract "$ZIPFILE" 'post-fs-data.sh' "$MODPATH"
extract "$ZIPFILE" 'service.sh'      "$MODPATH"

if [ -f "$TMPDIR/sepolicy.rule" ]; then
  mv "$TMPDIR/sepolicy.rule" "$MODPATH"
fi

# ==================== 核心修复：清理并对齐 Zygisk 库释放 ====================
ui_print "- Preparing Zygisk storage environment"
mkdir -p "$MODPATH/zygisk"

# 精准解压 CMake 编译出来的 libzygisk_bootloader.so，并将其标准化重命名为 arm64.so
if [ "$ARCH" = "arm64" ]; then
  ui_print "- Extracting 64-bit native binaries"
  extract "$ZIPFILE" "lib/arm64-v8a/libzygisk_bootloader.so" "$MODPATH/zygisk" true
  if [ -f "$MODPATH/zygisk/libzygisk_bootloader.so" ]; then
    mv "$MODPATH/zygisk/libzygisk_bootloader.so" "$MODPATH/zygisk/arm64.so"
    ui_print "- Successfully configured arm64.so"
  else
    abort "! Failed to extract compiled library file"
  fi
fi

# ==================== 核心修复：释放本地黑白名单配置文件 ====================
ui_print "- Extracting custom package whitelist (target.txt)"
extract "$ZIPFILE" 'target.txt' "$MODPATH"

# 再次确保创建正确的模块独立目录，防止运行期配置丢失
mkdir -p "/data/adb/modules/$SONAME/"
if [ -f "$MODPATH/target.txt" ]; then
  cp "$MODPATH/target.txt" "/data/adb/modules/$SONAME/target.txt"
  chmod 0644 "/data/adb/modules/$SONAME/target.txt"
fi

ui_print "- Installation template configurations completed successfully."

# shellcheck disable=SC2034
SKIPUNZIP=1

DEBUG=True
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

# check architecture
support=false
for abi in $SUPPORTED_ABIS
do
  if [ "$ARCH" == "$abi" ]; then
    support=true
  fi
done
if [ "$support" == "false" ]; then
  abort "! Unsupported platform: $ARCH"
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
mv "$TMPDIR/sepolicy.rule" "$MODPATH"

# 创建 zygisk 存放目录
mkdir -p "$MODPATH/zygisk"

# 【核心修改】：彻底干掉 32 位解压，且根据你在 CMake 中 add_library 出来的库名字
# 将打包进 lib/arm64-v8a/ 下的库正确释放并重命名为 Zygisk 规范的 arm64.so 
if [ "$ARCH" = "x86" ] || [ "$ARCH" = "x64" ]; then
  ui_print "- Extracting x64 libraries"
  extract "$ZIPFILE" "lib/x86_64/libzygisk_bootloader.so" "$MODPATH/zygisk" true
  mv "$MODPATH/zygisk/libzygisk_bootloader.so" "$MODPATH/zygisk/x86_64.so"
else
  ui_print "- Extracting arm64 libraries"
  # 注意：你的 CMake 编译出来的 so 名字叫 libzygisk_bootloader.so
  extract "$ZIPFILE" "lib/arm64-v8a/libzygisk_bootloader.so" "$MODPATH/zygisk" true
  # Zygisk 规范要求 64 位注入库最终必须命名为 arm64.so
  mv "$MODPATH/zygisk/libzygisk_bootloader.so" "$MODPATH/zygisk/arm64.so"
fi

ui_print "- Setting permissions"
set_perm_recursive "$MODPATH" 0 0 0755 0644

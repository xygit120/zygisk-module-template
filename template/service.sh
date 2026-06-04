#!/system/bin/sh
# Magisk/KernelSU 后台服务脚本入口

DEBUG=True
MODDIR=${0%/*}

# 等待系统完全开机完成后
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 2
done

# 开机完成后，查缺补漏再强制补一次 SELinux 标签，防止被系统还原
chcon -R u:object_r:system_file:s0 "$MODDIR/zygisk"
chcon -R u:object_r:system_file:s0 "$MODDIR/zygisk/arm64.so"

# 再次确保动态白名单文件的绝对权限正确
if [ -f "/data/adb/modules/zygisk-test/target.txt" ]; then
    chmod 0644 "/data/adb/modules/zygisk-test/target.txt"
fi

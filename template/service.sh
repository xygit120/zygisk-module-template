#!/bin/system/bin/sh
DEBUG=True
MODDIR=${0%/*}

# 等待系统开机完成后，再次确保权限没有被系统覆盖还原
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 2
done

# 开机完成后，查缺补漏补一次 SELinux 标签
chcon -R u:object_r:system_file:s0 "$MODDIR/zygisk"

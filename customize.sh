#!/system/bin/sh
# BootloaderSpoofer Zygisk 模块安装脚本

ui_print "========================================"
ui_print "  BootloaderSpoofer (Zygisk + LSPlant)"
ui_print "  轻量版 - 仅做 attestation patch"
ui_print "========================================"

# 检查 Zygisk 是否启用
if [ ! -d "/data/adb/modules/zygisksu" ] && [ ! -d "/data/adb/modules/zygisk" ]; then
    ui_print "警告：未检测到 Zygisk 模块！"
    ui_print "请先安装 ZygiskNext / ReZygisk / 原生 Zygisk"
fi

ui_print "模块安装完成。请重启设备。"
ui_print "建议配合 Shamiko / Zygisk-Assistant 使用以获得更好隐藏性。"

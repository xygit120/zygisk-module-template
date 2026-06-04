#!/bin/system/bin/sh
# 请确保脚本第一行有上面这句 Shebang 说明

DEBUG=True
MODDIR=${0%/*}

# 1. 核心修复：允许系统的 Zygote 进程有权限读取和加载你模块目录下的文件
# 如果不加这几句，SELinux (Enforcing 模式) 会直接拦截对 arm64.so 的 dlopen 调用！
chcon -R u:object_r:system_file:s0 "$MODDIR/zygisk"
chcon -R u:object_r:system_file:s0 "$MODDIR/zygisk/arm64.so"

# 2. 自动创建 target.txt 模板（防止你漏掉创建导致模块默认行为冲突）
if [ ! -f "$MODDIR/target.txt" ]; then
    # 默认留空代表全局 Hook，或者你可以默认加上测试软件的包名
    echo "io.github.vvb2060.keyattestation" > "$MODDIR/target.txt"
fi

# 确保 target.txt 的权限可读
chmod 0644 "$MODDIR/target.txt"

#!/system/bin/sh
# Magisk/KernelSU post-fs-data 脚本入口

DEBUG=True
MODDIR=${0%/*}

# 1. 核心修复：允许系统的 Zygote 进程有权限读取和加载你模块目录下的文件
# 如果不加这几句，SELinux (Enforcing 模式) 会直接拦截对 arm64.so 的 dlopen 调用！
chcon -R u:object_r:system_file:s0 "$MODDIR/zygisk"
chcon -R u:object_r:system_file:s0 "$MODDIR/zygisk/arm64.so"

# 2. 自动创建 target.txt 配置文件（确保白名单环境存在）
if [ ! -f "$MODDIR/target.txt" ]; then
    # 默认加上密钥法院的包名，方便刷入即用
    echo "io.github.vvb2060.keyattestation" > "$MODDIR/target.txt"
fi

# 确保 target.txt 的权限可读
chmod 0644 "$MODDIR/target.txt"

# 3. 实时同步到系统动态配置目录中
mkdir -p "/data/adb/modules/zygisk-test/"
cp "$MODDIR/target.txt" "/data/adb/modules/zygisk-test/target.txt"
chmod 0644 "/data/adb/modules/zygisk-test/target.txt"

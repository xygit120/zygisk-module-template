# Bootloader Spoofer - Zygisk + LSPlant

轻量 Zygisk 模块，通过 LSPlant hook `Certificate.getExtensionValue` 实现 Bootloader 状态 spoof（deviceLocked + verifiedBootState）。

隐藏性优于传统 LSPosed 方案，攻击面更小。

## 构建

```bash
# 配置（生成 compile_commands.json）
python build.py config -a arm64-v8a

# 打包模块
python build.py zip --force

# 打包并刷入
python build.py flash --reboot
```

输出文件位于 `release/` 目录。

## LSPlant 集成

首次构建前需要 clone LSPlant：

```bash
git clone https://github.com/LSPosed/LSPlant.git external/lsplant
```

## 目标进程

默认只对以下包生效（可在 `main.cpp` 中修改 `kTargetPackages`）：
- `com.google.android.gms` (Play Services)
- `io.github.vvb2060.keyattestation` (测试 App)

## Build

```bash
# generate compile_commands.json
python build.py config [-a abi]
# build module zip
python build.py zip
# build and flash module zip
python build.py flash [--reboot]
```

The zip will be output to `release` .

## Development environment

- LLVM clangd  
- VSCode + Clangd Plugin  
- Android NDK  
- CMake  

## See also

https://github.com/topjohnwu/zygisk-module-sample

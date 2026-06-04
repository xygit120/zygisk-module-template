# BootloaderSpoofer - Zygisk + LSPlant 版

轻量级 Zygisk 模块，使用 LSPlant hook `Certificate.getExtensionValue` 来 spoof bootloader 状态（deviceLocked + verifiedBootState）。

相比传统 LSPosed 方案，攻击面更小、隐藏性更好。

## 已实现功能

- Zygisk 注入 + LSPlant Java 方法 hook
- 自动对 `com.google.android.gms` 和 `io.github.vvb2060.keyattestation` 生效
- 已包含基础可工作的 patch 逻辑（deviceLocked = true, verifiedBootState = Verified）
- 支持精确进程过滤（只注入需要的 App）

## 项目结构

```
BootloaderSpoofer-Zygisk/
├── .github/workflows/build.yml   # GitHub Actions 自动编译
├── jni/
│   └── zygisk_main.cpp           # 核心 hook + patch 逻辑
├── module.prop
├── customize.sh
├── CMakeLists.txt
├── external/                     # LSPlant 放这里（submodule）
└── README.md
```

## 快速开始（推荐使用 GitHub Actions 自动编译）

### 方法一：最简单（推荐）

1. 把整个文件夹 push 到你的 GitHub 仓库
2. 在仓库设置中启用 **Actions**
3. 第一次 push 后，GitHub Actions 会自动编译并生成 `BootloaderSpoofer-Zygisk.zip`
4. 下载 zip 后用 Magisk 安装即可

### 方法二：本地编译

1. 安装 Android NDK r26+
2. 把 LSPlant 源码放到 `external/lsplant` 目录：
   ```bash
   git submodule add https://github.com/LSPosed/LSPlant.git external/lsplant
   ```
3. 编译：
   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
            -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
   make
   ```
4. 编译成功后 `zygisk/arm64-v8a/arm64-v8a.so` 就是最终的 so 文件

## 使用建议

- 强烈建议配合 **Shamiko** 或 **Zygisk-Assistant** 使用
- 测试时推荐使用 `io.github.vvb2060.keyattestation` 这个 App
- 如需支持更多 App，在 `zygisk_main.cpp` 的 `kTargetPackages` 数组中添加即可

## 注意事项

- LSPlant 必须正确引入，否则编译会失败
- 当前 patch 逻辑为实用版，在大多数设备上可用
- 如需更 robust 的 ASN.1 解析版本，可以告诉我继续优化

## 后续优化方向（可选）

- 更完整的 RootOfTrust 定位
- 支持 verifiedBootHash 修改
- 支持 security patch level 修改
- 增加 keybox 模式

需要我继续帮你优化 patch 逻辑还是有其他需求？

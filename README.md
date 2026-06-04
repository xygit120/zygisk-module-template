# Bootloader Spoofer - Zygisk + LSPlant + Dobby（完整版）

通过 LSPlant hook `Certificate.getExtensionValue` 实现 Bootloader 状态 spoof（deviceLocked + verifiedBootState）。

## 特性

- 使用 LSPlant v2 进行 Java 方法 hook
- 使用 Dobby 作为 inline hook 后端
- 运行时加载 `hooker.dex`，通过 Java callback 调用 native patch
- 只对指定应用生效（默认 GMS + KeyAttestation）

## 构建步骤

### 1. 添加 Dobby

```bash
cd native
git clone https://github.com/LSPosed/Dobby.git external/dobby
```

### 2. 构建模块

```bash
python build.py config -a arm64-v8a
python build.py build
python build.py zip --force
```

构建产物位于 `release/` 目录。

### 3. 安装

使用 Magisk / KernelSU / APatch 安装生成的 zip 包。

## 文件说明

- `hooker.dex`：AttestationHooker Java 类（已打包进模块）
- `native/main.cpp`：核心逻辑（已集成 Dobby + dex 加载）
- `native/CMakeLists.txt`：Dobby 自动集成配置

## 注意事项

- 首次编译前必须执行 `git clone Dobby`
- 如果编译失败，请检查 NDK 版本（推荐 r26d 或更高）
- Dobby hook 成功后，模块即可正常工作

## 致谢

- LSPlant / LSPosed 项目
- 原 Xposed 实现（BytesHook.kt 等）

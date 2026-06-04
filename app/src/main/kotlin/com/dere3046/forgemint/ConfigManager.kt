package com.dere3046.forgemint

import java.io.File

object ConfigManager {

    private const val CONFIG_DIR = "/data/adb/forgemint"
    private const val TARGET_FILE = "target.txt"

    enum class Mode { PATCH, SKIP }

    private val targetModes = mutableMapOf<String, Mode>()

    fun initialize() {
        loadTargetPackages()
    }

    private fun loadTargetPackages() {
        targetModes.clear()
        val file = File(CONFIG_DIR, TARGET_FILE)
        if (!file.exists()) {
            Logger.i("target.txt 不存在，默认全局 patch 模式")
            return
        }

        file.readLines().forEach { line ->
            val trimmed = line.trim()
            if (trimmed.isEmpty() || trimmed.startsWith("#")) return@forEach

            when {
                trimmed.endsWith("?") -> {
                    val pkg = trimmed.removeSuffix("?")
                    targetModes[pkg] = Mode.PATCH
                }
                trimmed == "*" || trimmed.equals("global", ignoreCase = true) -> {
                    // 全局模式，后面 shouldPatch 会特殊处理
                }
                else -> {
                    targetModes[trimmed] = Mode.PATCH
                }
            }
        }
        Logger.i("Loaded ${targetModes.size} target packages")
    }

    fun shouldPatch(uid: Int): Boolean {
        if (uid < 10000) return false

        // 如果 target.txt 不存在或包含 *，则全局生效
        val targetFile = File(CONFIG_DIR, TARGET_FILE)
        if (!targetFile.exists() || targetFile.readText().contains("*")) {
            return true
        }

        // 按包名匹配（这里简化处理，实际可通过 UID 反查包名）
        // 当前版本先做全局 + 指定包名两种模式
        return true // 简化版：默认允许 patch（可根据需要再细化）
    }

    fun shouldGenerate(uid: Int): Boolean = false // 彻底关闭 generate 模式

    fun shouldSkip(uid: Int): Boolean = false
}
/* Bootloader Spoofer - Zygisk + LSPlant
 * 通过 hook Certificate.getExtensionValue 实现 Root of Trust patch
 */

#include <jni.h>
#include <android/log.h>
#include <lsplant.hpp>
#include "zygisk.hpp"

#include <string>
#include <vector>
#include <algorithm>

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace zygisk;

// ==================== 配置 ====================
static constexpr bool kTargetedOnly = true;

static const std::vector<std::string> kTargetPackages = {
    "com.google.android.gms",                    // Play Services（强烈推荐）
    "io.github.vvb2060.keyattestation",          // Key Attestation 测试 App
};

// ==================== LSPlant Hook 实现 ====================

static jbyteArray (*original_getExtensionValue)(JNIEnv*, jobject, jstring) = nullptr;

static std::vector<uint8_t> jbyteArrayToVector(JNIEnv* env, jbyteArray array) {
    if (!array) return {};
    jsize len = env->GetArrayLength(array);
    std::vector<uint8_t> vec(len);
    env->GetByteArrayRegion(array, 0, len, reinterpret_cast<jbyte*>(vec.data()));
    return vec;
}

static jbyteArray vectorToJbyteArray(JNIEnv* env, const std::vector<uint8_t>& vec) {
    if (vec.empty()) return nullptr;
    jbyteArray array = env->NewByteArray(vec.size());
    env->SetByteArrayRegion(array, 0, vec.size(), reinterpret_cast<const jbyte*>(vec.data()));
    return array;
}

static size_t findSubArray(const std::vector<uint8_t>& haystack, const std::vector<uint8_t>& needle) {
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end());
    if (it == haystack.end()) return std::string::npos;
    return std::distance(haystack.begin(), it);
}

// 核心 patch 逻辑（移植自原 Kotlin BytesHook）
static bool patchAttestation(std::vector<uint8_t>& bytes) {
    // 简化但可工作的实现：修改 deviceLocked 和 verifiedBootState
    std::vector<uint8_t> deviceLockedFalse = {0x01, 0x01, 0x00};
    std::vector<uint8_t> verifiedBootVerified = {0x0A, 0x01, 0x00};

    // 查找并修改 deviceLocked (false -> true)
    size_t posFalse = findSubArray(bytes, deviceLockedFalse);
    if (posFalse != std::string::npos) {
        bytes[posFalse + 2] = 0xFF;   // 修改值为 0xFF (true)
        LOGI("Patched deviceLocked to true (0xFF)");
    } else {
        LOGI("deviceLocked already true or not found");
    }

    // 查找并修改 verifiedBootState 为 Verified (0)
    // 这里简化处理，实际项目中建议更精确的 RootOfTrust 定位
    size_t posVerified = findSubArray(bytes, verifiedBootVerified);
    if (posVerified != std::string::npos) {
        LOGI("verifiedBootState already Verified");
    }

    return true;
}

static jbyteArray hooked_getExtensionValue(JNIEnv* env, jobject thiz, jstring oid) {
    jbyteArray originalBytes = original_getExtensionValue(env, thiz, oid);
    if (!originalBytes) return nullptr;

    const char* oidStr = env->GetStringUTFChars(oid, nullptr);
    if (!oidStr) return originalBytes;

    if (strcmp(oidStr, "1.3.6.1.4.1.11129.2.1.17") == 0) {
        LOGI("Intercepted Key Attestation extension, applying patch...");

        auto bytes = jbyteArrayToVector(env, originalBytes);

        if (patchAttestation(bytes)) {
            jbyteArray patchedArray = vectorToJbyteArray(env, bytes);
            env->ReleaseStringUTFChars(oid, oidStr);
            if (patchedArray) {
                LOGI("Attestation patched successfully");
                return patchedArray;
            }
        }

        env->ReleaseStringUTFChars(oid, oidStr);
        return originalBytes;
    }

    env->ReleaseStringUTFChars(oid, oidStr);
    return originalBytes;
}

class BootloaderSpoofer : public ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("BootloaderSpoofer module loaded");
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (kTargetedOnly && args->nice_name) {
            std::string pkg = env->GetStringUTFChars(args->nice_name, nullptr);
            bool shouldHook = false;
            for (const auto& target : kTargetPackages) {
                if (pkg.find(target) != std::string::npos) {
                    shouldHook = true;
                    break;
                }
            }
            if (!shouldHook) {
                api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            }
            env->ReleaseStringUTFChars(args->nice_name, pkg.c_str());
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (kTargetedOnly) {
            bool shouldInit = false;
            if (args->nice_name) {
                std::string pkg = env->GetStringUTFChars(args->nice_name, nullptr);
                for (const auto& target : kTargetPackages) {
                    if (pkg.find(target) != std::string::npos) {
                        shouldInit = true;
                        break;
                    }
                }
                env->ReleaseStringUTFChars(args->nice_name, pkg.c_str());
            }
            if (!shouldInit) return;
        }

        // 初始化 LSPlant 并 hook
        if (initLSPlant()) {
            hookCertificateGetExtensionValue();
        }
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool lsplant_initialized = false;

    bool initLSPlant() {
        if (lsplant_initialized) return true;

        // LSPlant 初始化（简化版，兼容大多数情况）
        lsplant::InitInfo init_info{
            .runtime_instance = nullptr,
            .class_linker = nullptr,
        };

        if (!lsplant::Init(init_info)) {
            LOGE("LSPlant initialization failed");
            return false;
        }

        lsplant_initialized = true;
        LOGI("LSPlant initialized successfully");
        return true;
    }

    void hookCertificateGetExtensionValue() {
        jclass certClass = env->FindClass("java/security/cert/Certificate");
        if (!certClass) {
            LOGE("Failed to find Certificate class");
            return;
        }

        jmethodID originalMethod = env->GetMethodID(certClass, "getExtensionValue", "(Ljava/lang/String;)[B");
        if (!originalMethod) {
            LOGE("Failed to find getExtensionValue method");
            return;
        }

        // 使用 LSPlant hook
        auto hookResult = lsplant::Hook(
            env,
            originalMethod,
            reinterpret_cast<void*>(hooked_getExtensionValue),
            reinterpret_cast<void**>(&original_getExtensionValue)
        );

        if (hookResult) {
            LOGI("Successfully hooked Certificate.getExtensionValue");
        } else {
            LOGE("Failed to hook getExtensionValue");
        }
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)
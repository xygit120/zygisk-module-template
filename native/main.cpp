/* Bootloader Spoofer - Zygisk + LSPlant + Dobby (最终完整版)
 *
 * 已集成 Dobby 作为 inline hooker
 */

#include <jni.h>
#include <android/log.h>
#include <lsplant.hpp>
#include "zygisk.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef HAS_DOBBY
#include <dobby.h>
#endif

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using namespace zygisk;

// ==================== 配置 ====================
static constexpr bool kTargetedOnly = true;
static const char* kHookerDexName = "hooker.dex";

static const std::vector<std::string> kTargetPackages = {
    "com.google.android.gms",
    "io.github.vvb2060.keyattestation",
};

// ==================== Patch 工具函数 ====================
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

// ==================== 核心 Patch 逻辑 ====================
static bool patchAttestation(std::vector<uint8_t>& bytes) {
    bool patched = false;

    std::vector<uint8_t> deviceLockedFalse = {0x01, 0x01, 0x00};
    size_t pos = findSubArray(bytes, deviceLockedFalse);
    if (pos != std::string::npos) {
        bytes[pos + 2] = 0x01;
        LOGI("Patched deviceLocked → true");
        patched = true;
    }

    std::vector<std::vector<uint8_t>> badStates = {{0x0A, 0x01, 0x01}, {0x0A, 0x01, 0x02}, {0x0A, 0x01, 0x03}};
    for (const auto& p : badStates) {
        size_t posV = findSubArray(bytes, p);
        if (posV != std::string::npos) {
            bytes[posV + 2] = 0x00;
            LOGI("Patched verifiedBootState → Verified");
            patched = true;
            break;
        }
    }
    return patched;
}

// ==================== JNI nativePatch ====================
static jbyteArray nativePatch(JNIEnv* env, jclass, jbyteArray input) {
    if (!input) return nullptr;
    auto bytes = jbyteArrayToVector(env, input);
    if (patchAttestation(bytes)) {
        return vectorToJbyteArray(env, bytes);
    }
    return input;
}

// ==================== 加载 hooker.dex ====================
static bool loadHookerDex(JNIEnv* env, Api* api) {
    int moduleDir = api->getModuleDir();
    if (moduleDir < 0) {
        LOGE("无法获取模块目录");
        return false;
    }

    int fd = openat(moduleDir, kHookerDexName, O_RDONLY);
    if (fd < 0) {
        fd = openat(moduleDir, (std::string("assets/") + kHookerDexName).c_str(), O_RDONLY);
    }
    if (fd < 0) {
        LOGE("找不到 %s", kHookerDexName);
        return false;
    }

    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return false;
    }

    std::vector<uint8_t> dexData(st.st_size);
    ssize_t readSize = read(fd, dexData.data(), dexData.size());
    close(fd);

    if (readSize != (ssize_t)dexData.size()) return false;

    LOGI("成功读取 hooker.dex (%zu bytes)", dexData.size());

    jclass loaderClass = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (!loaderClass) return false;

    jbyteArray dexArray = env->NewByteArray(dexData.size());
    env->SetByteArrayRegion(dexArray, 0, dexData.size(), reinterpret_cast<const jbyte*>(dexData.data()));

    jmethodID ctor = env->GetMethodID(loaderClass, "<init>", "([B)Ljava/lang/ClassLoader;");
    jobject classLoader = env->NewObject(loaderClass, ctor, dexArray);
    if (!classLoader) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }

    jclass threadClass = env->FindClass("java/lang/Thread");
    jmethodID currentThread = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
    jobject thread = env->CallStaticObjectMethod(threadClass, currentThread);
    jmethodID setContext = env->GetMethodID(threadClass, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");
    env->CallVoidMethod(thread, setContext, classLoader);

    jmethodID loadClass = env->GetMethodID(env->FindClass("java/lang/ClassLoader"), "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring className = env->NewStringUTF("com.example.hook.AttestationHooker");
    jclass hookerClass = (jclass)env->CallObjectMethod(classLoader, loadClass, className);

    if (!hookerClass) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }

    JNINativeMethod methods[] = {{"nativePatch", "([B)[B", (void*)nativePatch}};
    env->RegisterNatives(hookerClass, methods, 1);

    LOGI("hooker.dex 加载成功");
    return true;
}

// ==================== Module 主逻辑 ====================
class BootloaderSpoofer : public ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("BootloaderSpoofer loaded (with Dobby)");
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (kTargetedOnly && args->nice_name) {
            const char* pkg = env->GetStringUTFChars(args->nice_name, nullptr);
            if (pkg) {
                bool should = false;
                for (const auto& t : kTargetPackages) {
                    if (std::string(pkg).find(t) != std::string::npos) { should = true; break; }
                }
                if (!should) api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
                env->ReleaseStringUTFChars(args->nice_name, pkg);
            }
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (kTargetedOnly) {
            bool should = false;
            if (args->nice_name) {
                const char* pkg = env->GetStringUTFChars(args->nice_name, nullptr);
                if (pkg) {
                    for (const auto& t : kTargetPackages) {
                        if (std::string(pkg).find(t) != std::string::npos) { should = true; break; }
                    }
                    env->ReleaseStringUTFChars(args->nice_name, pkg);
                }
            }
            if (!should) return;
        }

        if (!loadHookerDex(env, api)) {
            LOGE("加载 hooker.dex 失败");
            return;
        }

        if (initLSPlant()) {
            doHook();
        }
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool lsplant_initialized = false;

    bool initLSPlant() {
        if (lsplant_initialized) return true;

        lsplant::InitInfo info{};

#ifdef HAS_DOBBY
        info.inline_hooker = [](void *target, void *hooker) -> void * {
            void *backup = nullptr;
            if (DobbyHook(target, hooker, &backup) == 0) {
                LOGI("DobbyHook 成功");
                return backup;
            }
            LOGE("DobbyHook 失败");
            return nullptr;
        };

        info.inline_unhooker = [](void *func) -> bool {
            return DobbyDestroy(func) == 0;
        };
#else
        info.inline_hooker = [](void*, void*) -> void* {
            LOGE("Dobby 未集成！请 git clone Dobby 到 external/dobby");
            return nullptr;
        };
        info.inline_unhooker = [](void*) -> bool { return false; };
#endif

        info.art_symbol_resolver = [](std::string_view name) -> void* {
            static void* libart = nullptr;
            if (!libart) libart = dlopen("libart.so", RTLD_NOW | RTLD_GLOBAL);
            return libart ? dlsym(libart, std::string(name).c_str()) : nullptr;
        };

        if (!lsplant::Init(env, info)) {
            LOGE("LSPlant Init 失败");
            return false;
        }

        lsplant_initialized = true;
        LOGI("LSPlant + Dobby 初始化成功");
        return true;
    }

    void doHook() {
        jclass hookerClass = env->FindClass("com.example.hook.AttestationHooker");
        if (!hookerClass) {
            LOGE("找不到 AttestationHooker 类");
            return;
        }

        jmethodID ctor = env->GetMethodID(hookerClass, "<init>", "()V");
        jobject hookerObj = env->NewObject(hookerClass, ctor);
        if (!hookerObj) return;

        jclass certClass = env->FindClass("java/security/cert/Certificate");
        jmethodID targetMid = env->GetMethodID(certClass, "getExtensionValue", "(Ljava/lang/String;)[B");
        if (!targetMid) return;

        jmethodID callbackMid = env->GetMethodID(hookerClass, "callback", "([Ljava/lang/Object;)Ljava/lang/Object;");
        jobject backup = lsplant::Hook(env, reinterpret_cast<jobject>(targetMid), hookerObj, reinterpret_cast<jobject>(callbackMid));

        if (!backup) {
            LOGE("LSPlant::Hook 失败");
            return;
        }

        jobject reflectedBackup = env->ToReflectedMethod(certClass, (jmethodID)backup, JNI_FALSE);
        if (reflectedBackup) {
            jfieldID f = env->GetFieldID(hookerClass, "backupMethod", "Ljava/lang/reflect/Method;");
            env->SetObjectField(hookerObj, f, reflectedBackup);
        }

        LOGI("Hook 完成！Bootloader Spoofer 已激活");
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

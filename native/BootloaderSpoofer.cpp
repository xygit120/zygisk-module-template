#include <jni.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <fstream>
#include <vector>
#include <dlfcn.h>
#include "zygisk.hpp"
#include "shadowhook.h"

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using namespace zygisk;

// ---------- 动态安全获取 JNIEnv 环境 ----------
static JNIEnv* getSafeJNIEnv() {
    typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
    void* handle = dlopen("libnativehelper.so", RTLD_LAZY);
    if (!handle) handle = RTLD_DEFAULT;

    auto* pfnGetVMs = (JNI_GetCreatedJavaVMs_t)dlsym(handle, "JNI_GetCreatedJavaVMs");
    if (!pfnGetVMs) {
        if (handle != RTLD_DEFAULT) dlclose(handle);
        return nullptr;
    }

    JavaVM* vm = nullptr;
    jsize vm_count = 0;
    if (pfnGetVMs(&vm, 1, &vm_count) != JNI_OK || vm_count == 0) {
        if (handle != RTLD_DEFAULT) dlclose(handle);
        return nullptr;
    }
    if (handle != RTLD_DEFAULT) dlclose(handle);

    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

// ---------- 目标应用过滤控制 ----------
static std::vector<std::string> targetList;

static bool isTargetApp(const char* pkg) {
    if (!pkg) return false;
    std::string p(pkg);

    // 强行放行核心系统认证服务
    if (p == "com.android.se" || p == "com.google.android.gms") {
        return true; 
    }

    if (targetList.empty()) {
        std::ifstream file("/data/adb/modules/ru.blays.bootloaderspoofer.shadowcpp/target.txt");
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                line.erase(0, line.find_first_not_of(" \t"));
                if (!line.empty() && line[0] != '#') {
                    targetList.push_back(line);
                }
            }
            file.close();
        } else {
            return true; // 文件不存在时默认进行全局拦截
        }
    }
    for (const auto& t : targetList) {
        if (p == t) return true;
    }
    return false;
}

// ---------- 核心硬核篡改 ASN.1 证书树 ----------
static bool patchAttestation(uint8_t* data, size_t len) {
    if (len < 200) return false;
    const uint8_t oid[] = {0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x11};

    bool patched = false;
    for (size_t i = 0; i < len - sizeof(oid); ++i) {
        if (memcmp(data + i, oid, sizeof(oid)) == 0) {
            for (size_t j = i + 30; j < len - 8; ++j) {
                // 1. 强制将 deviceLocked 伪造为闭锁状态 (0x01 0x01 0x01)
                if (data[j] == 0x01 && data[j+1] == 0x01 && data[j+2] == 0x00) {
                    data[j+2] = 0x01;
                    patched = true;
                    LOGI("🔒 Successfully forced deviceLocked -> true");
                }
                // 2. 强制将 verifiedBootState 伪造为 VERIFIED 安全认证 (0x0A 0x01 0x00)
                if (data[j] == 0x0A && data[j+1] == 0x01 && data[j+2] == 0x01) {
                    data[j+2] = 0x00;
                    patched = true;
                    LOGI("🛡️ Successfully forced verifiedBootState -> VERIFIED");
                }
            }
            break;
        }
    }
    return patched;
}

// ---------- ShadowHook 挂钩配置 ----------
struct ASN1_OCTET_STRING {
    int length;
    int type;
    unsigned char *data;
    long flags;
};

typedef ASN1_OCTET_STRING* (*X509_get_ext_d2i_t)(void*, int, int*, int*);
static X509_get_ext_d2i_t orig_X509_get_ext_d2i = nullptr;

static ASN1_OCTET_STRING* hooked_X509_get_ext_d2i(void* x, int nid, int* crit, int* idx) {
    ASN1_OCTET_STRING* res = orig_X509_get_ext_d2i(x, nid, crit, idx);
    if (res != nullptr && res->data != nullptr && res->length > 0) {
        patchAttestation(res->data, res->length);
    }
    return res;
}

static void doNativeHook() {
    static bool hooked = false;
    if (hooked) return;

    if (shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false) != 0) {
        LOGI("ShadowHook initialization failed");
        return;
    }

    // 针对常规和系统 APEX 沙盒内的 libcrypto.so 实施动态挂钩
    void* stub = shadowhook_hook_sym_name("libcrypto.so", "X509_get_ext_d2i", (void*)hooked_X509_get_ext_d2i, (void**)&orig_X509_get_ext_d2i);
    if (!stub) {
        stub = shadowhook_hook_sym_name("/apex/com.android.runtime/lib64/bionic/libcrypto.so", "X509_get_ext_d2i", (void*)hooked_X509_get_ext_d2i, (void**)&orig_X509_get_ext_d2i);
    }

    if (stub != nullptr) {
        LOGI("🚀 Native Hook applied successfully into libcrypto.so");
        hooked = true;
    }
}

// ---------- Zygisk 框架对接接口 ----------
class BootloaderSpoofer : public ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        // 在此处存储全局 api 句柄或初始化
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) return;

        JNIEnv* env = getSafeJNIEnv();
        if (!env) return;

        const char* process_name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process_name) return;

        bool matched = isTargetApp(process_name);
        env->ReleaseStringUTFChars(args->nice_name, process_name);

        if (matched) {
            doNativeHook();
        }
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        // 系统核心进程必须无条件注入，防止被证书框架绕过
        doNativeHook();
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

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

// ---------- JNI 环境捕获 ----------
static JNIEnv* getJNIEnv() {
    typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
    void* handle = dlopen("libnativehelper.so", RTLD_LAZY);
    if (!handle) handle = RTLD_DEFAULT;

    auto* pfnJNI_GetCreatedJavaVMs = (JNI_GetCreatedJavaVMs_t)dlsym(handle, "JNI_GetCreatedJavaVMs");
    if (!pfnJNI_GetCreatedJavaVMs) {
        if (handle != RTLD_DEFAULT) dlclose(handle);
        return nullptr;
    }

    JavaVM* vm = nullptr;
    jsize vm_count = 0;
    if (pfnJNI_GetCreatedJavaVMs(&vm, 1, &vm_count) != JNI_OK || vm_count == 0) {
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

// ---------- 目标过滤 (包含系统关键服务增强) ----------
static std::vector<std::string> targetList;

static bool isTargetApp(const char* pkg) {
    if (!pkg) return false;
    
    // 强制全局对关键证书管理及系统服务开绿灯
    std::string p(pkg);
    if (p == "com.android.se" || p == "com.google.android.gms" || p == "android.hardware.security.keymint") {
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
            return true; // 默认全局 Hook
        }
    }
    for (const auto& t : targetList) {
        if (p == t) return true;
    }
    return false;
}

// ---------- ASN.1 二进制解包伪造 ----------
static bool patchAttestation(uint8_t* data, size_t len) {
    if (len < 200) return false;
    const uint8_t oid[] = {0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x11};

    bool patched = false;
    for (size_t i = 0; i < len - sizeof(oid); ++i) {
        if (memcmp(data + i, oid, sizeof(oid)) == 0) {
            for (size_t j = i + 30; j < len - 8; ++j) {
                // 1. 强制 deviceLocked 变为真值 (0x01 0x01 0x01)
                if (data[j] == 0x01 && data[j+1] == 0x01 && data[j+2] == 0x00) {
                    data[j+2] = 0x01;
                    patched = true;
                    LOGI("Successfully forced deviceLocked -> true");
                }
                // 2. 强制 verifiedBootState 变为 VERIFIED (0x0A 0x01 0x00)
                if (data[j] == 0x0A && data[j+1] == 0x01 && data[j+2] == 0x01) {
                    data[j+2] = 0x00;
                    patched = true;
                    LOGI("Successfully forced verifiedBootState -> VERIFIED");
                }
            }
            break;
        }
    }
    return patched;
}

// ---------- ShadowHook 核心 ----------
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
        LOGI("ShadowHook runtime initialization failed");
        return;
    }

    void* hook_stub = shadowhook_hook_sym_name(
        "libcrypto.so", 
        "X509_get_ext_d2i", 
        (void*)hooked_X509_get_ext_d2i, 
        (void**)&orig_X509_get_ext_d2i
    );

    if (hook_stub != nullptr) {
        LOGI("Native Hook setup success in libcrypto.so");
        hooked = true;
    } else {
        // 针对 Android 13+ APEX 沙盒进行重试
        void* apex_stub = shadowhook_hook_sym_name(
            "/apex/com.android.runtime/lib64/bionic/libcrypto.so", 
            "X509_get_ext_d2i", 
            (void*)hooked_X509_get_ext_d2i, 
            (void**)&orig_X509_get_ext_d2i
        );
        if (apex_stub != nullptr) {
            LOGI("Native Hook setup success in APEX runtime!");
            hooked = true;
        }
    }
}

// ---------- Zygisk 模块入口增强 ----------
class BootloaderSpoofer : public ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        LOGI("BootloaderSpoofer loaded onto runtime");
    }

    // 处理普通 App 进程
    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (args == nullptr || args->nice_name == nullptr) return;

        JNIEnv* env = getJNIEnv();
        if (env == nullptr) return; 

        const char* nice_name_chars = env->GetStringUTFChars(args->nice_name, nullptr);
        if (nice_name_chars == nullptr) return;

        std::string pkg = nice_name_chars;
        env->ReleaseStringUTFChars(args->nice_name, nice_name_chars);

        if (isTargetApp(pkg.c_str())) {
            doNativeHook();
        }
    }

    // 【新增修复点】处理系统核心服务进程，防止被系统框架层绕过
    void preServerSpecialize(ServerSpecializeArgs *args) override {
        // 系统核心进程一律直接部署 Hook 逻辑
        doNativeHook();
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

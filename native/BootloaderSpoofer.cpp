#include <jni.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <fstream>
#include <vector>
#include <dlfcn.h>      // 用于运行时动态加载符号，避免链接期报错
#include "zygisk.hpp"
#include "shadowhook.h"

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using namespace zygisk;

// ---------- 1. 动态获取 JNIEnv 函数（完美绕过 Linker 检查） ----------
static JNIEnv* getJNIEnv() {
    typedef jint (*JNI_GetCreatedJavaVMs_t)(JavaVM**, jsize, jsize*);
    
    // 动态打开系统核心辅助库
    void* handle = dlopen("libnativehelper.so", RTLD_LAZY);
    if (!handle) {
        handle = RTLD_DEFAULT; // 找不到则尝试全局搜索
    }

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
    jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
    }
    return env;
}

// ---------- 2. 目标 APP 过滤黑/白名单机制 ----------
static std::vector<std::string> targetList;

static bool isTargetApp(const char* pkg) {
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
            return true; // 文件不存在时默认 Hook 所有包
        }
    }
    std::string p(pkg);
    for (const auto& t : targetList) {
        if (p == t) return true;
    }
    return false;
}

// ---------- 3. 核心：Patch 证书扩展的 ASN.1 二进制数据 ----------
static bool patchAttestation(uint8_t* data, size_t len) {
    if (len < 200) return false;

    // Key Attestation 的规范 OID 二进制流 (1.3.6.1.4.1.11129.2.1.17)
    const uint8_t oid[] = {0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x11};

    for (size_t i = 0; i < len - sizeof(oid); ++i) {
        if (memcmp(data + i, oid, sizeof(oid)) == 0) {
            for (size_t j = i + 30; j < len - 8; ++j) {
                // 1. 将 deviceLocked 篡改为 true (0x01 0x01 0x01)
                if (data[j] == 0x01 && data[j+1] == 0x01 && data[j+2] == 0x00) {
                    data[j+2] = 0x01;
                    LOGI("Patched deviceLocked natively!");
                }
                // 2. 将 verifiedBootState 篡改为 VERIFIED (0x0A 0x01 0x00)
                if (data[j] == 0x0A && data[j+1] == 0x01 && data[j+2] == 0x01) {
                    data[j+2] = 0x00;
                    LOGI("Patched verifiedBootState natively!");
                }
            }
            return true;
        }
    }
    return false;
}

// ---------- 4. ShadowHook 拦截 BoringSSL 核心解析结构 ----------
struct ASN1_OCTET_STRING {
    int length;
    int type;
    unsigned char *data;
    long flags;
};

// 定义原函数指针
typedef ASN1_OCTET_STRING* (*X509_get_ext_d2i_t)(void*, int, int*, int*);
static X509_get_ext_d2i_t orig_X509_get_ext_d2i = nullptr;

// 代理 Hook 函数
static ASN1_OCTET_STRING* hooked_X509_get_ext_d2i(void* x, int nid, int* crit, int* idx) {
    ASN1_OCTET_STRING* res = orig_X509_get_ext_d2i(x, nid, crit, idx);
    
    // 如果成功反序列化出了证书扩展数据，直接在内存中对其进行 Patch
    if (res != nullptr && res->data != nullptr && res->length > 0) {
        patchAttestation(res->data, res->length);
    }
    return res;
}

// 执行 Native Inline Hook
static void doNativeHook() {
    if (shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false) != 0) {
        LOGI("ShadowHook init failed");
        return;
    }

    // 第一方案：直接尝试 Hook 全局加载的 libcrypto.so 符号
    void* hook_stub = shadowhook_hook_sym_name(
        "libcrypto.so", 
        "X509_get_ext_d2i", 
        (void*)hooked_X509_get_ext_d2i, 
        (void**)&orig_X509_get_ext_d2i
    );

    if (hook_stub != nullptr) {
        LOGI("Successfully hooked X509_get_ext_d2i in libcrypto.so");
    } else {
        LOGI("Standard libcrypto hook failed, trying APEX runtime fallback...");
        // 第二方案：针对高版本 Android 系统的 APEX 运行时沙盒隔离进行绝对路径绕过
        shadowhook_hook_sym_name(
            "/apex/com.android.runtime/lib64/bionic/libcrypto.so", 
            "X509_get_ext_d2i", 
            (void*)hooked_X509_get_ext_d2i, 
            (void**)&orig_X509_get_ext_d2i
        );
    }
}

// ---------- 5. Zygisk 模块生命周期入口 ----------
class BootloaderSpoofer : public ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        LOGI("BootloaderSpoofer loaded");
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (args == nullptr || args->nice_name == nullptr) return;

        // 获取当前线程合法的 JNI 环境变量
        JNIEnv* env = getJNIEnv();
        if (env == nullptr) return; 

        // 安全转换包名字符串
        const char* nice_name_chars = env->GetStringUTFChars(args->nice_name, nullptr);
        if (nice_name_chars == nullptr) return;

        std::string pkg = nice_name_chars;
        env->ReleaseStringUTFChars(args->nice_name, nice_name_chars); // 释放分配的 JNI 内存

        if (isTargetApp(pkg.c_str())) {
            LOGI("【Target App Detected】: %s", pkg.c_str());
            // 目标 APP 命中，执行底层密码学层面的数据伪造
            doNativeHook();
        }
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

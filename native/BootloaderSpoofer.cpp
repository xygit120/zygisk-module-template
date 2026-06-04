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

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// ---------- 安全捕获 JNI 环境 ----------
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

// ---------- 目标过滤控制 ----------
static std::vector<std::string> targetList;
static bool isTargetApp(const char* pkg) {
    if (!pkg) return false;
    std::string p(pkg);
    // 强制放行系统关键认证相关组件
    if (p == "com.android.se" || p == "com.google.android.gms") return true;

    if (targetList.empty()) {
        std::ifstream file("/data/adb/modules/ru.blays.bootloaderspoofer.shadowcpp/target.txt");
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                line.erase(0, line.find_first_not_of(" \t"));
                if (!line.empty() && line[0] != '#') targetList.push_back(line);
            }
            file.close();
        } else {
            return true; // 默认全局拦截（若 target.txt 不存在或为空）
        }
    }
    for (const auto& t : targetList) {
        if (p == t) return true;
    }
    return false;
}

// ---------- 暴力搜索字节流工具 ----------
static int findBytesIndex(const uint8_t* haystack, size_t haystack_len, const uint8_t* needle, size_t needle_len) {
    if (haystack_len < needle_len) return -1;
    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (memcmp(haystack + i, needle, needle_len) == 0) return i;
    }
    return -1;
}

// ---------- 辅助工具：将字节数组转为 Hex 字符串（限制最大打印长度） ----------
static std::string toHex(const uint8_t* buf, size_t len, size_t max_len = 128) {
    const char hex_chars[] = "0123456789ABCDEF";
    std::string str;
    size_t parse_len = (len > max_len) ? max_len : len;
    for (size_t i = 0; i < parse_len; ++i) {
        str.push_back(hex_chars[(buf[i] >> 4) & 0x0F]);
        str.push_back(hex_chars[buf[i] & 0x0F]);
        str.push_back(' ');
    }
    if (len > max_len) str += "...";
    return str;
}

// ---------- 核心解析、Dump 与篡改 (对齐原 Xposed 逻辑) ----------
static bool patchAttestation(uint8_t* data, size_t len) {
    // 日志埋点 1：打印每次进入该函数的原始大小
    LOGI("🔍 [Dump] Entered patchAttestation with data length: %zu", len);

    // 日志埋点 2：打印前 64 个字节的 Hex 头部，直观确认是否为标准 ASN.1 序列
    LOGI("🔍 [Dump] Data Head (First 64B): %s", toHex(data, len, 64).c_str());

    // 验证 Key Attestation 顶层扩展 OID (1.3.6.1.4.1.11129.2.1.17)
    const uint8_t attestation_oid[] = {0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x11};
    int oid_index = findBytesIndex(data, len, attestation_oid, sizeof(attestation_oid));
    
    if (oid_index == -1) {
        return false; 
    }

    // 日志埋点 3：定位到 Key Attestation 数据，打印 OID 后面 128 字节的内容
    LOGI("🎯 [Dump] Found Key Attestation OID at index: %d", oid_index);
    LOGI("🔍 [Dump] Data after OID (128B): %s", toHex(data + oid_index, len - oid_index, 128).c_str());
    
    bool patched = false;
    for (size_t i = 0; i < len - 8; ++i) {
        // 匹配特征：deviceLocked(BOOLEAN) 紧邻 verifiedBootState(ENUMERATED)
        if (data[i] == 0x01 && data[i+1] == 0x01 && data[i+3] == 0x0A && data[i+4] == 0x01) {
            
            LOGI("✨ [Dump] Matched RootOfTrust pattern at index: %zu", i);
            LOGI("✨ [Dump] Before Patch -> deviceLocked: %02X, verifiedBootState: %02X", data[i+2], data[i+5]);
            
            // 1. deviceLocked -> true (0x01)
            if (data[i+2] == 0x00) {
                data[i+2] = 0x01;
                LOGI("🔒 [RootOfTrust] forced deviceLocked -> true");
                patched = true;
            }
            
            // 2. verifiedBootState -> VERIFIED (0x00)
            if (data[i+5] != 0x00) {
                data[i+5] = 0x00;
                LOGI("🛡️ [RootOfTrust] forced verifiedBootState -> VERIFIED");
                patched = true;
            }
            
            if (patched) break;
        }
    }

    if (!patched) {
        LOGI("⚠️ [Dump] Failed to match RootOfTrust pattern in this block.");
    }
    return patched;
}

// ---------- ShadowHook 回调 ----------
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

    if (shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false) != 0) return;

    void* stub = shadowhook_hook_sym_name("libcrypto.so", "X509_get_ext_d2i", (void*)hooked_X509_get_ext_d2i, (void**)&orig_X509_get_ext_d2i);
    if (!stub) {
        stub = shadowhook_hook_sym_name("/apex/com.android.runtime/lib64/bionic/libcrypto.so", "X509_get_ext_d2i", (void*)hooked_X509_get_ext_d2i, (void**)&orig_X509_get_ext_d2i);
    }
    if (stub != nullptr) {
        LOGI("🚀 Native Hook applied successfully into libcrypto.so");
        hooked = true;
    }
}

// ---------- Zygisk 接口包装 ----------
class BootloaderSpoofer : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {}

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) return;
        JNIEnv* env = getSafeJNIEnv();
        if (!env) return;

        const char* process_name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process_name) return;

        bool matched = isTargetApp(process_name);
        env->ReleaseStringUTFChars(args->nice_name, process_name);

        if (matched) doNativeHook();
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        doNativeHook();
    }
};

// 使用向下兼容的单模块注册宏（剔除了导致编译冲突的 Companion 注册宏）
REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

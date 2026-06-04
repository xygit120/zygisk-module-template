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
            return true; // 默认全局拦截
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

// ---------- 核心解析与篡改 (100% 对齐原 Xposed 逻辑) ----------
static bool patchAttestation(uint8_t* data, size_t len) {
    // 1. 验证 Key Attestation 顶层扩展 OID (1.3.6.1.4.1.11129.2.1.17)
    // ASN.1 DER 编码格式: 0x06 (Object Identifier), 0x0B (长度 11)
    const uint8_t attestation_oid[] = {0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x11};
    if (findBytesIndex(data, len, attestation_oid, sizeof(attestation_oid)) == -1) {
        return false; 
    }

    // 2. 定位 RootOfTrust 结构体特征
    // 根据原代码中 ASN1TaggedObject 标签 704 定位 (ASN.1 编码一般以形如 0xBF, 0x85, 0x40 或类似的高位复合标签开头)
    // 我们直接寻找 rootOfTrust 内部特征：verifiedBootState 紧跟在 deviceLocked 后面。
    // deviceLocked 为 ASN1Boolean: 0x01, 0x01, 0x00 (假) 或 0x01, 0x01, 0x01 (真)
    // verifiedBootState 为 ASN1Enumerated: 0x0A, 0x01, 0x01 (解锁/自签名) 
    
    bool patched = false;
    for (size_t i = 0; i < len - 8; ++i) {
        // 匹配未锁定的典型特征组合：
        // data[i] -> 0x01 (BOOLEAN 标签)
        // data[i+1] -> 0x01 (长度 1)
        // data[i+2] -> 0x00 (false, 代表没锁)
        // data[i+3] -> 0x0A (ENUMERATED 标签)
        // data[i+4] -> 0x01 (长度 1)
        // data[i+5] -> 0x01 或 0x02 或 0x03 (代表 UNVERIFIED / SELF_SIGNED)
        if (data[i] == 0x01 && data[i+1] == 0x01 && data[i+3] == 0x0A && data[i+4] == 0x01) {
            
            // 精准对齐原本 Xposed 逻辑中的两行赋值：
            // 1. deviceLocked 设为 1 (true)
            if (data[i+2] == 0x00) {
                data[i+2] = 0x01;
                LOGI("🔒 [RootOfTrust] forced deviceLocked -> true");
                patched = true;
            }
            
            // 2. verifiedBootState 设为 0 (VERIFIED)
            if (data[i+5] != 0x00) {
                data[i+5] = 0x00;
                LOGI("🛡️ [RootOfTrust] forced verifiedBootState -> VERIFIED");
                patched = true;
            }
            
            if (patched) break;
        }
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
class BootloaderSpoofer : public ModuleBase {
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

class BootloaderSpooferCompanion : public CompanionBase {
public:
    void onConnection(int socket) override {}
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)
REGISTER_ZYGISK_COMPANION(BootloaderSpooferCompanion)

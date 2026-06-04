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
    if (p == "com.android.se" || p == "com.google.android.gms" || p == "io.github.vvb2060.keyattestation") return true;

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

// ---------- 核心爆破：直接对 byte[] 内存段执行暴力强改 ----------
static bool patchRawBuffer(uint8_t* data, size_t len) {
    bool patched = false;
    if (len < 6) return false;

    for (size_t i = 0; i < len - 5; ++i) {
        // 100% 对齐 Kotlin 特征扫描：找 deviceLocked (0x01 0x01 XX) 紧邻 verifiedBootState (0x0A 0x01 XX)
        if (data[i] == 0x01 && data[i+1] == 0x01 && data[i+3] == 0x0A && data[i+4] == 0x01) {
            LOGI("🎯 [Native] 捕获到 RootOfTrust 内存特征流，偏移位置: %zu", i);
            LOGI("🔍 [Native] 篡改前 -> deviceLocked: %02X, verifiedBootState: %02X", data[i+2], data[i+5]);

            // 1. deviceLocked -> true (0x01)
            if (data[i+2] == 0x00) {
                data[i+2] = 0x01;
                patched = true;
            }
            // 2. verifiedBootState -> VERIFIED (0x00)
            if (data[i+5] != 0x00) {
                data[i+5] = 0x00;
                patched = true;
            }

            if (patched) {
                LOGI("🎉 [Native] 篡改成功！已强刷为 🔒Locked(01) + 🛡️VERIFIED(00)");
                break;
            }
        }
    }
    return patched;
}

// ---------- 拦截层 1：挂钩原始 X509_get_ext_d2i ----------
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
        patchRawBuffer(res->data, res->length);
    }
    return res;
}

// ---------- 拦截层 2：兜底大网，直接挂钩 BoringSSL 的底层 ASN1_item_d2i ----------
// 无论 Java 层通过什么偏门函数解析任何证书段，最终在 C++ 层反序列化生成 ASN.1 结构时，必过此路
typedef void* (*ASN1_item_d2i_t)(void**, const unsigned char**, long, const void*);
static ASN1_item_d2i_t orig_ASN1_item_d2i = nullptr;

static void* hooked_ASN1_item_d2i(void** val, const unsigned char** in, long len, const void* it) {
    // 因为 in 指针在解析时会被修改，我们先拷贝它的初始地址
    const unsigned char* p_in = *in;
    void* res = orig_ASN1_item_d2i(val, in, len, it);
    
    // 如果解析成功，直接在刚刚读过的原始输入缓冲区里就地扫描并强改
    if (res != nullptr && p_in != nullptr && len > 0) {
        // 由于这里拦截的是全系统所有的 ASN.1 解析，我们需要无条件快速扫描特征码
        patchRawBuffer(const_cast<uint8_t*>(p_in), static_cast<size_t>(len));
    }
    return res;
}

// ---------- 执行多点防御挂钩 ----------
static void doNativeHook() {
    static bool hooked = false;
    if (hooked) return;

    if (shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false) != 0) return;

    // 1. 尝试挂钩顶层封装
    shadowhook_hook_sym_name("libcrypto.so", "X509_get_ext_d2i", (void*)hooked_X509_get_ext_d2i, (void**)&orig_X509_get_ext_d2i);
    
    // 2. 强行挂钩必经之路（兜底大网）
    void* stub = shadowhook_hook_sym_name("libcrypto.so", "ASN1_item_d2i", (void*)hooked_ASN1_item_d2i, (void**)&orig_ASN1_item_d2i);
    if (!stub) {
        stub = shadowhook_hook_sym_name("/apex/com.android.runtime/lib64/bionic/libcrypto.so", "ASN1_item_d2i", (void*)hooked_ASN1_item_d2i, (void**)&orig_ASN1_item_d2i);
    }

    if (stub != nullptr) {
        LOGI("🚀 Native 全局 ASN1 通道双重拦截网络构建成功！");
        hooked = true;
    }
}

// ---------- Zygisk 核心入口 ----------
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

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

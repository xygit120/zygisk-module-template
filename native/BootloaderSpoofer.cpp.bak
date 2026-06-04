#include <jni.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <fstream>
#include <vector>
#include <dlfcn.h>      // 引入动态链接库操作
#include "zygisk.hpp"
#include "shadowhook.h"

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using namespace zygisk;

// ---------- 1. 基础配置与目标 APP 过滤 ----------
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
            return true; // 文件不存在时 Hook 所有
        }
    }
    std::string p(pkg);
    for (const auto& t : targetList) {
        if (p == t) return true;
    }
    return false;
}

// ---------- 2. 核心：Patch 证书扩展的二进制数据 ----------
static bool patchAttestation(uint8_t* data, size_t len) {
    if (len < 200) return false;

    // Key Attestation 的 OID: 1.3.6.1.4.1.11129.2.1.17
    const uint8_t oid[] = {0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x11};

    for (size_t i = 0; i < len - sizeof(oid); ++i) {
        if (memcmp(data + i, oid, sizeof(oid)) == 0) {
            for (size_t j = i + 30; j < len - 8; ++j) {
                // 修改 deviceLocked 为 true (0x01 0x01 0x01)
                if (data[j] == 0x01 && data[j+1] == 0x01 && data[j+2] == 0x00) {
                    data[j+2] = 0x01;
                    LOGI("Patched deviceLocked natively!");
                }
                // 修改 verifiedBootState 为 VERIFIED (0x0A 0x01 0x00)
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

// ---------- 3. ShadowHook 拦截 BoringSSL 函数 ----------

// BoringSSL 的 ASN1_OCTET_STRING 结构体定义
struct ASN1_OCTET_STRING {
    int length;
    int type;
    unsigned char *data;
    long flags;
};

// 原函数指针占位
// X509_get_ext_d2i(X509 *x, int nid, int *crit, int *idx)
typedef ASN1_OCTET_STRING* (*X509_get_ext_d2i_t)(void*, int, int*, int*);
static X509_get_ext_d2i_t orig_X509_get_ext_d2i = nullptr;

// 我们的代理 Hook 函数
static ASN1_OCTET_STRING* hooked_X509_get_ext_d2i(void* x, int nid, int* crit, int* idx) {
    // 调用原函数获取解析结果
    ASN1_OCTET_STRING* res = orig_X509_get_ext_d2i(x, nid, crit, idx);
    
    // NID_keyAttestation 在 Android BoringSSL 中通常是 429 或通过 OID 匹配
    // 为了保险，只要拿到了数据，我们就进密文流里搜索 OID 并修改
    if (res != nullptr && res->data != nullptr && res->length > 0) {
        patchAttestation(res->data, res->length);
    }
    
    return res;
}

// 执行 Native Hook 的函数
static void doNativeHook() {
    // 初始化 ShadowHook
    if (shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false) != 0) {
        LOGI("ShadowHook init failed");
        return;
    }

    // 【避坑关键】Android 7.0+ 隔离了公共 Namespace。
    // 如果直接写 "libcrypto.so"，ShadowHook 可能会因为没有权限加载系统库而失败。
    // 我们需要先拿到已经加载到内存中的核心库句柄（Conscrypt 或者是系统链接器里的句柄）。
    // 在 Zygisk 中，可以尝试通过常规符号名 Hook，如果失败，则指定绝对路径：
    void* hook_stub = shadowhook_hook_sym_name(
        "libcrypto.so", 
        "X509_get_ext_d2i", 
        (void*)hooked_X509_get_ext_d2i, 
        (void**)&orig_X509_get_ext_d2i
    );

    if (hook_stub != nullptr) {
        LOGI("Successfully hooked X509_get_ext_d2i in libcrypto.so");
    } else {
        int err_num = shadowhook_get_errno();
        LOGI("Hook failed, error code: %d", err_num);
        
        // 如果上面失败了，尝试 Hook 具体的私有命名空间映射（ fallback 方案）
        // 很多时候系统会把 libcrypto.so 软链接或缓存在 apex 目录下
        shadowhook_hook_sym_name(
            "/apex/com.android.runtime/lib64/bionic/libcrypto.so", 
            "X509_get_ext_d2i", 
            (void*)hooked_X509_get_ext_d2i, 
            (void**)&orig_X509_get_ext_d2i
        );
    }
}

// ---------- 4. Zygisk 生命周期入口 ----------
class BootloaderSpoofer : public ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        LOGI("BootloaderSpoofer loaded");
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (args == nullptr || args->nice_name == nullptr || args->env == nullptr) return;

        const char* nice_name_chars = args->env->GetStringUTFChars(args->nice_name, nullptr);
        if (nice_name_chars == nullptr) return;

        std::string pkg = nice_name_chars;
        args->env->ReleaseStringUTFChars(args->nice_name, nice_name_chars); // 释放指针

        if (isTargetApp(pkg.c_str())) {
            LOGI("【Target App Detected】: %s", pkg.c_str());
            // 执行 Native 层的 Hook
            doNativeHook();
        }
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

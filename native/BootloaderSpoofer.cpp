#include <jni.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <fstream>
#include <vector>
#include "zygisk.hpp"

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

static Api* g_zygisk_api = nullptr;
static JavaVM* g_vm = nullptr;

// ---------- 全局目标过滤 ----------
static std::vector<std::string> targetList;
static bool isTargetApp(const char* pkg) {
    if (!pkg) return false;
    std::string p(pkg);
    if (p == "com.android.se" || p == "com.google.android.gms" || p == "io.github.vvb2060.keyattestation") return true;

    if (targetList.empty()) {
        std::ifstream file("/data/adb/modules/zygisk-test/target.txt");
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                line.erase(0, line.find_first_not_of(" \t"));
                if (!line.empty() && line[0] != '#') targetList.push_back(line);
            }
            file.close();
        } else {
            return true; 
        }
    }
    for (const auto& t : targetList) {
        if (p == t) return true;
    }
    return false;
}

// ---------- 核心算法：100% 字节强改 ----------
static void patchRawExtensionBytes(jbyte* data, jsize len) {
    if (len < 6) return;
    for (jsize i = 0; i < len - 5; ++i) {
        // 精准狙击 RootOfTrust 特征：deviceLocked (0x01 0x01 XX) 与 verifiedBootState (0x0A 0x01 XX)
        if (data[i] == 0x01 && data[i+1] == 0x01 && data[i+3] == 0x0A && data[i+4] == 0x01) {
            LOGI("🎯 [ZygiskHook] 成功在 Conscrypt 管道中截获 RootOfTrust 特征结构！");
            
            // 1. deviceLocked -> 强刷为 1 (true)
            if (data[i+2] == 0x00) {
                data[i+2] = 0x01;
                LOGI("🔒 [ZygiskHook] 状态强制修正 -> deviceLocked = LOCKED");
            }
            // 2. verifiedBootState -> 强刷为 0 (VERIFIED)
            if (data[i+5] != 0x00) {
                data[i+5] = 0x00;
                LOGI("🛡️ [ZygiskHook] 状态强制修正 -> verifiedBootState = VERIFIED");
            }
            break;
        }
    }
}

// ---------- 备份原始 Conscrypt Native 函数的执行指针 ----------
static jbyteArray (*orig_X509_get_ext_d2i)(JNIEnv*, jclass, jlong, jstring) = nullptr;

// ---------- 我们的 Zygisk 代理拦截函数 ----------
extern "C" jbyteArray My_X509_get_ext_d2i(JNIEnv* env, jclass clazz, jlong x509Ref, jstring oid) {
    // 1. 先调用原始的 NativeCrypto 获取未修改的扩展数据
    jbyteArray res = orig_X509_get_ext_d2i(env, clazz, x509Ref, oid);
    if (res == nullptr) return nullptr;

    // 2. 判定是否为谷歌密钥认证的 OID
    if (oid != nullptr) {
        const char* oid_str = env->GetStringUTFChars(oid, nullptr);
        if (oid_str && strcmp(oid_str, "1.3.6.1.4.1.11129.2.1.17") == 0) {
            jsize len = env->GetArrayLength(res);
            jbyte* p_bytes = env->GetByteArrayElements(res, nullptr);
            if (p_bytes) {
                patchRawExtensionBytes(p_bytes, len);
                env->ReleaseByteArrayElements(res, p_bytes, 0); // 0 表示就地覆盖并同步回 Java 虚拟机内存
            }
        }
        env->ReleaseStringUTFChars(oid, oid_str);
    }
    return res;
}

// ---------- 利用 Zygisk 官方专属 API 进行底层运行时强刷 ----------
static void executeZygiskOfficialHook(JNIEnv* env) {
    if (!g_zygisk_api) return;

    LOGI("🚀 正在激活 Zygisk 核心通道，强行挂钩 Conscrypt 底层通信咽喉...");

    // 描述我们要 Hook 的真正底层原生方法
    JNINativeMethod hook_methods[] = {
        {"X509_get_ext_d2i", "(JLjava/lang/String;)[B", (void*)&My_X509_get_ext_d2i}
    };

    // 针对 Conscrypt 的底层 Native 映射类直接重定向
    g_zygisk_api->hookJniNativeMethods(env, "com/android/org/conscrypt/NativeCrypto", hook_methods, 1);
    
    // Zygisk 执行后会自动在原结构体 fnPtr 中回填系统原始函数的函数指针
    orig_X509_get_ext_d2i = (jbyteArray (*)(JNIEnv*, jclass, jlong, jstring))hook_methods[0].fnPtr;

    LOGI("🎉 [ZygiskHook] 底层 NativeCrypto 管道接管完毕！");
}

// ---------- Zygisk 标准生命周期入口 ----------
class BootloaderSpoofer : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        g_zygisk_api = api;
        // 【核心修复】：在加载时通过正规渠道换取并缓存 JavaVM 指针，彻底丢弃不合规的 args->env
        env->GetJavaVM(&g_vm);
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) return;
        
        // 【核心修复】：通过全局缓存的虚拟机对象安全换取当前隔离线程的 JNIEnv 环境
        JNIEnv* env = nullptr;
        if (g_vm && g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
            g_vm->AttachCurrentThread(&env, nullptr);
        }
        if (!env) return;

        const char* process_name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process_name) return;

        bool matched = isTargetApp(process_name);
        env->ReleaseStringUTFChars(args->nice_name, process_name);

        if (matched) {
            executeZygiskOfficialHook(env);
        }
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

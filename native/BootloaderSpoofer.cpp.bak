#include <jni.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <fstream>
#include <vector>
#include <dlfcn.h>
#include "zygisk.hpp"

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

// ---------- 全局白名单配置 ----------
static std::vector<std::string> targetList;
static bool isTargetApp(const char* pkg) {
    if (!pkg) return false;
    std::string p(pkg);
    if (p == "com.android.se" || p == "com.google.android.gms" || p == "io.github.vvb2060.keyattestation") return true;

    if (targetList.empty()) {
        // 严格对齐模块路径
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

// ---------- 核心算法：100% 对齐原版 Kotlin 字节修改逻辑 ----------
static void patchRawExtensionBytes(jbyte* data, jsize len) {
    if (len < 6) return;
    for (jsize i = 0; i < len - 5; ++i) {
        if (data[i] == 0x01 && data[i+1] == 0x01 && data[i+3] == 0x0A && data[i+4] == 0x01) {
            LOGI("🎯 [JavaHook] 成功在扩展段字节流中定位到 RootOfTrust 结构！位置: %d", i);
            
            if (data[i+2] == 0x00) {
                data[i+2] = 0x01;
                LOGI("🔒 forced deviceLocked -> true");
            }
            if (data[i+5] != 0x00) {
                data[i+5] = 0x00;
                LOGI("🛡️ forced verifiedBootState -> VERIFIED");
            }
            break;
        }
    }
}

// ---------- JNI 篡改代理：接管 X509Certificate.getExtensionValue ----------
static jobject g_orig_getExtensionValue_method = nullptr;

extern "C" JNIEXPORT jbyteArray JNICALL Native_getExtensionValue_Proxy(JNIEnv* env, jobject thiz, jstring oid) {
    jclass method_cls = env->FindClass("java/lang/reflect/Method");
    jmethodID invoke_id = env->GetMethodID(method_cls, "invoke", "(java/lang/Object;[java/lang/Object;)java/lang/Object;");
    
    jobjectArray args = env->NewObjectArray(1, env->FindClass("java/lang/Object"), oid);
    jbyteArray raw_res = (jbyteArray)env->CallObjectMethod(g_orig_getExtensionValue_method, invoke_id, thiz, args);
    
    if (raw_res == nullptr) return nullptr;

    const char* oid_str = env->GetStringUTFChars(oid, nullptr);
    bool is_target_oid = (oid_str && strcmp(oid_str, "1.3.6.1.4.1.11129.2.1.17") == 0);
    env->ReleaseStringUTFChars(oid, oid_str);

    if (is_target_oid) {
        jsize len = env->GetArrayLength(raw_res);
        jbyte* p_bytes = env->GetByteArrayElements(raw_res, nullptr);
        if (p_bytes) {
            patchRawExtensionBytes(p_bytes, len);
            env->ReleaseByteArrayElements(raw_res, p_bytes, 0);
        }
    }
    // 【核心修复】：显式返回获取到且经过篡改的字节流数组，防止编译器报错
    return raw_res;
}

// ---------- 核心注入点：利用 JNI 全局劫持 Java 核心类 ----------
static void injectJavaLayerHook(JNIEnv* env) {
    static bool java_hooked = false;
    if (java_hooked) return;

    LOGI("🚀 Zygisk 正在切入 Java 运行时环境进行全局劫持...");

    jclass x509_cls = env->FindClass("java/security/cert/X509Certificate");
    if (!x509_cls) {
        LOGI("❌ 未能找到 X509Certificate 类");
        return;
    }

    jmethodID target_method_id = env->GetMethodID(x509_cls, "getExtensionValue", "(java/lang/String;)[B");
    if (target_method_id) {
        jobject method_obj = env->ToReflectedMethod(x509_cls, target_method_id, JNI_FALSE);
        g_orig_getExtensionValue_method = env->NewGlobalRef(method_obj);

        // 正确的 Java 类 JNI 签名格式：Ljava/lang/String;
        JNINativeMethod g_methods[] = {
            {"getExtensionValue", "(Ljava/lang/String;)[B", (void*)&Native_getExtensionValue_Proxy}
        };
        
        if (env->RegisterNatives(x509_cls, g_methods, 1) == 0) {
            LOGI("🎉 [JavaHook] 成功接管 X509Certificate.getExtensionValue()！");
            java_hooked = true;
        } else {
            LOGI("❌ 动态注册代理失败");
        }
    }
}

// ---------- Zygisk 接口包装 ----------
class BootloaderSpoofer : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {}

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) return;
        
        JNIEnv* env = args->env; 
        if (!env) return;

        const char* process_name = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process_name) return;

        bool matched = isTargetApp(process_name);
        env->ReleaseStringUTFChars(args->nice_name, process_name);

        if (matched) {
            injectJavaLayerHook(env);
        }
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)

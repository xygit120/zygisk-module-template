#include <jni.h>
#include <android/log.h>
#include <string>
#include <cstring>
#include <fstream>
#include <vector>
#include "zygisk.hpp"
#include "shadowhook.h"

#define LOG_TAG "BootloaderSpoofer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using namespace zygisk;

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
            return true; // 文件不存在时 hook 所有
        }
    }
    std::string p(pkg);
    for (const auto& t : targetList) {
        if (p == t) return true;
    }
    return false;
}

static bool patchAttestation(uint8_t* data, size_t len) {
    if (len < 200) return false;

    const uint8_t oid[] = {0x06, 0x0b, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x11};

    for (size_t i = 0; i < len - sizeof(oid); ++i) {
        if (memcmp(data + i, oid, sizeof(oid)) == 0) {
            for (size_t j = i + 30; j < len - 8; ++j) {
                if (data[j] == 0x01 && data[j+1] == 0x01 && data[j+2] == 0x00) {
                    data[j+2] = 0x01;
                    LOGI("Patched deviceLocked");
                }
                if (data[j] == 0x0A && data[j+1] == 0x01 && data[j+2] == 0x01) {
                    data[j+2] = 0x00;
                    LOGI("Patched verifiedBootState");
                }
            }
            return true;
        }
    }
    return false;
}

static jobject (*orig_getExtensionValue)(JNIEnv*, jobject, jstring) = nullptr;

static jobject hooked_getExtensionValue(JNIEnv* env, jobject thiz, jstring oid) {
    jobject result = orig_getExtensionValue(env, thiz, oid);
    if (result == nullptr || oid == nullptr) return result;

    const char* oidStr = env->GetStringUTFChars(oid, nullptr);
    if (strcmp(oidStr, "1.3.6.1.4.1.11129.2.1.17") == 0) {
        jbyteArray arr = (jbyteArray)result;
        jsize length = env->GetArrayLength(arr);
        jbyte* bytes = env->GetByteArrayElements(arr, nullptr);

        if (patchAttestation((uint8_t*)bytes, length)) {
            jbyteArray newArr = env->NewByteArray(length);
            env->SetByteArrayRegion(newArr, 0, length, bytes);
            env->ReleaseByteArrayElements(arr, bytes, JNI_ABORT);
            env->ReleaseStringUTFChars(oid, oidStr);
            return newArr;
        }
        env->ReleaseByteArrayElements(arr, bytes, JNI_ABORT);
    }
    env->ReleaseStringUTFChars(oid, oidStr);
    return result;
}

class BootloaderSpoofer : public ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        LOGI("BootloaderSpoofer loaded");
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (args == nullptr || args->nice_name == nullptr) return;

        std::string pkg = args->nice_name;
        LOGI("App: %s", pkg.c_str());

        if (isTargetApp(pkg.c_str())) {
            LOGI("【Target】%s", pkg.c_str());

            shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);

            shadowhook_hook_func("java.security.cert.X509Certificate",
                                 "getExtensionValue",
                                 (void*)hooked_getExtensionValue,
                                 (void**)&orig_getExtensionValue,
                                 nullptr);
        }
    }
};

REGISTER_ZYGISK_MODULE(BootloaderSpoofer)
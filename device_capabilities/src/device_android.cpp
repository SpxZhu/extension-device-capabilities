#include <dmsdk/sdk.h>
#if defined(DM_PLATFORM_ANDROID)
#include <dmsdk/dlib/android.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <dlfcn.h>
#include <GLES3/gl3.h>
#include <dmsdk/graphics/graphics.h>
#include <dmsdk/graphics/graphics_vulkan.h>

// A consumer can strip the Vulkan adapter. Its optional entry points must not pull it back in.
namespace dmGraphics {
    VkPhysicalDevice VulkanGetPhysicalDevice(HContext) __attribute__((weak));
    VkInstance VulkanGetInstance(HContext) __attribute__((weak));
}
static jobject g_Device;
static jmethodID g_Info, g_Start, g_Poll, g_Cancel, g_Dialog;
static jmethodID g_SnapshotStart, g_SnapshotPoll, g_SnapshotCancel, g_Close;
static bool Clear(JNIEnv* env)
{
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear(); return true;
}
static int DeviceInfo(lua_State* L)
{
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    int values[4] = {-1, -1, -1, -1};
    if (env && g_Device) {
        jintArray result = (jintArray)env->CallObjectMethod(g_Device, g_Info);
        bool failed = Clear(env);
        if (!failed && result && env->GetArrayLength(result) == 4)
            env->GetIntArrayRegion(result, 0, 4, values);
        Clear(env);
        if (result) env->DeleteLocalRef(result);
    }
    lua_newtable(L);
    const char* names[] = {"vpn", "proxy", "usb_debugging", "developer_mode"};
    for (int i = 0; i < 4; ++i) {
        if (values[i] >= 0) { lua_pushboolean(L, values[i] != 0); lua_setfield(L, -2, names[i]); }
    }
    lua_pushstring(L, "observed"); lua_setfield(L, -2, "status");
    return 1;
}
static void PutString(lua_State* L, const char* name, const char* value)
{
    if (!value || !*value) return;
    lua_pushstring(L, value); lua_setfield(L, -2, name);
}
static void GraphicsInfo(lua_State* L)
{
    dmGraphics::AdapterFamily family = dmGraphics::GetInstalledAdapterFamily();
    if (family == dmGraphics::ADAPTER_FAMILY_OPENGLES || family == dmGraphics::ADAPTER_FAMILY_OPENGL) {
        PutString(L, "gpuName", (const char*)glGetString(GL_RENDERER));
        PutString(L, "graphicsApi", (const char*)glGetString(GL_VERSION));
    } else if (family == dmGraphics::ADAPTER_FAMILY_VULKAN) {
        PutString(L, "graphicsApi", "Vulkan");
        if (!dmGraphics::VulkanGetPhysicalDevice || !dmGraphics::VulkanGetInstance) return;
        void* library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) return;
        PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
        dmGraphics::HContext context = dmGraphics::GetInstalledContext();
        if (get && context) {
            PFN_vkGetPhysicalDeviceProperties properties = (PFN_vkGetPhysicalDeviceProperties)
                get(dmGraphics::VulkanGetInstance(context), "vkGetPhysicalDeviceProperties");
            VkPhysicalDevice device = dmGraphics::VulkanGetPhysicalDevice(context);
            if (properties && device) {
                VkPhysicalDeviceProperties info;
                properties(device, &info);
                PutString(L, "gpuName", info.deviceName);
                char version[64];
                snprintf(version, sizeof(version), "Vulkan %u.%u.%u", VK_VERSION_MAJOR(info.apiVersion),
                    VK_VERSION_MINOR(info.apiVersion), VK_VERSION_PATCH(info.apiVersion));
                PutString(L, "graphicsApi", version);
            }
        }
        dlclose(library);
    }
#if defined(__aarch64__)
    PutString(L, "cpuArch", "arm64-v8a");
#elif defined(__arm__)
    PutString(L, "cpuArch", "armeabi-v7a");
#elif defined(__x86_64__)
    PutString(L, "cpuArch", "x86_64");
#elif defined(__i386__)
    PutString(L, "cpuArch", "x86");
#endif
}
static int SnapshotStart(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    if (id <= 0) return luaL_error(L, "snapshot_start requires a positive id");
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    bool accepted = false;
    if (env && g_Device) {
        accepted = env->CallBooleanMethod(g_Device, g_SnapshotStart, id);
        if (Clear(env)) accepted = false;
    }
    lua_pushboolean(L, accepted); return 1;
}
static int SnapshotPoll(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    jobjectArray result = 0;
    if (env && g_Device) {
        result = (jobjectArray)env->CallObjectMethod(g_Device, g_SnapshotPoll, id);
        if (Clear(env)) result = 0;
    }
    int length = result ? env->GetArrayLength(result) : 0;
    lua_pushstring(L, "unavailable");
    if (length > 0) {
        jstring state = (jstring)env->GetObjectArrayElement(result, 0);
        const char* status = state ? env->GetStringUTFChars(state, 0) : 0;
        if (status) { lua_pop(L, 1); lua_pushstring(L, status); env->ReleaseStringUTFChars(state, status); }
        if (state) env->DeleteLocalRef(state);
    }
    lua_newtable(L);
    for (int index = 1; index + 1 < length; index += 2) {
        jstring key = (jstring)env->GetObjectArrayElement(result, index);
        jstring item = (jstring)env->GetObjectArrayElement(result, index + 1);
        const char* name = key ? env->GetStringUTFChars(key, 0) : 0;
        const char* value = item ? env->GetStringUTFChars(item, 0) : 0;
        if (name && value && *value) {
            if (*value == 'n') {
                char* end = 0;
                double number = strtod(value + 1, &end);
                if (end != value + 1 && !*end && isfinite(number)) {
                    lua_pushnumber(L, number); lua_setfield(L, -2, name);
                }
            } else if (*value == 's' || *value == 'i') PutString(L, name, value + 1);
        }
        if (name) env->ReleaseStringUTFChars(key, name);
        if (value) env->ReleaseStringUTFChars(item, value);
        if (key) env->DeleteLocalRef(key);
        if (item) env->DeleteLocalRef(item);
    }
    if (env) Clear(env);
    if (result) env->DeleteLocalRef(result);
    GraphicsInfo(L);
    return 2;
}
static int SnapshotCancel(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (env && g_Device) { env->CallVoidMethod(g_Device, g_SnapshotCancel, id); Clear(env); }
    return 0;
}
static int Start(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    const char* project = luaL_checkstring(L, 2);
    size_t length = 0;
    const char* data = luaL_checklstring(L, 3, &length);
    if (id <= 0 || length == 0 || length > 4096 || memchr(data, 0, length))
        return luaL_error(L, "integrity_start requires positive id and 1..4096 bytes of UTF-8 request data");
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    bool accepted = env && g_Device;
    if (accepted) {
        jstring number = env->NewStringUTF(project), input = env->NewStringUTF(data);
        if (!Clear(env) && number && input) {
            env->CallVoidMethod(g_Device, g_Start, id, number, input);
            accepted = !Clear(env);
        } else accepted = false;
        if (number) env->DeleteLocalRef(number);
        if (input) env->DeleteLocalRef(input);
    }
    lua_pushboolean(L, accepted); return 1;
}
static int Poll(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (env && g_Device) {
        jobjectArray result = (jobjectArray)env->CallObjectMethod(g_Device, g_Poll, id);
        bool failed = Clear(env);
        if (!failed && result && env->GetArrayLength(result) == 3) {
            for (int i = 0; i < 3; ++i) {
                jstring item = (jstring)env->GetObjectArrayElement(result, i);
                const char* value = item ? env->GetStringUTFChars(item, 0) : 0;
                lua_pushstring(L, value ? value : "");
                if (value) env->ReleaseStringUTFChars(item, value);
                if (item) env->DeleteLocalRef(item);
            }
            Clear(env);
            env->DeleteLocalRef(result);
            return 3;
        }
        if (result) env->DeleteLocalRef(result);
    }
    lua_pushstring(L, "unavailable"); return 1;
}
static int Cancel(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (env && g_Device) { env->CallVoidMethod(g_Device, g_Cancel, id); Clear(env); }
    return 0;
}
static int ShowDialog(lua_State* L)
{
    int id = luaL_checkint(L, 1), type = luaL_checkint(L, 2);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    bool accepted = env && g_Device;
    if (accepted) { env->CallVoidMethod(g_Device, g_Dialog, id, type); accepted = !Clear(env); }
    lua_pushboolean(L, accepted); return 1;
}
dmExtension::Result DeviceAndroidInitialize(dmExtension::Params* params)
{
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (!env) return dmExtension::RESULT_OK;
    jclass cls = dmAndroid::LoadClass(env, "com.defold.devicecapabilities.DeviceBridge");
    if (Clear(env) || !cls) return dmExtension::RESULT_OK;
    jmethodID constructor = env->GetMethodID(cls, "<init>", "(Landroid/app/Activity;)V");
    g_Info = env->GetMethodID(cls, "deviceInfo", "()[I");
    g_Start = env->GetMethodID(cls, "start", "(ILjava/lang/String;Ljava/lang/String;)V");
    g_Poll = env->GetMethodID(cls, "poll", "(I)[Ljava/lang/String;");
    g_Cancel = env->GetMethodID(cls, "cancel", "(I)V");
    g_Dialog = env->GetMethodID(cls, "showDialog", "(II)V");
    g_SnapshotStart = env->GetMethodID(cls, "snapshotStart", "(I)Z");
    g_SnapshotPoll = env->GetMethodID(cls, "snapshotPoll", "(I)[Ljava/lang/String;");
    g_SnapshotCancel = env->GetMethodID(cls, "snapshotCancel", "(I)V");
    g_Close = env->GetMethodID(cls, "close", "()V");
    bool failed = Clear(env);
    if (!failed && constructor && g_Info && g_Start && g_Poll && g_Cancel && g_Dialog
        && g_SnapshotStart && g_SnapshotPoll && g_SnapshotCancel && g_Close) {
        jobject object = env->NewObject(cls, constructor, thread.GetActivity()->clazz);
        if (!Clear(env) && object) g_Device = env->NewGlobalRef(object);
        if (object) env->DeleteLocalRef(object);
    }
    env->DeleteLocalRef(cls);
    if (g_Device) {
        const luaL_reg methods[] = {{"device_info", DeviceInfo}, {"integrity_start", Start},
            {"snapshot_start", SnapshotStart}, {"snapshot_poll", SnapshotPoll}, {"snapshot_cancel", SnapshotCancel},
            {"integrity_poll", Poll}, {"integrity_cancel", Cancel}, {"integrity_show_dialog", ShowDialog}, {0, 0}};
        luaL_register(params->m_L, "device_capabilities", methods); lua_pop(params->m_L, 1);
    }
    return dmExtension::RESULT_OK;
}
dmExtension::Result DeviceAndroidFinalize(dmExtension::Params* params)
{
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (env && g_Device) {
        env->CallVoidMethod(g_Device, g_Close); Clear(env);
        env->DeleteGlobalRef(g_Device);
    }
    g_Device = 0; return dmExtension::RESULT_OK;
}
#endif

#include <dmsdk/sdk.h>
#if defined(DM_PLATFORM_ANDROID)
#include <dmsdk/dlib/android.h>
#include <string.h>
static jobject g_Device;
static jmethodID g_Info, g_Start, g_Poll, g_Cancel, g_Dialog;
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
    bool failed = Clear(env);
    if (!failed && constructor && g_Info && g_Start && g_Poll && g_Cancel && g_Dialog) {
        jobject object = env->NewObject(cls, constructor, thread.GetActivity()->clazz);
        if (!Clear(env) && object) g_Device = env->NewGlobalRef(object);
        if (object) env->DeleteLocalRef(object);
    }
    env->DeleteLocalRef(cls);
    if (g_Device) {
        const luaL_reg methods[] = {{"device_info", DeviceInfo}, {"integrity_start", Start},
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
        env->CallVoidMethod(g_Device, g_Cancel, 0); Clear(env);
        env->DeleteGlobalRef(g_Device);
    }
    g_Device = 0; return dmExtension::RESULT_OK;
}
#endif

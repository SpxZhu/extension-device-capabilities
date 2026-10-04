#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_ANDROID)
#include <dmsdk/dlib/android.h>

static jobject g_Location;
static jmethodID g_Start, g_Poll, g_Cancel;

static bool ClearException(JNIEnv* env)
{
    if (!env->ExceptionCheck()) return false;
    // Do not print platform exception details (may include location information).
    env->ExceptionClear();
    return true;
}

static int Start(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    bool ok = env && g_Location;
    if (ok) { env->CallVoidMethod(g_Location, g_Start, id); ok = !ClearException(env); }
    lua_pushboolean(L, ok);
    return 1;
}

static int Poll(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    double result[3] = {6, 0, 0};
    if (env && g_Location)
    {
        jdoubleArray values = (jdoubleArray)env->CallObjectMethod(g_Location, g_Poll, id);
        bool failed = ClearException(env);
        if (!failed && values && env->GetArrayLength(values) == 3)
            env->GetDoubleArrayRegion(values, 0, 3, result);
        if (ClearException(env)) result[0] = 5;
        if (values) env->DeleteLocalRef(values);
    }
    const char* statuses[] = {"permission", "locating", "success", "permission_denied",
        "disabled", "failed", "unavailable", "timeout"};
    int status = (int)result[0];
    lua_pushstring(L, status >= 0 && status <= 7 ? statuses[status] : "failed");
    if (status != 2) return 1;
    lua_pushnumber(L, result[1]); lua_pushnumber(L, result[2]);
    return 3;
}

static int Cancel(lua_State* L)
{
    int id = luaL_checkint(L, 1);
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (env && g_Location) { env->CallVoidMethod(g_Location, g_Cancel, id); ClearException(env); }
    return 0;
}

dmExtension::Result DeviceLocationInitialize(dmExtension::Params* params)
{
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (!env) return dmExtension::RESULT_OK;
    jclass cls = dmAndroid::LoadClass(env, "com.defold.devicecapabilities.LocationBridge");
    if (ClearException(env) || !cls) return dmExtension::RESULT_OK;
    jmethodID constructor = env->GetMethodID(cls, "<init>", "(Landroid/app/Activity;)V");
    g_Start = env->GetMethodID(cls, "start", "(I)V");
    g_Poll = env->GetMethodID(cls, "poll", "(I)[D");
    g_Cancel = env->GetMethodID(cls, "cancel", "(I)V");
    bool failed = ClearException(env);
    if (!failed && constructor && g_Start && g_Poll && g_Cancel)
    {
        jobject object = env->NewObject(cls, constructor, thread.GetActivity()->clazz);
        if (!ClearException(env) && object) g_Location = env->NewGlobalRef(object);
        if (object) env->DeleteLocalRef(object);
    }
    env->DeleteLocalRef(cls);
    if (g_Location)
    {
        const luaL_reg methods[] = {{"location_start", Start}, {"location_poll", Poll}, {"location_cancel", Cancel}, {0, 0}};
        luaL_register(params->m_L, "device_capabilities", methods); lua_pop(params->m_L, 1);
    }
    return dmExtension::RESULT_OK;
}

dmExtension::Result DeviceLocationFinalize(dmExtension::Params* params)
{
    dmAndroid::ThreadAttacher thread;
    JNIEnv* env = thread.GetEnv();
    if (env && g_Location)
    {
        env->CallVoidMethod(g_Location, g_Cancel, 0); ClearException(env);
        env->DeleteGlobalRef(g_Location);
    }
    g_Location = 0;
    return dmExtension::RESULT_OK;
}

#endif

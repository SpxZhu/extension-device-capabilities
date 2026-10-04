#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_ANDROID) || defined(DM_PLATFORM_IOS)
dmExtension::Result DeviceLocationInitialize(dmExtension::Params*);
dmExtension::Result DeviceLocationFinalize(dmExtension::Params*);
#endif
#if defined(DM_PLATFORM_ANDROID)
dmExtension::Result DeviceAndroidInitialize(dmExtension::Params*);
dmExtension::Result DeviceAndroidFinalize(dmExtension::Params*);
#endif
#if defined(DM_PLATFORM_IOS)
dmExtension::Result DeviceIOSInitialize(dmExtension::Params*);
#endif

static int DeviceInfo(lua_State* L)
{
    lua_newtable(L);
    lua_pushstring(L, "unsupported"); lua_setfield(L, -2, "status");
    return 1;
}
static int Unavailable(lua_State* L)
{
    lua_pushnil(L); lua_pushstring(L, "unavailable"); return 2;
}
static int PollUnavailable(lua_State* L)
{
    lua_pushstring(L, "unavailable"); return 1;
}
static int Cancel(lua_State* L) { return 0; }

static dmExtension::Result Initialize(dmExtension::Params* params)
{
    const luaL_reg methods[] = {
        {"device_info", DeviceInfo},
        {"location_start", Unavailable}, {"location_poll", PollUnavailable}, {"location_cancel", Cancel},
        {"integrity_start", Unavailable}, {"integrity_poll", PollUnavailable},
        {"integrity_cancel", Cancel}, {"integrity_show_dialog", Unavailable}, {0, 0}};
    luaL_register(params->m_L, "device_capabilities", methods); lua_pop(params->m_L, 1);
#if defined(DM_PLATFORM_ANDROID)
    DeviceAndroidInitialize(params);
#endif
#if defined(DM_PLATFORM_IOS)
    DeviceIOSInitialize(params);
#endif
#if defined(DM_PLATFORM_ANDROID) || defined(DM_PLATFORM_IOS)
    return DeviceLocationInitialize(params);
#else
    return dmExtension::RESULT_OK;
#endif
}
static dmExtension::Result Finalize(dmExtension::Params* params)
{
#if defined(DM_PLATFORM_ANDROID)
    DeviceAndroidFinalize(params);
#endif
#if defined(DM_PLATFORM_ANDROID) || defined(DM_PLATFORM_IOS)
    return DeviceLocationFinalize(params);
#else
    return dmExtension::RESULT_OK;
#endif
}
DM_DECLARE_EXTENSION(DeviceCapabilities, "DeviceCapabilities", 0, 0, Initialize, 0, 0, Finalize)

#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_ANDROID)
dmExtension::Result DeviceAndroidInitialize(dmExtension::Params*);
dmExtension::Result DeviceAndroidFinalize(dmExtension::Params*);
#endif
#if defined(DM_PLATFORM_IOS)
dmExtension::Result DeviceIOSInitialize(dmExtension::Params*);
void DeviceSnapshotIOSFinalize();
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
static int SnapshotStart(lua_State* L) { lua_pushboolean(L, false); return 1; }
static int SnapshotPoll(lua_State* L)
{
    lua_pushstring(L, "unsupported"); lua_newtable(L); return 2;
}

static dmExtension::Result Initialize(dmExtension::Params* params)
{
    const luaL_reg methods[] = {
        {"device_info", DeviceInfo},
        {"snapshot_start", SnapshotStart}, {"snapshot_poll", SnapshotPoll}, {"snapshot_cancel", Cancel},
        {"integrity_start", Unavailable}, {"integrity_poll", PollUnavailable},
        {"integrity_cancel", Cancel}, {"integrity_show_dialog", Unavailable}, {0, 0}};
    luaL_register(params->m_L, "device_capabilities", methods); lua_pop(params->m_L, 1);
#if defined(DM_PLATFORM_ANDROID)
    DeviceAndroidInitialize(params);
#endif
#if defined(DM_PLATFORM_IOS)
    DeviceIOSInitialize(params);
#endif
    return dmExtension::RESULT_OK;
}
static dmExtension::Result Finalize(dmExtension::Params* params)
{
#if defined(DM_PLATFORM_ANDROID)
    DeviceAndroidFinalize(params);
#endif
#if defined(DM_PLATFORM_IOS)
    DeviceSnapshotIOSFinalize();
#endif
    return dmExtension::RESULT_OK;
}
DM_DECLARE_EXTENSION(DeviceCapabilities, "DeviceCapabilities", 0, 0, Initialize, 0, 0, Finalize)

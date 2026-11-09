#include <dmsdk/sdk.h>
#if defined(DM_PLATFORM_IOS)
#import <Foundation/Foundation.h>
#import <CFNetwork/CFNetwork.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <string.h>
void DeviceSnapshotIOSInitialize(lua_State*);

static int DeviceInfo(lua_State* L)
{
    lua_newtable(L);
    struct ifaddrs* interfaces = 0;
    if (getifaddrs(&interfaces) == 0) {
        bool vpn = false;
        for (struct ifaddrs* item = interfaces; item; item = item->ifa_next) {
            if (!item->ifa_name || !(item->ifa_flags & IFF_UP)) continue;
            const char* name = item->ifa_name;
            if (strncmp(name, "utun", 4) == 0 || strncmp(name, "ppp", 3) == 0
                || strncmp(name, "ipsec", 5) == 0 || strncmp(name, "tun", 3) == 0) vpn = true;
        }
        freeifaddrs(interfaces);
        lua_pushboolean(L, vpn); lua_setfield(L, -2, "vpn");
    }
    CFDictionaryRef settings = CFNetworkCopySystemProxySettings();
    if (settings) {
        NSDictionary* dictionary = (NSDictionary*)settings;
        bool proxy = [[dictionary objectForKey:(NSString*)kCFNetworkProxiesHTTPEnable] boolValue]
            || [[dictionary objectForKey:(NSString*)kCFNetworkProxiesProxyAutoConfigEnable] boolValue];
        lua_pushboolean(L, proxy); lua_setfield(L, -2, "proxy");
        CFRelease(settings);
    }
    // iOS has no public equivalent for Android developer/USB debugging settings.
    lua_pushstring(L, "observed"); lua_setfield(L, -2, "status");
    return 1;
}
dmExtension::Result DeviceIOSInitialize(dmExtension::Params* params)
{
    const luaL_reg methods[] = {{"device_info", DeviceInfo}, {0, 0}};
    luaL_register(params->m_L, "device_capabilities", methods); lua_pop(params->m_L, 1);
    DeviceSnapshotIOSInitialize(params->m_L);
    return dmExtension::RESULT_OK;
}
#endif

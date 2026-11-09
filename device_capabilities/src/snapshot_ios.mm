#include <dmsdk/sdk.h>
#if defined(DM_PLATFORM_IOS)
#include <dmsdk/graphics/graphics.h>
#include <dmsdk/graphics/graphics_native.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <Network/Network.h>
#import <CoreMotion/CoreMotion.h>
#import <AVFoundation/AVFoundation.h>
#import <AdSupport/AdSupport.h>
#import <AppTrackingTransparency/AppTrackingTransparency.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <WebKit/WebKit.h>
#import <OpenGLES/ES3/gl.h>
#include <TargetConditionals.h>
#include <mach-o/dyld.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <math.h>
#include <mutex>
#include <atomic>

static std::mutex g_SnapshotMutex;
static NSMutableDictionary* g_Snapshot;
static NSDictionary* g_Hardware;
static NSDictionary* g_Network;
static NSString* g_UserAgent;
static WKWebView* g_WebView;
static nw_path_monitor_t g_Monitor;
static int g_RequestId;
static unsigned int g_Generation;
static bool g_Ready;
static std::atomic<bool> g_Stopped(false);
static unsigned int g_NetworkGeneration;

static NSString* AddressForInterface(const char* name)
{
    struct ifaddrs* interfaces = 0;
    if (!name || getifaddrs(&interfaces) != 0) return nil;
    NSString* ipv4 = nil;
    NSString* ipv6 = nil;
    for (struct ifaddrs* item = interfaces; item; item = item->ifa_next) {
        if (!item->ifa_addr || strcmp(name, item->ifa_name) != 0 || !(item->ifa_flags & IFF_UP)
            || (item->ifa_flags & IFF_LOOPBACK)) continue;
        int family = item->ifa_addr->sa_family;
        char buffer[INET6_ADDRSTRLEN];
        if (family == AF_INET) {
            struct sockaddr_in* address = (struct sockaddr_in*)item->ifa_addr;
            if (inet_ntop(AF_INET, &address->sin_addr, buffer, sizeof(buffer))) ipv4 = [NSString stringWithUTF8String:buffer];
        } else if (family == AF_INET6) {
            struct sockaddr_in6* address = (struct sockaddr_in6*)item->ifa_addr;
            if (IN6_IS_ADDR_LINKLOCAL(&address->sin6_addr) || IN6_IS_ADDR_LOOPBACK(&address->sin6_addr)) continue;
            if (inet_ntop(AF_INET6, &address->sin6_addr, buffer, sizeof(buffer))) ipv6 = [NSString stringWithUTF8String:buffer];
        }
    }
    freeifaddrs(interfaces);
    return ipv4 ?: ipv6;
}

static void MonitorNetwork()
{
    if (g_Monitor || g_Stopped) return;
    g_Monitor = nw_path_monitor_create();
    unsigned int generation = ++g_NetworkGeneration;
    nw_path_monitor_set_queue(g_Monitor, dispatch_get_main_queue());
    nw_path_monitor_set_update_handler(g_Monitor, ^(nw_path_t path) {
        if (g_Stopped || generation != g_NetworkGeneration) return;
        NSMutableDictionary* values = [NSMutableDictionary dictionary];
        bool available = nw_path_get_status(path) == nw_path_status_satisfied;
        values[@"isNetworkAvailable"] = @(available);
        values[@"isNetworkExpensive"] = @(nw_path_is_expensive(path));
        if (@available(iOS 13.0, *)) values[@"isLowDataMode"] = @(nw_path_is_constrained(path));
        if (!available) values[@"netwarkType"] = @"offline";
        else if (nw_path_uses_interface_type(path, nw_interface_type_wifi)) values[@"netwarkType"] = @"wifi";
        else if (nw_path_uses_interface_type(path, nw_interface_type_cellular)) values[@"netwarkType"] = @"cellular";
        else if (nw_path_uses_interface_type(path, nw_interface_type_wired)) values[@"netwarkType"] = @"ethernet";
        // Only an interface actually used by this path can supply its local address.
        nw_path_enumerate_interfaces(path, ^bool(nw_interface_t interface) {
            nw_interface_type_t type = nw_interface_get_type(interface);
            if (!available || !nw_path_uses_interface_type(path, type)) return true;
            NSString* address = AddressForInterface(nw_interface_get_name(interface));
            if (address && !values[@"localIp"]) {
                values[@"localIp"] = address;
                values[@"networkInterface"] = [NSString stringWithUTF8String:nw_interface_get_name(interface)];
            }
            return true;
        });
        [g_Network release];
        g_Network = [values copy];
        std::lock_guard<std::mutex> guard(g_SnapshotMutex);
        if (!g_Stopped && g_Snapshot) {
            [g_Snapshot addEntriesFromDictionary:values];
            g_Ready = g_UserAgent != nil;
        }
    });
    nw_path_monitor_start(g_Monitor);
}

static void GraphicsInfo(NSMutableDictionary* values)
{
    dmGraphics::AdapterFamily family = dmGraphics::GetInstalledAdapterFamily();
    if (family == dmGraphics::ADAPTER_FAMILY_METAL) values[@"graphicsApi"] = @"Metal";
    else if (family == dmGraphics::ADAPTER_FAMILY_VULKAN) values[@"graphicsApi"] = @"Vulkan";
    UIView* view = (UIView*)dmGraphics::GetNativeiOSUIView();
    id<MTLDevice> device = [view.layer isKindOfClass:[CAMetalLayer class]] ? ((CAMetalLayer*)view.layer).device : nil;
    // iOS has one system GPU. Metal capabilities also exist when the app renders using GLES.
    id<MTLDevice> fallback = nil;
    if (!device) { fallback = MTLCreateSystemDefaultDevice(); device = fallback; }
    if (!device) return;
    if (device.name) values[@"gpuName"] = device.name;
    if (@available(iOS 13.0, *)) {
        NSMutableArray* families = [NSMutableArray array];
        // SDK-known enumerators, with runtime availability checked independently.
        for (int index = 1; index <= 9; ++index) {
            if (index == 7) { if (@available(iOS 14.0, *)) { } else continue; }
            if (index >= 8) { if (@available(iOS 16.0, *)) { } else continue; }
            if (index == 9) { if (@available(iOS 17.0, *)) { } else continue; }
            if ([device supportsFamily:(MTLGPUFamily)(1000 + index)])
                [families addObject:[NSString stringWithFormat:@"apple%d", index]];
        }
        for (int index = 1; index <= 3; ++index)
            if ([device supportsFamily:(MTLGPUFamily)(3000 + index)])
                [families addObject:[NSString stringWithFormat:@"common%d", index]];
        if (@available(iOS 16.0, *)) {
#if defined(__IPHONE_16_0)
            if ([device supportsFamily:MTLGPUFamilyMetal3]) [families addObject:@"metal3"];
#endif
        }
#if defined(__IPHONE_26_0)
        if (@available(iOS 26.0, *)) {
            if ([device supportsFamily:MTLGPUFamilyApple10]) [families addObject:@"apple10"];
            if ([device supportsFamily:MTLGPUFamilyMetal4]) [families addObject:@"metal4"];
        }
#endif
        values[@"gpuFamilies"] = families;
    }
    [fallback release];
}

static NSDictionary* Hardware()
{
    if (g_Hardware) return g_Hardware;
    NSMutableDictionary* values = [NSMutableDictionary dictionary];
    NSProcessInfo* process = [NSProcessInfo processInfo];
    values[@"cpuCores"] = [NSString stringWithFormat:@"%lu", (unsigned long)process.processorCount];
    values[@"totalMemory"] = [NSString stringWithFormat:@"%llu", (unsigned long long)process.physicalMemory];
#if defined(__arm64__)
    values[@"cpuArch"] = @"arm64";
#elif defined(__x86_64__)
    values[@"cpuArch"] = @"x86_64";
#endif
    NSString* package = [NSBundle mainBundle].bundleIdentifier;
    if (package) values[@"packageName"] = package;
    CMMotionManager* motion = [[CMMotionManager alloc] init];
    values[@"hasAccelerometer"] = @(motion.accelerometerAvailable);
    values[@"hasGyroscope"] = @(motion.gyroAvailable);
    values[@"hasMagnetometer"] = @(motion.magnetometerAvailable);
    [motion release];
    values[@"isEmulator"] = @(TARGET_OS_SIMULATOR != 0);
    GraphicsInfo(values);
    g_Hardware = [values copy];
    return g_Hardware;
}

static void SecurityInfo(NSMutableDictionary* values)
{
    bool jailbreak = false;
#if !TARGET_OS_SIMULATOR
    NSArray* paths = @[@"/Applications/Cydia.app", @"/Applications/Sileo.app", @"/Library/MobileSubstrate/MobileSubstrate.dylib",
        @"/usr/sbin/sshd", @"/bin/bash", @"/var/jb/usr/bin/su"];
    for (NSString* path in paths) jailbreak |= [[NSFileManager defaultManager] fileExistsAtPath:path];
#endif
    values[@"isRootOrJailbreak"] = @(jailbreak);
    bool hook = false;
    for (uint32_t index = 0; index < _dyld_image_count(); ++index) {
        const char* path = _dyld_get_image_name(index);
        NSString* name = path ? [[NSString stringWithUTF8String:path] lowercaseString] : nil;
        hook |= name && ([name containsString:@"frida"] || [name containsString:@"substrate"]
            || [name containsString:@"substitute"] || [name containsString:@"libhooker"]);
    }
    values[@"hasHookFramework"] = @(hook);
}

static void Collect(int request_id, unsigned int generation)
{
    @autoreleasepool {
        {
            std::lock_guard<std::mutex> guard(g_SnapshotMutex);
            if (g_Stopped || request_id != g_RequestId || generation != g_Generation) return;
        }
        MonitorNetwork();
        NSMutableDictionary* values = [NSMutableDictionary dictionaryWithDictionary:Hardware()];
        values[@"timezone"] = [NSTimeZone localTimeZone].name;
        NSProcessInfo* process = [NSProcessInfo processInfo];
        values[@"lowPowerMode"] = @(process.lowPowerModeEnabled);
        if (@available(iOS 11.0, *)) values[@"thermalState"] = @(process.thermalState);
        UIDevice* device = [UIDevice currentDevice];
        device.batteryMonitoringEnabled = YES;
        float battery = device.batteryLevel;
        if (battery >= 0) values[@"battery"] = [NSString stringWithFormat:@"%.0f", roundf(battery * 100)];
        int status = 0;
        switch (device.batteryState) {
            case UIDeviceBatteryStateCharging: status = 1; break;
            case UIDeviceBatteryStateUnplugged: status = 2; break;
            case UIDeviceBatteryStateFull: status = 4; break;
            default: break;
        }
        values[@"batteryStatus"] = [NSString stringWithFormat:@"%d", status];
        values[@"screenScale"] = @([UIScreen mainScreen].scale);
        values[@"maxRefreshRateHz"] = @([UIScreen mainScreen].maximumFramesPerSecond);
        // IDFV comes from Defold sys_info at the SDK layer. This standalone API also exposes it.
        if (device.identifierForVendor) values[@"idfv"] = device.identifierForVendor.UUIDString;
        bool authorized = false;
        if (@available(iOS 14.0, *)) authorized = [ATTrackingManager trackingAuthorizationStatus] == ATTrackingManagerAuthorizationStatusAuthorized;
        else authorized = [ASIdentifierManager sharedManager].advertisingTrackingEnabled;
        if (authorized) values[@"idfa"] = [ASIdentifierManager sharedManager].advertisingIdentifier.UUIDString;
        bool headset = false;
        for (AVAudioSessionPortDescription* port in [AVAudioSession sharedInstance].currentRoute.outputs) {
            headset |= [port.portType isEqualToString:AVAudioSessionPortHeadphones]
                || [port.portType isEqualToString:AVAudioSessionPortBluetoothA2DP]
                || [port.portType isEqualToString:AVAudioSessionPortBluetoothHFP]
                || [port.portType isEqualToString:AVAudioSessionPortBluetoothLE];
        }
        values[@"isHeadsetPlugged"] = @(headset);
        if (g_Network) [values addEntriesFromDictionary:g_Network];
        SecurityInfo(values);
        if (g_UserAgent) values[@"userAgent"] = g_UserAgent;
        {
            std::lock_guard<std::mutex> guard(g_SnapshotMutex);
            if (g_Stopped || request_id != g_RequestId || generation != g_Generation) return;
            [g_Snapshot release];
            g_Snapshot = [values mutableCopy];
            g_Ready = g_UserAgent != nil && g_Network != nil;
        }
        if (g_UserAgent) return;
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        if (g_WebView) { [configuration release]; return; }
        g_WebView = [[WKWebView alloc] initWithFrame:CGRectZero configuration:configuration];
        [configuration release];
        WKWebView* requested_view = g_WebView;
        [requested_view evaluateJavaScript:@"navigator.userAgent" completionHandler:^(id value, NSError* error) {
            if (g_WebView != requested_view) return;
            std::lock_guard<std::mutex> guard(g_SnapshotMutex);
            if (!g_Stopped && !error && [value isKindOfClass:[NSString class]] && [value length]) {
                [g_UserAgent release];
                g_UserAgent = [value copy];
            }
            if (!g_Stopped && g_RequestId != 0 && g_Snapshot) {
                if (g_UserAgent) g_Snapshot[@"userAgent"] = g_UserAgent;
                g_Ready = g_Network != nil;
            }
            [g_WebView release]; g_WebView = nil;
        }];
    }
}

static int SnapshotStart(lua_State* L)
{
    int request_id = luaL_checkint(L, 1);
    if (request_id <= 0) return luaL_error(L, "snapshot_start requires a positive id");
    unsigned int generation;
    {
        std::lock_guard<std::mutex> guard(g_SnapshotMutex);
        g_RequestId = request_id;
        generation = ++g_Generation;
        [g_Snapshot release]; g_Snapshot = nil;
        g_Ready = false;
    }
    dispatch_async(dispatch_get_main_queue(), ^{ Collect(request_id, generation); });
    lua_pushboolean(L, true); return 1;
}

static int SnapshotPoll(lua_State* L)
{
    int request_id = luaL_checkint(L, 1);
    @autoreleasepool {
        NSDictionary* snapshot;
        const char* status;
        {
            std::lock_guard<std::mutex> guard(g_SnapshotMutex);
            bool owns = !g_Stopped && request_id == g_RequestId;
            snapshot = owns ? [g_Snapshot copy] : nil;
            status = !owns ? "unavailable" : g_Ready ? "ready" : "pending";
        }
        lua_pushstring(L, status);
        lua_newtable(L);
        for (NSString* key in snapshot) {
            id value = snapshot[key];
            if ([value isKindOfClass:[NSString class]]) lua_pushstring(L, [value UTF8String]);
            else if ([value isKindOfClass:[NSNumber class]]) lua_pushnumber(L, [value doubleValue]);
            else if ([value isKindOfClass:[NSArray class]]) {
                lua_newtable(L);
                int index = 1;
                for (NSString* item in value) {
                    lua_pushstring(L, item.UTF8String); lua_rawseti(L, -2, index++);
                }
            } else continue;
            lua_setfield(L, -2, key.UTF8String);
        }
        [snapshot release];
    }
    dmGraphics::AdapterFamily family = dmGraphics::GetInstalledAdapterFamily();
    if (family == dmGraphics::ADAPTER_FAMILY_OPENGLES || family == dmGraphics::ADAPTER_FAMILY_OPENGL) {
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        const char* version = (const char*)glGetString(GL_VERSION);
        if (renderer) { lua_pushstring(L, renderer); lua_setfield(L, -2, "gpuName"); }
        if (version) { lua_pushstring(L, version); lua_setfield(L, -2, "graphicsApi"); }
    }
    return 2;
}

static int SnapshotCancel(lua_State* L)
{
    int request_id = luaL_checkint(L, 1);
    std::lock_guard<std::mutex> guard(g_SnapshotMutex);
    if (request_id == 0 || request_id == g_RequestId) {
        g_RequestId = 0; ++g_Generation;
        [g_Snapshot release]; g_Snapshot = nil;
    }
    return 0;
}

void DeviceSnapshotIOSInitialize(lua_State* L)
{
    g_Stopped = false;
    const luaL_reg methods[] = {{"snapshot_start", SnapshotStart}, {"snapshot_poll", SnapshotPoll},
        {"snapshot_cancel", SnapshotCancel}, {0, 0}};
    luaL_register(L, "device_capabilities", methods); lua_pop(L, 1);
}

void DeviceSnapshotIOSFinalize()
{
    {
        std::lock_guard<std::mutex> guard(g_SnapshotMutex);
        g_Stopped = true; g_RequestId = 0; ++g_Generation;
        [g_Snapshot release]; g_Snapshot = nil;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        ++g_NetworkGeneration;
        if (g_Monitor) { nw_path_monitor_cancel(g_Monitor); [g_Monitor release]; g_Monitor = nil; }
        [g_Network release]; g_Network = nil;
        [g_Hardware release]; g_Hardware = nil;
        [g_UserAgent release]; g_UserAgent = nil;
        [g_WebView stopLoading]; [g_WebView release]; g_WebView = nil;
    });
}
#endif

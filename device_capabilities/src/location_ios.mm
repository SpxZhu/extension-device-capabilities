#include <dmsdk/sdk.h>
#include <math.h>
#include <string.h>

#if defined(DM_PLATFORM_IOS)
#import <CoreLocation/CoreLocation.h>
#import <UIKit/UIKit.h>

// All manager/delegate state belongs to the main queue, including Lua reads.
static void OnMain(dispatch_block_t block)
{
    if ([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
}

@interface DeviceLocation : NSObject<CLLocationManagerDelegate>
{
@public
    CLLocationManager* manager;
    int requestId;
    const char* status;
    double latitude, longitude;
    NSTimeInterval startedAt;
    BOOL pending;
}
- (void)stop;
- (void)advance;
@end

@implementation DeviceLocation
- (id)init
{
    self = [super init];
    if (self) {
        status = "unavailable";
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(background:)
            name:UIApplicationDidEnterBackgroundNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(foreground:)
            name:UIApplicationDidBecomeActiveNotification object:nil];
    }
    return self;
}
- (void)stop
{
    pending = NO;
    if (manager) {
        [manager stopUpdatingLocation];
        manager.delegate = nil;
        [manager release];
        manager = nil;
    }
}
- (void)advance
{
    if (!pending || !manager) return;
    CLAuthorizationStatus authorization = [CLLocationManager authorizationStatus];
    if (authorization == kCLAuthorizationStatusDenied || authorization == kCLAuthorizationStatusRestricted) {
        status = "permission_denied"; [self stop]; return;
    }
    if (![CLLocationManager locationServicesEnabled]) {
        status = "disabled"; [self stop]; return;
    }
    if (authorization == kCLAuthorizationStatusNotDetermined) {
        status = "permission";
        // Only request while active; returning from a system dialog rechecks authorization.
        if ([UIApplication sharedApplication].applicationState == UIApplicationStateActive)
            [manager requestWhenInUseAuthorization];
        return;
    }
    if (@available(iOS 14.0, *)) {
        if (manager.accuracyAuthorization == CLAccuracyAuthorizationReducedAccuracy) {
            status = "permission_denied"; [self stop]; return;
        }
    }
    status = "locating";
    if ([UIApplication sharedApplication].applicationState == UIApplicationStateActive)
        [manager startUpdatingLocation];
}
- (void)background:(NSNotification*)notification
{
    if (manager) [manager stopUpdatingLocation];
}
- (void)foreground:(NSNotification*)notification { [self advance]; }
- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)source
{
    if (source == manager) [self advance];
}
- (void)locationManager:(CLLocationManager*)source didChangeAuthorizationStatus:(CLAuthorizationStatus)authorization
{
    if (source == manager) [self advance];
}
- (void)locationManager:(CLLocationManager*)source didUpdateLocations:(NSArray<CLLocation*>*)locations
{
    if (source != manager || !pending || [UIApplication sharedApplication].applicationState != UIApplicationStateActive) return;
    CLLocation* location = [locations lastObject];
    if (!location || location.horizontalAccuracy < 0
        || !CLLocationCoordinate2DIsValid(location.coordinate)
        || [location.timestamp timeIntervalSinceReferenceDate] < startedAt) return;
    latitude = location.coordinate.latitude;
    longitude = location.coordinate.longitude;
    status = "success";
    [self stop];
}
- (void)locationManager:(CLLocationManager*)source didFailWithError:(NSError*)error
{
    if (source != manager || !pending || error.code == kCLErrorLocationUnknown) return;
    status = error.code == kCLErrorDenied ? "permission_denied" : "failed";
    [self stop];
}
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self stop];
    [super dealloc];
}
@end

static DeviceLocation* g_Location = nil;
static int Start(lua_State* L)
{
    int request = luaL_checkint(L, 1);
    __block bool accepted = false;
    OnMain(^{
        if (!g_Location) return;
        [g_Location stop];
        g_Location->requestId = request;
        // Missing purpose text must fail without invoking Apple's permission API.
        NSString* purpose = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSLocationWhenInUseUsageDescription"];
        if (![purpose isKindOfClass:[NSString class]] || !purpose.length) {
            g_Location->status = "unavailable"; return;
        }
        g_Location->manager = [[CLLocationManager alloc] init];
        g_Location->manager.delegate = g_Location;
        g_Location->manager.desiredAccuracy = kCLLocationAccuracyBest;
        g_Location->startedAt = [NSDate timeIntervalSinceReferenceDate];
        g_Location->pending = YES;
        [g_Location advance];
        accepted = true;
    });
    lua_pushboolean(L, accepted);
    return 1;
}
static int Poll(lua_State* L)
{
    int request = luaL_checkint(L, 1);
    __block const char* status = "unavailable";
    __block double latitude = 0, longitude = 0;
    OnMain(^{
        if (!g_Location || g_Location->requestId != request) return;
        status = g_Location->status;
        latitude = g_Location->latitude; longitude = g_Location->longitude;
    });
    lua_pushstring(L, status);
    if (strcmp(status, "success") != 0) return 1;
    lua_pushnumber(L, latitude); lua_pushnumber(L, longitude);
    return 3;
}
static int Cancel(lua_State* L)
{
    int request = luaL_checkint(L, 1);
    OnMain(^{
        if (g_Location && (!request || g_Location->requestId == request)) {
            [g_Location stop];
            g_Location->requestId = 0;
            g_Location->status = "unavailable";
        }
    });
    return 0;
}
dmExtension::Result DeviceLocationInitialize(dmExtension::Params* params)
{
    OnMain(^{ g_Location = [[DeviceLocation alloc] init]; });
    const luaL_reg methods[] = {{"location_start", Start}, {"location_poll", Poll}, {"location_cancel", Cancel}, {0, 0}};
    luaL_register(params->m_L, "device_capabilities", methods); lua_pop(params->m_L, 1);
    return dmExtension::RESULT_OK;
}
dmExtension::Result DeviceLocationFinalize(dmExtension::Params* params)
{
    OnMain(^{ [g_Location release]; g_Location = nil; });
    return dmExtension::RESULT_OK;
}
#endif

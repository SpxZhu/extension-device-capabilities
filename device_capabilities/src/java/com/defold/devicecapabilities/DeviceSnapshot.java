package com.defold.devicecapabilities;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.hardware.Sensor;
import android.hardware.SensorManager;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.BatteryManager;
import android.os.Build;
import android.os.PowerManager;
import android.os.StatFs;
import android.os.SystemClock;
import android.view.Display;
import android.view.WindowManager;
import android.webkit.WebSettings;
import com.google.android.gms.ads.identifier.AdvertisingIdClient;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.net.InetAddress;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.TimeZone;
import java.util.concurrent.Future;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** A single bounded worker, no queued collection jobs, and no logging of identifiers. */
final class DeviceSnapshot {
    private final Activity activity;
    private final DeviceBridge bridge;
    private final ThreadPoolExecutor worker = new ThreadPoolExecutor(0, 1, 30, TimeUnit.SECONDS,
        new SynchronousQueue<Runnable>(), runnable -> {
            Thread thread = new Thread(runnable, "DeviceCapabilities");
            thread.setDaemon(true);
            return thread;
        });
    private Map<String, String> hardware;
    private Map<String, String> observed = new LinkedHashMap<>();
    private int requestId, generation;
    private String status = "unavailable";
    private Future<?> task;
    private String userAgent;

    DeviceSnapshot(Activity activity, DeviceBridge bridge) {
        this.activity = activity;
        this.bridge = bridge;
    }

    private static void string(Map<String, String> values, String key, String value) {
        if (value != null && !value.isEmpty()) values.put(key, "s" + value);
    }
    private static void number(Map<String, String> values, String key, double value) {
        if (!Double.isNaN(value) && !Double.isInfinite(value)) values.put(key, "n" + Double.toString(value));
    }
    private static void integer(Map<String, String> values, String key, long value) {
        values.put(key, "i" + Long.toString(value));
    }
    private static void flag(Map<String, String> values, String key, boolean value) {
        values.put(key, value ? "n1" : "n0");
    }

    synchronized boolean start(int id) {
        cancel(0);
        requestId = id;
        final int current = ++generation;
        status = "pending";
        try {
            task = worker.submit(() -> collect(id, current));
            return true;
        } catch (java.util.concurrent.RejectedExecutionException ignored) {
            status = "unavailable";
            return false;
        }
    }

    synchronized String[] poll(int id) {
        if (id != requestId) return new String[] {"unavailable"};
        String[] result = new String[1 + observed.size() * 2];
        result[0] = status;
        int index = 1;
        for (Map.Entry<String, String> entry : observed.entrySet()) {
            result[index++] = entry.getKey();
            result[index++] = entry.getValue();
        }
        return result;
    }

    synchronized void cancel(int id) {
        if (id != 0 && id != requestId) return;
        requestId = 0;
        generation++;
        status = "unavailable";
        observed.clear();
        if (task != null) task.cancel(true);
        task = null;
    }

    synchronized void close() {
        cancel(0);
        worker.shutdownNow();
    }

    private synchronized boolean publish(int id, int current, Map<String, String> values, boolean ready) {
        if (id != requestId || current != generation) return false;
        observed = new LinkedHashMap<>(values);
        if (ready) status = "ready";
        return true;
    }

    private Map<String, String> hardware() {
        if (hardware != null) return new LinkedHashMap<>(hardware);
        Map<String, String> values = new LinkedHashMap<>();
        string(values, "packageName", activity.getPackageName());
        integer(values, "cpuCores", Runtime.getRuntime().availableProcessors());
        number(values, "screenScale", activity.getResources().getDisplayMetrics().density);
        string(values, "securityPatch", Build.VERSION.SECURITY_PATCH);
        if (Build.VERSION.SDK_INT >= 31) string(values, "socModel", Build.SOC_MODEL);
        SensorManager sensors = (SensorManager)activity.getSystemService(Context.SENSOR_SERVICE);
        if (sensors != null) {
            flag(values, "hasAccelerometer", sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) != null);
            flag(values, "hasGyroscope", sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null);
            flag(values, "hasMagnetometer", sensors.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD) != null);
        }
        try {
            ActivityManager manager = (ActivityManager)activity.getSystemService(Context.ACTIVITY_SERVICE);
            if (manager != null) {
                ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
                manager.getMemoryInfo(memory);
                integer(values, "totalMemory", memory.totalMem);
            }
        } catch (RuntimeException ignored) { }
        try {
            PackageManager manager = activity.getPackageManager();
            String name = activity.getPackageName();
            String installer = Build.VERSION.SDK_INT >= 30
                ? manager.getInstallSourceInfo(name).getInstallingPackageName() : manager.getInstallerPackageName(name);
            string(values, "installerPackageName", installer);
            int options = Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
            PackageInfo info = manager.getPackageInfo(name, options);
            integer(values, "firstInstallTime", info.firstInstallTime);
            Signature[] signatures = Build.VERSION.SDK_INT >= 28 && info.signingInfo != null
                ? info.signingInfo.getApkContentsSigners() : info.signatures;
            // The singular wire field cannot faithfully represent multiple current signers.
            if (signatures != null && signatures.length == 1) {
                byte[] digest = MessageDigest.getInstance("SHA-256").digest(signatures[0].toByteArray());
                StringBuilder hex = new StringBuilder(64);
                for (byte item : digest) hex.append(String.format(Locale.ROOT, "%02x", item & 255));
                string(values, "appSignatureHash", hex.toString());
            }
        } catch (Exception ignored) { }
        flag(values, "isEmulator", Build.FINGERPRINT.startsWith("generic") || Build.FINGERPRINT.contains("emulator")
            || Build.HARDWARE.contains("goldfish") || Build.HARDWARE.contains("ranchu")
            || Build.MODEL.contains("Emulator") || Build.PRODUCT.contains("sdk"));
        hardware = new LinkedHashMap<>(values);
        return values;
    }

    private void battery(Map<String, String> values) {
        try {
            Intent battery = activity.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (battery != null) {
                int level = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
                int scale = battery.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
                if (level >= 0 && scale > 0) integer(values, "battery", Math.round(level * 100.0 / scale));
                int status = battery.getIntExtra(BatteryManager.EXTRA_STATUS, BatteryManager.BATTERY_STATUS_UNKNOWN);
                int unified = 0;
                switch (status) {
                    case BatteryManager.BATTERY_STATUS_CHARGING: unified = 1; break;
                    case BatteryManager.BATTERY_STATUS_DISCHARGING: unified = 2; break;
                    case BatteryManager.BATTERY_STATUS_NOT_CHARGING: unified = 3; break;
                    case BatteryManager.BATTERY_STATUS_FULL: unified = 4; break;
                }
                integer(values, "batteryStatus", unified);
            }
            BatteryManager manager = (BatteryManager)activity.getSystemService(Context.BATTERY_SERVICE);
            if (manager != null) {
                long current = manager.getLongProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW);
                if (current != Long.MIN_VALUE && current != Integer.MIN_VALUE) integer(values, "batteryCurrent", current);
            }
        } catch (RuntimeException ignored) { }
    }

    private void network(Map<String, String> values) {
        try {
            ConnectivityManager manager = (ConnectivityManager)activity.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (manager == null) return;
            Network active = manager.getActiveNetwork();
            flag(values, "isNetworkAvailable", active != null);
            flag(values, "isNetworkMetered", manager.isActiveNetworkMetered());
            if (active == null) { string(values, "netwarkType", "offline"); return; }
            NetworkCapabilities capabilities = manager.getNetworkCapabilities(active);
            if (capabilities != null) {
                if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) string(values, "netwarkType", "wifi");
                else if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) string(values, "netwarkType", "cellular");
                else if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) string(values, "netwarkType", "ethernet");
            }
            LinkProperties links = manager.getLinkProperties(active);
            if (links == null) return;
            string(values, "networkInterface", links.getInterfaceName());
            InetAddress ipv6 = null;
            for (android.net.LinkAddress link : links.getLinkAddresses()) {
                InetAddress address = link.getAddress();
                if (address.isLoopbackAddress() || address.isLinkLocalAddress() || address.isAnyLocalAddress()) continue;
                if (address instanceof java.net.Inet4Address) {
                    string(values, "localIp", address.getHostAddress()); return;
                }
                if (ipv6 == null) ipv6 = address;
            }
            if (ipv6 != null) string(values, "localIp", ipv6.getHostAddress());
        } catch (RuntimeException ignored) { }
    }

    private void powerAndAudio(Map<String, String> values) {
        try {
            PowerManager manager = (PowerManager)activity.getSystemService(Context.POWER_SERVICE);
            if (manager != null) {
                flag(values, "lowPowerMode", manager.isPowerSaveMode());
                if (Build.VERSION.SDK_INT >= 29) number(values, "thermalState", manager.getCurrentThermalStatus());
            }
            AudioManager audio = (AudioManager)activity.getSystemService(Context.AUDIO_SERVICE);
            if (audio != null) {
                boolean headset = false;
                for (AudioDeviceInfo device : audio.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                    int type = device.getType();
                    headset |= type == AudioDeviceInfo.TYPE_WIRED_HEADSET || type == AudioDeviceInfo.TYPE_WIRED_HEADPHONES
                        || type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP || type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                        || type == AudioDeviceInfo.TYPE_USB_HEADSET || type == AudioDeviceInfo.TYPE_BLE_HEADSET;
                }
                flag(values, "isHeadsetPlugged", headset);
            }
            WindowManager windows = (WindowManager)activity.getSystemService(Context.WINDOW_SERVICE);
            if (windows != null) {
                float maximum = 0;
                for (Display.Mode mode : windows.getDefaultDisplay().getSupportedModes())
                    maximum = Math.max(maximum, mode.getRefreshRate());
                if (maximum > 0) number(values, "maxRefreshRateHz", maximum);
            }
        } catch (RuntimeException ignored) { }
    }

    private void security(Map<String, String> values) {
        String[] roots = {"/system/bin/su", "/system/xbin/su", "/sbin/su", "/sbin/magisk", "/data/adb/magisk"};
        boolean root = Build.TAGS != null && Build.TAGS.contains("test-keys");
        for (String path : roots) root |= new File(path).exists();
        flag(values, "isRootOrJailbreak", root);
        String data = activity.getApplicationInfo().dataDir;
        flag(values, "isVirtualApp", data != null && (data.contains("/virtual/") || data.contains("/parallel/")));
        try (BufferedReader maps = new BufferedReader(new FileReader("/proc/self/maps"))) {
            boolean hooked = false;
            String line;
            while ((line = maps.readLine()) != null) {
                String lower = line.toLowerCase(Locale.ROOT);
                hooked |= lower.contains("frida") || lower.contains("xposed") || lower.contains("substrate") || lower.contains("lsposed");
            }
            flag(values, "hasHookFramework", hooked);
        } catch (Exception ignored) { }
    }

    private void collect(int id, int current) {
        Map<String, String> values = new LinkedHashMap<>();
        try {
            values.putAll(hardware());
            string(values, "timezone", TimeZone.getDefault().getID());
            number(values, "screenScale", activity.getResources().getDisplayMetrics().density);
            int[] risk = bridge.deviceInfo();
            String[] names = {"isVpn", "isProxy", "isAdbEnabled", "isDeveloperMode"};
            for (int index = 0; index < names.length; index++)
                if (risk[index] >= 0) number(values, names[index], risk[index]);
            integer(values, "bootTime", SystemClock.elapsedRealtime());
            try {
                StatFs disk = new StatFs(activity.getFilesDir().getAbsolutePath());
                integer(values, "totalDiskSpace", disk.getTotalBytes());
                integer(values, "freeDiskSpace", disk.getAvailableBytes());
            } catch (RuntimeException ignored) { }
            battery(values);
            network(values);
            powerAndAudio(values);
            security(values);
            if (!publish(id, current, values, false)) return;
            // These services can block. Partial data remains available to the caller's deadline.
            try {
                if (userAgent == null) userAgent = WebSettings.getDefaultUserAgent(activity);
                string(values, "userAgent", userAgent);
            } catch (RuntimeException ignored) { }
            if (!publish(id, current, values, false)) return;
            try {
                AdvertisingIdClient.Info advertising = AdvertisingIdClient.getAdvertisingIdInfo(activity);
                if (!advertising.isLimitAdTrackingEnabled()) string(values, "gaid", advertising.getId());
            } catch (Exception ignored) { }
        } catch (RuntimeException ignored) {
            // Successfully collected fields survive an unavailable platform service.
        } finally {
            publish(id, current, values, true);
        }
    }
}

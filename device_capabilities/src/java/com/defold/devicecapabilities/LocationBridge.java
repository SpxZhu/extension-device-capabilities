package com.defold.devicecapabilities;

import android.Manifest;
import android.app.Activity;
import android.app.Fragment;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

/** Foreground one-shot location. No cached/default coordinate or third-party SDK. */
public final class LocationBridge {
    private final Activity activity;
    private final Handler main = new Handler(Looper.getMainLooper());
    // The Lua engine polls a synchronized snapshot; Java never calls Lua off-thread.
    private int currentId, status = 6;
    private double latitude, longitude;
    private LocationListener listener;
    private PermissionFragment permission;
    private LocationManager manager;
    private Runnable timeout;

    public LocationBridge(Activity activity) { this.activity = activity; }

    private synchronized boolean owns(int id) { return currentId == id; }
    private synchronized boolean waiting(int id) { return currentId == id && (status == 0 || status == 1); }
    private synchronized void result(int id, int state, double lat, double lon) {
        if (currentId != id) return;
        status = state; latitude = lat; longitude = lon;
    }
    public synchronized double[] poll(int id) {
        return currentId == id ? new double[] {status, latitude, longitude} : new double[] {6, 0, 0};
    }
    private boolean permitted() {
        return activity.checkCallingOrSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
            == PackageManager.PERMISSION_GRANTED;
    }
    public void start(final int id) {
        synchronized (this) { currentId = id; status = 0; latitude = longitude = 0; }
        main.post(() -> {
            if (!owns(id)) return;
            cleanup();
            try {
                if (permitted()) { locate(id); return; }
                if (Build.VERSION.SDK_INT < 23) { result(id, 3, 0, 0); return; }
                permission = new PermissionFragment();
                permission.owner = this; permission.requestId = id;
                activity.getFragmentManager().beginTransaction().add(permission, "device_capabilities_" + id).commit();
            } catch (RuntimeException error) { complete(id, 5, 0, 0); }
        });
    }
    private void permissionResult(int id) {
        if (!owns(id)) return;
        if (!permitted()) { complete(id, 3, 0, 0); return; }
        locate(id);
    }
    private void locate(final int id) {
        if (!owns(id)) return;
        try {
            manager = (LocationManager)activity.getSystemService(Context.LOCATION_SERVICE);
            if (manager == null) { complete(id, 6, 0, 0); return; }
            boolean network = manager.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
            boolean gps = manager.isProviderEnabled(LocationManager.GPS_PROVIDER);
            if (!network && !gps) { complete(id, 4, 0, 0); return; }
            result(id, 1, 0, 0);
            final long started = SystemClock.elapsedRealtimeNanos();
            listener = new LocationListener() {
                public void onLocationChanged(Location value) {
                    if (!owns(id) || value == null) return;
                    // A new measurement is required; never substitute last-known coordinates.
                    if (value.getElapsedRealtimeNanos() < started) return;
                    if (value.isFromMockProvider()) { complete(id, 5, 0, 0); return; }
                    double lat = value.getLatitude(), lon = value.getLongitude();
                    if (Double.isNaN(lat) || Double.isNaN(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180)
                        complete(id, 5, 0, 0);
                    else complete(id, 2, lat, lon);
                }
                public void onProviderDisabled(String provider) {
                    if (owns(id) && !manager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
                        && !manager.isProviderEnabled(LocationManager.GPS_PROVIDER)) complete(id, 4, 0, 0);
                }
                public void onProviderEnabled(String provider) {}
                public void onStatusChanged(String provider, int state, Bundle extras) {}
            };
            if (network) manager.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 0, 0, listener, Looper.getMainLooper());
            if (gps) manager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0, 0, listener, Looper.getMainLooper());
            timeout = () -> complete(id, 7, 0, 0);
            main.postDelayed(timeout, 30000);
        } catch (SecurityException denied) { complete(id, 3, 0, 0); }
        catch (RuntimeException error) { complete(id, 5, 0, 0); }
    }
    private void complete(int id, int state, double lat, double lon) {
        if (!waiting(id)) return;
        cleanup(); result(id, state, lat, lon);
    }
    private void cleanup() {
        if (timeout != null) { main.removeCallbacks(timeout); timeout = null; }
        if (listener != null && manager != null) {
            try { manager.removeUpdates(listener); } catch (RuntimeException ignored) {}
        }
        listener = null;
        if (permission != null) {
            permission.owner = null;
            try { activity.getFragmentManager().beginTransaction().remove(permission).commitAllowingStateLoss(); }
            catch (RuntimeException ignored) {}
            permission = null;
        }
    }
    public void cancel(int id) {
        synchronized (this) {
            if (id != 0 && currentId != id) return;
            currentId = 0; status = 6; latitude = longitude = 0;
        }
        main.post(() -> { if (owns(0)) cleanup(); });
    }
    public static final class PermissionFragment extends Fragment {
        private LocationBridge owner;
        private int requestId;
        @Override public void onCreate(Bundle saved) {
            super.onCreate(saved);
            if (owner == null || !owner.owns(requestId)) return;
            try {
                requestPermissions(new String[] {Manifest.permission.ACCESS_FINE_LOCATION,
                    Manifest.permission.ACCESS_COARSE_LOCATION}, 601);
            } catch (RuntimeException error) { owner.complete(requestId, 5, 0, 0); }
        }
        @Override public void onRequestPermissionsResult(int code, String[] names, int[] grants) {
            if (code == 601 && owner != null) owner.permissionResult(requestId);
        }
    }
}

package com.defold.devicecapabilities;

import android.app.Activity;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.ProxyInfo;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import com.google.android.play.core.integrity.IntegrityManagerFactory;
import com.google.android.play.core.integrity.StandardIntegrityException;
import com.google.android.play.core.integrity.StandardIntegrityManager;
import com.google.android.play.core.integrity.StandardIntegrityManager.PrepareIntegrityTokenRequest;
import com.google.android.play.core.integrity.StandardIntegrityManager.StandardIntegrityTokenProvider;
import com.google.android.play.core.integrity.StandardIntegrityManager.StandardIntegrityTokenRequest;
import com.google.android.play.core.integrity.StandardIntegrityManager.StandardIntegrityToken;
import com.google.android.play.core.integrity.model.StandardIntegrityErrorCode;

/** Only platform data crosses JNI. Tokens/nonces and platform errors are never logged. */
public final class DeviceBridge {
    private final Activity activity;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final StandardIntegrityManager manager;
    private StandardIntegrityTokenProvider provider;
    private long providerProject;
    private int requestId, generation, errorCode;
    private String status = "unavailable", token = "";
    private StandardIntegrityToken result;
    private final DeviceSnapshot snapshot;

    public DeviceBridge(Activity activity) {
        this.activity = activity;
        manager = IntegrityManagerFactory.createStandard(activity);
        snapshot = new DeviceSnapshot(activity, this);
    }
    public boolean snapshotStart(int id) { return snapshot.start(id); }
    public String[] snapshotPoll(int id) { return snapshot.poll(id); }
    public void snapshotCancel(int id) { snapshot.cancel(id); }
    public void close() { snapshot.close(); cancel(0); }
    private int setting(String key) {
        try { return Settings.Global.getInt(activity.getContentResolver(), key, 0); }
        catch (RuntimeException ignored) { return -1; }
    }
    public int[] deviceInfo() {
        int vpn = -1, proxy = -1;
        try {
            ConnectivityManager connectivity = (ConnectivityManager)activity.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (connectivity != null) {
                vpn = 0;
                for (Network network : connectivity.getAllNetworks()) {
                    NetworkCapabilities capabilities = connectivity.getNetworkCapabilities(network);
                    if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) vpn = 1;
                }
                ProxyInfo info = connectivity.getDefaultProxy();
                proxy = info != null && ((info.getHost() != null && !info.getHost().isEmpty())
                    || (info.getPacFileUrl() != null && !info.getPacFileUrl().toString().isEmpty())) ? 1 : 0;
            }
        } catch (RuntimeException ignored) { /* Unknown remains -1, never a false assurance. */ }
        return new int[] {vpn, proxy, setting(Settings.Global.ADB_ENABLED),
            setting(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED)};
    }
    private synchronized boolean owns(int id, int generation) {
        return requestId == id && this.generation == generation;
    }
    private synchronized void failure(int id, int generation, Exception error) {
        if (!owns(id, generation)) return;
        errorCode = error instanceof StandardIntegrityException
            ? ((StandardIntegrityException)error).getErrorCode() : 0;
        if (errorCode == StandardIntegrityErrorCode.INTEGRITY_TOKEN_PROVIDER_INVALID) provider = null;
        status = "failed"; token = ""; result = null;
    }
    private void request(int id, int generation, String hash, StandardIntegrityTokenProvider current) {
        if (!owns(id, generation)) return;
        current.request(StandardIntegrityTokenRequest.builder().setRequestHash(hash).build())
            .addOnSuccessListener(value -> {
                synchronized (this) {
                    if (!owns(id, generation)) return;
                    result = value; token = value.token(); status = "success";
                }
            }).addOnFailureListener(error -> failure(id, generation, error));
    }
    public void start(final int id, final String project, final String data) {
        final int next;
        synchronized (this) {
            requestId = id; next = ++generation;
            status = "pending"; token = ""; result = null; errorCode = 0;
        }
        main.post(() -> {
            if (!owns(id, next)) return;
            try {
                final String hash = android.util.Base64.encodeToString(java.security.MessageDigest.getInstance("SHA-256").digest(data.getBytes(java.nio.charset.StandardCharsets.UTF_8)), android.util.Base64.URL_SAFE | android.util.Base64.NO_WRAP | android.util.Base64.NO_PADDING);
                final long number = Long.parseLong(project);
                if (number <= 0) throw new IllegalArgumentException();
                if (provider != null && providerProject == number) {
                    request(id, next, hash, provider); return;
                }
                manager.prepareIntegrityToken(PrepareIntegrityTokenRequest.builder().setCloudProjectNumber(number).build())
                    .addOnSuccessListener(value -> {
                        synchronized (this) {
                            if (!owns(id, next)) return;
                            provider = value; providerProject = number;
                        }
                        request(id, next, hash, value);
                    }).addOnFailureListener(error -> failure(id, next, error));
            } catch (Exception error) { failure(id, next, error); }
        });
    }
    public synchronized String[] poll(int id) {
        return requestId == id ? new String[] {status, token, Integer.toString(errorCode)}
            : new String[] {"unavailable", "", "0"};
    }
    public void showDialog(final int id, final int type) {
        final int current;
        synchronized (this) { current = generation; }
        main.post(() -> {
            final StandardIntegrityToken value;
            synchronized (this) {
                if (!owns(id, current) || result == null) return;
                value = result; status = "dialog"; token = "";
            }
            try {
                value.showDialog(activity, type).addOnSuccessListener(code -> {
                    synchronized (this) {
                        if (!owns(id, current)) return;
                        status = "dialog_complete"; errorCode = code;
                        // A token cannot be reused after a remediation dialog.
                        result = null; provider = null;
                    }
                }).addOnFailureListener(error -> failure(id, current, error));
            } catch (RuntimeException error) { failure(id, current, error); }
        });
    }
    public synchronized void cancel(int id) {
        if (id != 0 && id != requestId) return;
        requestId = 0; generation++;
        token = ""; result = null; status = "unavailable"; errorCode = 0;
    }
}

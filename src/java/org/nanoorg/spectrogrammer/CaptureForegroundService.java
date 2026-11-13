// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0

package org.nanoorg.spectrogrammer;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

public class CaptureForegroundService extends Service {
    private static final String CHANNEL_ID = "spectrogrammer_capture";
    private static final int NOTIFICATION_ID = 1001;
    private boolean useChineseUi = false;

    @Override
    public void onCreate() {
        super.onCreate();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        useChineseUi = intent != null && intent.getBooleanExtra("use_chinese_ui", false);
        createNotificationChannel();
        Notification notification = buildNotification();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }

        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                useChineseUi ? "频谱采集" : "Spectrum Capture",
                NotificationManager.IMPORTANCE_LOW);
        channel.setDescription(useChineseUi
                ? "Spectrogrammer 后台持续采集"
                : "Continuous background capture for Spectrogrammer");

        NotificationManager notificationManager = getSystemService(NotificationManager.class);
        if (notificationManager != null) {
            notificationManager.createNotificationChannel(channel);
        }
    }

    private Notification buildNotification() {
        Intent launchIntent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launchIntent == null) {
            launchIntent = new Intent();
        }

        int pendingIntentFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            pendingIntentFlags |= PendingIntent.FLAG_IMMUTABLE;
        }

        PendingIntent pendingIntent = PendingIntent.getActivity(
                this,
                0,
                launchIntent,
                pendingIntentFlags);

        Notification.Builder builder = new Notification.Builder(this, CHANNEL_ID);

        builder.setContentTitle(useChineseUi ? "频谱仪正在采集" : "Spectrogrammer is capturing")
                .setContentText(useChineseUi ? "后台频谱分析已启用" : "Background spectrum analysis is enabled")
                .setSmallIcon(R.drawable.ic_notification)
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .setOnlyAlertOnce(true);

        return builder.build();
    }
}

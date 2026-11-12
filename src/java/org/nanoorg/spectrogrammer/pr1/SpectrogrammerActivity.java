// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0

package org.nanoorg.spectrogrammer.pr1;

import android.app.NativeActivity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public class SpectrogrammerActivity extends NativeActivity {
    private static final String TAG = "SpectrogrammerActivity";
    private static final int REQUEST_PICK_AUDIO = 2001;
    public static volatile SpectrogrammerActivity instance = null;

    static {
        System.loadLibrary("Spectrogrammer");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        instance = this;
    }

    @Override
    protected void onDestroy() {
        if (instance == this) {
            instance = null;
        }
        super.onDestroy();
    }

    public static void launchAudioPickerFromNative() {
        final SpectrogrammerActivity act = instance;
        if (act != null) {
            act.launchAudioFilePicker();
        } else {
            Log.w(TAG, "SpectrogrammerActivity instance is null");
        }
    }

    public void launchAudioFilePicker() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("audio/*");
                    String[] extraMimeTypes = new String[]{
                        "audio/*", "application/ogg", "application/x-ogg", "application/octet-stream"
                    };
                    intent.putExtra(Intent.EXTRA_MIME_TYPES, extraMimeTypes);
                    startActivityForResult(intent, REQUEST_PICK_AUDIO);
                } catch (Exception e) {
                    try {
                        Intent fallback = new Intent(Intent.ACTION_GET_CONTENT);
                        fallback.setType("audio/*");
                        startActivityForResult(fallback, REQUEST_PICK_AUDIO);
                    } catch (Exception ex) {
                        Log.e(TAG, "Failed to launch file picker", ex);
                    }
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_PICK_AUDIO && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) {
                handleSelectedAudioUri(uri);
            }
        }
    }

    private void handleSelectedAudioUri(final Uri uri) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    String displayName = "imported_audio.wav";
                    Cursor cursor = getContentResolver().query(uri, null, null, null, null);
                    if (cursor != null) {
                        try {
                            int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                            if (nameIndex >= 0 && cursor.moveToFirst()) {
                                displayName = cursor.getString(nameIndex);
                            }
                        } finally {
                            cursor.close();
                        }
                    }

                    File extDir = getExternalFilesDir(null);
                    if (extDir == null) {
                        extDir = getFilesDir();
                    }
                    if (extDir != null && !extDir.exists()) {
                        extDir.mkdirs();
                    }
                    File outFile = new File(extDir, displayName);

                    InputStream is = getContentResolver().openInputStream(uri);
                    if (is != null) {
                        FileOutputStream fos = new FileOutputStream(outFile);
                        byte[] buf = new byte[65536];
                        int len;
                        while ((len = is.read(buf)) > 0) {
                            fos.write(buf, 0, len);
                        }
                        fos.flush();
                        fos.close();
                        is.close();

                        Log.i(TAG, "Imported audio to: " + outFile.getAbsolutePath());
                        nativeOnAudioFileSelected(outFile.getAbsolutePath(), displayName);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Error importing selected audio file", e);
                }
            }
        }).start();
    }

    private static native void nativeOnAudioFileSelected(String filePath, String displayName);
}

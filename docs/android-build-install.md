# Android Build and Install

This document describes how to build, install, launch, and inspect the Android APK for the current `Spectrogrammer` repository in this environment.

## Artifacts
- APK: `Spectrogrammer.apk`
- Package name: `org.nanoorg.Spectrogrammer`

## Prerequisites
Make sure the following are available:
- Submodules initialized with `make init-submodules`
- Android SDK / NDK / build-tools installed
- `android-29` platform available
- `javac`, `keytool`, `envsubst`, `zip`, `unzip`, and `pkg-config` in `PATH`

Recommended check:

```bash
make doctor-android
```

If the SDK is not in the default location, run:

```bash
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
make doctor-android
```

## Build the APK
From the repository root:

```bash
make init-submodules
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
make BUILD_ANDROID=y
```

The build produces:

```bash
Spectrogrammer.apk
```

## Connect an Android Device

### USB ADB
If `adb devices` already shows the phone over USB, you can skip Wi-Fi ADB and install directly.

### Wi-Fi ADB
It is usually best to discover the current wireless-debugging service with mDNS first:

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb mdns services
```

If you see output like:

```text
adb-xxxxxx._adb-tls-connect._tcp
```

you can target that service name directly for `install`, `shell`, and `logcat` commands without manually running `adb connect`.

If the device is already paired, you can also keep using `connect` without entering a new pairing code:

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb connect <device-ip:port>
```

Pairing is only required the first time:

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb pair <device-ip:pairing-port>
```

Notes:
- `HOME=/tmp/adb-home` avoids failures caused by `adb` trying to write to a read-only `~/.android` in this environment
- Replace `<device-ip:port>` with the current wireless debugging endpoint shown on the phone, for example `192.168.1.4:42361`
- If `adb mdns services` already exposes an `_adb-tls-connect._tcp` service, targeting the service name directly is usually the simplest option

## Install the APK

### Install with an mDNS service name
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mdns-service-name> install -r /home/zhangheng/Spectrogrammer/Spectrogrammer.apk
```

### Install with an IP:port target
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <device-ip:port> install -r /home/zhangheng/Spectrogrammer/Spectrogrammer.apk
```

Keep `-r` for reinstalling over an existing app.

## Launch the App

### Launch with an mDNS service name
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mdns-service-name> shell monkey -p org.nanoorg.Spectrogrammer -c android.intent.category.LAUNCHER 1
```

### Launch with an IP:port target
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <device-ip:port> shell monkey -p org.nanoorg.Spectrogrammer -c android.intent.category.LAUNCHER 1
```

To force-stop before relaunching:

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <device-ip:port> shell am force-stop org.nanoorg.Spectrogrammer

env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <device-ip:port> shell monkey -p org.nanoorg.Spectrogrammer -c android.intent.category.LAUNCHER 1
```

## View Logs
With an mDNS service name:

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mdns-service-name> logcat
```

Raw logs with an IP:port target:

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <device-ip:port> logcat
```

To use the repository `make logcat` symbolization flow, ensure:
- `ANDROID_SDK_ROOT` / `ANDROID_HOME` point to the SDK
- `ADB` includes the chosen target

Example:

```bash
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
ADB='env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH adb -s <device-ip:port>' \
make logcat
```

## Clean Build Artifacts
```bash
make clean
```

This removes:
- The built APK
- Generated Android manifest files
- Intermediate output such as `build/`, `libs/`, and `makecapk/lib/`

## Shortest Common Workflow
```bash
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
make BUILD_ANDROID=y

env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb mdns services

env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mdns-service-name> install -r /home/zhangheng/Spectrogrammer/Spectrogrammer.apk
```

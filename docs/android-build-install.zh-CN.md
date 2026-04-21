# Android 打包与安装说明

本文档记录当前 `Spectrogrammer` 仓库在本机环境下的 Android APK 打包、安装、启动与日志查看方式。

## 产物
- APK 文件：`Spectrogrammer.apk`
- 包名：`org.nanoorg.Spectrogrammer`

## 构建前提
先确认以下条件满足：
- 已初始化子模块：`make init-submodules`
- Android SDK / NDK / build-tools 可用
- `android-29` 平台已安装
- `javac`、`keytool`、`envsubst`、`zip`、`unzip`、`pkg-config` 在 `PATH` 中

建议先执行：

```bash
make doctor-android
```

如果 SDK 不在默认位置，可以显式指定：

```bash
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
make doctor-android
```

## 打包 APK
在仓库根目录执行：

```bash
make init-submodules
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
make BUILD_ANDROID=y
```

执行完成后，仓库根目录会生成：

```bash
Spectrogrammer.apk
```

## 连接 Android 设备

### USB ADB
如果本机已经能直接使用 `adb devices` 看到设备，可跳过 Wi-Fi ADB 步骤，直接安装。

### Wi-Fi ADB
推荐先用 mDNS 查看当前无线调试服务：

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb mdns services
```

如果能看到类似下面的输出：

```text
adb-xxxxxx._adb-tls-connect._tcp
```

则可以直接用这个服务名执行 `install`、`shell`、`logcat`，不一定非要先手动 `adb connect`。

如果设备已完成配对，也可以继续使用 `connect`，不需要再次输入配对码：

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb connect <设备IP:端口>
```

首次配对时才需要：

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb pair <设备IP:配对端口>
```

说明：
- 这里显式设置 `HOME=/tmp/adb-home`，是为了避免当前环境下 `adb` 尝试写入只读的 `~/.android`
- `<设备IP:端口>` 需要替换成手机当前显示的无线调试地址，例如 `192.168.1.4:42361`
- 如果已经能从 `adb mdns services` 找到 `_adb-tls-connect._tcp` 服务名，优先直接使用服务名更省事

## 安装 APK

### 用 mDNS 服务名安装
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mDNS服务名> install -r /home/zhangheng/Spectrogrammer/Spectrogrammer.apk
```

### 用 IP:端口 安装
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <设备IP:端口> install -r /home/zhangheng/Spectrogrammer/Spectrogrammer.apk
```

如果只是重新覆盖安装，`-r` 保留即可。

## 启动应用

### 用 mDNS 服务名启动
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mDNS服务名> shell monkey -p org.nanoorg.Spectrogrammer -c android.intent.category.LAUNCHER 1
```

### 用 IP:端口 启动
```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <设备IP:端口> shell monkey -p org.nanoorg.Spectrogrammer -c android.intent.category.LAUNCHER 1
```

如果要先强制停止再启动：

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <设备IP:端口> shell am force-stop org.nanoorg.Spectrogrammer

env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <设备IP:端口> shell monkey -p org.nanoorg.Spectrogrammer -c android.intent.category.LAUNCHER 1
```

## 查看日志
如果你是通过 mDNS 服务名连的，也可以直接这样看：

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mDNS服务名> logcat
```

直接看原始日志：

```bash
env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <设备IP:端口> logcat
```

如果想继续使用仓库内 `make logcat` 的符号化能力，需要保证：
- `ANDROID_SDK_ROOT` / `ANDROID_HOME` 指向 SDK
- `ADB` 指向包含设备序号的 adb 命令

示例：

```bash
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
ADB='env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH adb -s <设备IP:端口>' \
make logcat
```

## 常用清理
```bash
make clean
```

该命令会删除：
- 构建出的 APK
- AndroidManifest 生成文件
- `build/`、`libs/`、`makecapk/lib/` 等中间产物

## 最常用的最短流程
```bash
env PATH=/tmp/spectrogrammer-toolchain/jdk-17.0.18+8/bin:/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
ANDROID_SDK_ROOT=/tmp/spectrogrammer-android-sdk \
ANDROID_HOME=/tmp/spectrogrammer-android-sdk \
make BUILD_ANDROID=y

env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb mdns services

env HOME=/tmp/adb-home PATH=/tmp/spectrogrammer-android-sdk/platform-tools:$PATH \
adb -s <mDNS服务名> install -r /home/zhangheng/Spectrogrammer/Spectrogrammer.apk
```

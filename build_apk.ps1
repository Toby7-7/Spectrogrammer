# Build script for Spectrogrammer-PR1 on Windows with Android NDK & SDK

$ErrorActionPreference = "Stop"

$REPO_ROOT = $PSScriptRoot
Set-Location $REPO_ROOT

$SDK = "D:\develop\android"
$NDK = "D:\develop\android\ndk\28.2.13676358"
if (-not (Test-Path $NDK)) {
    $NDK = "D:\develop\android\ndk\29.0.14206865"
}

$BUILD_TOOLS = "D:\develop\android\build-tools\36.0.0"
$PLATFORM_JAR = "$SDK\platforms\android-36\android.jar"
if (-not (Test-Path $PLATFORM_JAR)) {
    $PLATFORM_JAR = (Get-ChildItem "$SDK\platforms\android-*\android.jar" | Select-Object -First 1).FullName
}

$TOOLCHAIN_BIN = "$NDK\toolchains\llvm\prebuilt\windows-x86_64\bin"
$CC = "$TOOLCHAIN_BIN\aarch64-linux-android29-clang.cmd"
$CXX = "$TOOLCHAIN_BIN\aarch64-linux-android29-clang++.cmd"
$AR = "$TOOLCHAIN_BIN\llvm-ar.exe"
$STRIP = "$TOOLCHAIN_BIN\llvm-strip.exe"

$JAVAC = "C:\Program Files\Java\jdk-17\bin\javac.exe"
$JAR = "C:\Program Files\Java\jdk-17\bin\jar.exe"
$KEYTOOL = "C:\Program Files\Java\jdk-17\bin\keytool.exe"

$AAPT = "$BUILD_TOOLS\aapt.exe"
$D8 = "$BUILD_TOOLS\d8.bat"
$ZIPALIGN = "$BUILD_TOOLS\zipalign.exe"
$APKSIGNER = "$BUILD_TOOLS\apksigner.bat"

$ARCH = "arm64-v8a"
$APPNAME = "Spectrogrammer"
$LABEL = "频谱仪-PR1"
$PACKAGENAME = "org.nanoorg.spectrogrammer.pr1"
$APKFILE = "Spectrogrammer-PR1.apk"
$KEYSTOREFILE = "$REPO_ROOT\my-release-key.keystore"
$STOREPASS = "123456"
$ALIASNAME = "mykey"

Write-Host "==> 1. Building KissFFT for $ARCH..." -ForegroundColor Cyan
$KISSFFT_OBJ_DIR = "$REPO_ROOT\libs\$ARCH\kissfft\objs"
$KISSFFT_LIB_DIR = "$REPO_ROOT\libs\$ARCH\kissfft\lib"
New-Item -ItemType Directory -Force -Path $KISSFFT_OBJ_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $KISSFFT_LIB_DIR | Out-Null

& $CC -c -O2 -Isubmodules/kissfft -Dkiss_fft_scalar=float -DNDEBUG=1 -fPIC submodules/kissfft/kiss_fft.c -o "$KISSFFT_OBJ_DIR\kiss_fft.o"
& $CC -c -O2 -Isubmodules/kissfft -Dkiss_fft_scalar=float -DNDEBUG=1 -fPIC submodules/kissfft/kiss_fftr.c -o "$KISSFFT_OBJ_DIR\kiss_fftr.o"
& $AR ru "$KISSFFT_LIB_DIR\libkissfft-float.a" "$KISSFFT_OBJ_DIR\kiss_fft.o" "$KISSFFT_OBJ_DIR\kiss_fftr.o"

Write-Host "==> 2. Building ImGui for $ARCH..." -ForegroundColor Cyan
$IMGUI_OBJ_DIR = "$REPO_ROOT\libs\$ARCH\imgui\objs"
$IMGUI_LIB_DIR = "$REPO_ROOT\libs\$ARCH\imgui"
New-Item -ItemType Directory -Force -Path $IMGUI_OBJ_DIR | Out-Null

$IMGUI_SRCS = @(
    "submodules/imgui/imgui.cpp",
    "submodules/imgui/imgui_draw.cpp",
    "submodules/imgui/imgui_tables.cpp",
    "submodules/imgui/imgui_widgets.cpp",
    "submodules/imgui/backends/imgui_impl_opengl3.cpp",
    "submodules/imgui/backends/imgui_impl_android.cpp"
)

$IMGUI_OBJS = @()
foreach ($src in $IMGUI_SRCS) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $obj = "$IMGUI_OBJ_DIR\$baseName.o"
    & $CXX -c -Os -Isubmodules/imgui -DANDROID -fPIC -ffunction-sections -fdata-sections -Wall -fvisibility=hidden -fno-exceptions -fno-rtti -m64 $src -o $obj
    $IMGUI_OBJS += $obj
}
& $AR ru "$IMGUI_LIB_DIR\libimgui.a" $IMGUI_OBJS

Write-Host "==> 3. Compiling C/C++ App Sources..." -ForegroundColor Cyan
$APP_OBJ_DIR = "$REPO_ROOT\libs\$ARCH\app\objs"
New-Item -ItemType Directory -Force -Path $APP_OBJ_DIR | Out-Null

$CFLAGS = @(
    "-Os", "-DANDROID", "-DAPPNAME=\`"$APPNAME\`"", "-DANDROID_FULLSCREEN", "-DANDROIDVERSION=29",
    "-Isrc", "-Isrc/app", "-Isrc/audio", "-Isubmodules/kissfft", "-Isubmodules/imgui",
    "-fPIC", "-ffunction-sections", "-fdata-sections", "-Wall", "-fvisibility=hidden",
    "-fno-exceptions", "-fno-rtti", "-fno-sized-deallocation", "-m64"
)

$APP_CPP_SRCS = @(
    "src/main_android.cpp",
    "src/audio/audio_main.cpp",
    "src/audio/audio_recorder.cpp",
    "src/audio/file_source.cpp",
    "src/audio/debug_utils.cpp"
) + (Get-ChildItem -Path "src/app/*.cpp" | ForEach-Object { $_.FullName.Substring($REPO_ROOT.Length + 1).Replace('\', '/') })

$APP_C_SRCS = @(
    "src/android_native_app_glue.c"
)

$APP_OBJS = @()

foreach ($src in $APP_C_SRCS) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $obj = "$APP_OBJ_DIR\$base.o"
    & $CC -c $CFLAGS $src -o $obj
    $APP_OBJS += $obj
}

foreach ($src in $APP_CPP_SRCS) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $obj = "$APP_OBJ_DIR\$base.o"
    & $CXX -c $CFLAGS $src -o $obj
    $APP_OBJS += $obj
}

Write-Host "==> 4. Linking lib$APPNAME.so..." -ForegroundColor Cyan
$LIB_OUT_DIR = "$REPO_ROOT\makecapk\lib\$ARCH"
New-Item -ItemType Directory -Force -Path $LIB_OUT_DIR | Out-Null
$SO_PATH = "$LIB_OUT_DIR\lib$APPNAME.so"

$LDFLAGS = @(
    "-shared", "-uANativeActivity_onCreate", "-Wl,--no-undefined", "-Wl,--gc-sections", "-static-libstdc++",
    "-landroid", "-lGLESv3", "-lEGL", "-llog", "-lOpenSLES", "-lz", "-lmediandk",
    "$KISSFFT_LIB_DIR\libkissfft-float.a",
    "$IMGUI_LIB_DIR\libimgui.a"
)

& $CXX -m64 -o $SO_PATH $APP_OBJS $LDFLAGS
Write-Host "Stripping debug and symbol info from $SO_PATH..." -ForegroundColor Yellow
& $STRIP --strip-all $SO_PATH

Write-Host "==> 5. Compiling Java Classes and Generating DEX..." -ForegroundColor Cyan
$JAVA_CLASS_DIR = "$REPO_ROOT\build\android\classes"
$JAVA_DEX_DIR = "$REPO_ROOT\build\android\dex"
$JAVA_JAR_FILE = "$REPO_ROOT\build\android\classes.jar"
$DEX_FILE = "$JAVA_DEX_DIR\classes.dex"

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "$REPO_ROOT\build\android"
New-Item -ItemType Directory -Force -Path $JAVA_CLASS_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $JAVA_DEX_DIR | Out-Null

$JAVA_SOURCES = (Get-ChildItem -Path "src/java" -Recurse -Filter "*.java").FullName
& $JAVAC -encoding UTF-8 -source 8 -target 8 -classpath $PLATFORM_JAR -d $JAVA_CLASS_DIR $JAVA_SOURCES
& $JAR cf $JAVA_JAR_FILE -C $JAVA_CLASS_DIR .
& cmd.exe /c "`"$D8`" --lib `"$PLATFORM_JAR`" --output `"$JAVA_DEX_DIR`" `"$JAVA_JAR_FILE`""

Write-Host "==> 6. Packaging APK Resources..." -ForegroundColor Cyan
Remove-Item -Force -ErrorAction SilentlyContinue "$REPO_ROOT\temp.apk", "$REPO_ROOT\makecapk.apk", "$REPO_ROOT\$APKFILE"
New-Item -ItemType Directory -Force -Path "$REPO_ROOT\makecapk\assets" | Out-Null

& $AAPT package -f -F "$REPO_ROOT\temp.apk" -I $PLATFORM_JAR -M "$REPO_ROOT\src\AndroidManifest.xml" -S "$REPO_ROOT\src\res" -A "$REPO_ROOT\makecapk\assets" -v --target-sdk-version 29

# Unpack temp.apk into makecapk
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipArchive = [System.IO.Compression.ZipFile]::OpenRead("$REPO_ROOT\temp.apk")
foreach ($entry in $zipArchive.Entries) {
    $targetPath = [System.IO.Path]::Combine("$REPO_ROOT\makecapk", $entry.FullName)
    $dir = [System.IO.Path]::GetDirectoryName($targetPath)
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    if ($entry.Name -ne "") {
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $targetPath, $true)
    }
}
$zipArchive.Dispose()
Copy-Item -Force $DEX_FILE "$REPO_ROOT\makecapk\classes.dex"

# Create zip archive of makecapk with forward slashes for Android compatibility
if (Test-Path "$REPO_ROOT\makecapk.apk") { Remove-Item -Force "$REPO_ROOT\makecapk.apk" }
$apkZip = [System.IO.Compression.ZipFile]::Open("$REPO_ROOT\makecapk.apk", [System.IO.Compression.ZipArchiveMode]::Create)
$makecapkFiles = Get-ChildItem -Path "$REPO_ROOT\makecapk" -Recurse -File
$makecapkPrefix = "$REPO_ROOT\makecapk\"
foreach ($f in $makecapkFiles) {
    $entryName = $f.FullName.Substring($makecapkPrefix.Length).Replace('\', '/')
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($apkZip, $f.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
}
$apkZip.Dispose()

Write-Host "==> 7. Aligning APK with zipalign..." -ForegroundColor Cyan
& $ZIPALIGN -v -f 4 "$REPO_ROOT\makecapk.apk" "$REPO_ROOT\$APKFILE"

Write-Host "==> 8. Signing APK..." -ForegroundColor Cyan
if (-not (Test-Path $KEYSTOREFILE)) {
    Write-Host "Generating release keystore..." -ForegroundColor Yellow
    & $KEYTOOL -genkeypair -v -keystore $KEYSTOREFILE -alias $ALIASNAME -keyalg RSA -keysize 2048 -validity 10000 -storepass $STOREPASS -keypass $STOREPASS -dname "CN=Spectrogrammer, OU=PR1, O=NanoOrg, L=Unknown, ST=Unknown, C=CN"
}

& cmd.exe /c "`"$APKSIGNER`" sign --key-pass pass:$STOREPASS --ks-pass pass:$STOREPASS --ks `"$KEYSTOREFILE`" `"$REPO_ROOT\$APKFILE`""
& cmd.exe /c "`"$APKSIGNER`" verify `"$REPO_ROOT\$APKFILE`""

Write-Host "==> BUILD SUCCESSFUL! Output: $REPO_ROOT\$APKFILE" -ForegroundColor Green
Get-Item "$REPO_ROOT\$APKFILE" | Select-Object Name, Length, LastWriteTime

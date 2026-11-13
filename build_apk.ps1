# Build script for Spectrogrammer on Windows with Android NDK & SDK

param(
    [switch]$Release,
    [string]$KeystoreFile = "",
    [string]$KeystorePass = "",
    [string]$KeyAlias = "",
    [string]$KeyPass = ""
)

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
$LABEL = "Spectrogrammer"
$PACKAGENAME = "org.nanoorg.spectrogrammer"
$APKFILE = "Spectrogrammer.apk"
$KEYSTORE_PROPS = @{}
$PROPS_PATH = "$REPO_ROOT\keystore.properties"
if (Test-Path $PROPS_PATH) {
    foreach ($line in Get-Content $PROPS_PATH) {
        $trimmed = $line.Trim()
        if ($trimmed -eq "" -or $trimmed.StartsWith("#")) { continue }
        $eqIdx = $trimmed.IndexOf("=")
        if ($eqIdx -gt 0) {
            $k = $trimmed.Substring(0, $eqIdx).Trim()
            $v = $trimmed.Substring($eqIdx + 1).Trim()
            $KEYSTORE_PROPS[$k] = $v
        }
    }
}

$HAS_EXPLICIT_SIGNING = $Release -or $KeystoreFile -or $env:KEYSTORE_FILE -or ($KEYSTORE_PROPS.ContainsKey("storeFile"))
if (-not $KeystoreFile) { $KeystoreFile = $env:KEYSTORE_FILE }
if (-not $KeystoreFile -and $KEYSTORE_PROPS.ContainsKey("storeFile")) { $KeystoreFile = $KEYSTORE_PROPS["storeFile"] }

if ($HAS_EXPLICIT_SIGNING) {
    if (-not $KeystoreFile) {
        throw "Release signing requested, but no keystore path was specified. Set -KeystoreFile, KEYSTORE_FILE, or keystore.properties."
    }
    if (-not [System.IO.Path]::IsPathRooted($KeystoreFile)) {
        $KeystoreFile = [System.IO.Path]::GetFullPath((Join-Path $REPO_ROOT $KeystoreFile))
    }
    if (-not (Test-Path $KeystoreFile)) {
        throw "Keystore file not found: $KeystoreFile"
    }
    if (-not $KeyAlias) { $KeyAlias = $env:KEY_ALIAS }
    if (-not $KeyAlias -and $KEYSTORE_PROPS.ContainsKey("keyAlias")) { $KeyAlias = $KEYSTORE_PROPS["keyAlias"] }
    if (-not $KeystorePass) { $KeystorePass = $env:KEYSTORE_PASS }
    if (-not $KeystorePass -and $KEYSTORE_PROPS.ContainsKey("storePassword")) { $KeystorePass = $KEYSTORE_PROPS["storePassword"] }
    if (-not $KeyPass) { $KeyPass = $env:KEY_PASS }
    if (-not $KeyPass -and $KEYSTORE_PROPS.ContainsKey("keyPassword")) { $KeyPass = $KEYSTORE_PROPS["keyPassword"] }
    if (-not $KeyPass) { $KeyPass = $KeystorePass }
    if (-not $KeyAlias -or -not $KeystorePass) {
        throw "Release signing requires keyAlias and storePassword (via CLI args, env vars, or keystore.properties)."
    }
    $KEYSTOREFILE = $KeystoreFile
    $ALIASNAME = $KeyAlias
    $STOREPASS = $KeystorePass
    $KEYPASS = $KeyPass
} else {
    $KEYSTOREFILE = "$REPO_ROOT\my-release-key.keystore"
    $STOREPASS = "123456"
    $ALIASNAME = "mykey"
    $KEYPASS = $STOREPASS
}

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
    & $CXX -c -Os -Isubmodules/imgui -DANDROID -fPIC -ffunction-sections -fdata-sections -Wall -Wno-unused-variable -fvisibility=hidden -fno-exceptions -fno-rtti -m64 $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "Failed compiling ImGui source: $src" }
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

Write-Host "==> 5. Generating Manifest & R.java..." -ForegroundColor Cyan

# Dynamically calculate VersionCode and VersionName
$GIT_COUNT = (git rev-list --count HEAD 2>$null)
if (-not $GIT_COUNT) { $GIT_COUNT = 101 }
$VERSION_CODE = [int]$GIT_COUNT
$VERSION_NAME = "1.1.0"
Write-Host "Configuring Manifest: package=$PACKAGENAME, versionCode=$VERSION_CODE, versionName=$VERSION_NAME" -ForegroundColor Yellow

$TEMPLATE = Get-Content "$REPO_ROOT\src\AndroidManifest.xml.template" -Raw
$MANIFEST_CONTENT = $TEMPLATE `
    -replace '\$\{PACKAGENAME\}', $PACKAGENAME `
    -replace '\$\{ANDROIDVERSION\}', "29" `
    -replace '\$\{ANDROIDTARGET\}', "29" `
    -replace '\$\{APPNAME\}', $APPNAME `
    -replace '\$\{VERSION_CODE\}', $VERSION_CODE `
    -replace '\$\{VERSION_NAME\}', $VERSION_NAME

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText("$REPO_ROOT\src\AndroidManifest.xml", $MANIFEST_CONTENT, $utf8NoBom)

$JAVA_GEN_DIR = "$REPO_ROOT\build\android\gen"
$JAVA_CLASS_DIR = "$REPO_ROOT\build\android\classes"
$JAVA_DEX_DIR = "$REPO_ROOT\build\android\dex"
$JAVA_JAR_FILE = "$REPO_ROOT\build\android\classes.jar"
$DEX_FILE = "$JAVA_DEX_DIR\classes.dex"

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "$REPO_ROOT\build\android"
New-Item -ItemType Directory -Force -Path $JAVA_GEN_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $JAVA_CLASS_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $JAVA_DEX_DIR | Out-Null

& $AAPT package -f -m -J $JAVA_GEN_DIR -I $PLATFORM_JAR -M "$REPO_ROOT\src\AndroidManifest.xml" -S "$REPO_ROOT\src\res"
if ($LASTEXITCODE -ne 0) { throw "aapt R.java generation failed with exit code $LASTEXITCODE" }

Write-Host "==> 6. Compiling Java Classes and Generating DEX..." -ForegroundColor Cyan
$JAVA_SOURCES = @((Get-ChildItem -Path "src/java" -Recurse -Filter "*.java").FullName) + @((Get-ChildItem -Path $JAVA_GEN_DIR -Recurse -Filter "*.java").FullName)
& $JAVAC -encoding UTF-8 -source 8 -target 8 -Xlint:all -Xlint:-options -classpath $PLATFORM_JAR -d $JAVA_CLASS_DIR $JAVA_SOURCES
if ($LASTEXITCODE -ne 0) { throw "javac failed with exit code $LASTEXITCODE" }
& $JAR cf $JAVA_JAR_FILE -C $JAVA_CLASS_DIR .
if ($LASTEXITCODE -ne 0) { throw "jar failed with exit code $LASTEXITCODE" }
& cmd.exe /c "`"$D8`" --lib `"$PLATFORM_JAR`" --output `"$JAVA_DEX_DIR`" `"$JAVA_JAR_FILE`""
if ($LASTEXITCODE -ne 0) { throw "d8 failed with exit code $LASTEXITCODE" }

Write-Host "==> 7. Packaging APK Resources..." -ForegroundColor Cyan

Remove-Item -Force -ErrorAction SilentlyContinue "$REPO_ROOT\temp.apk", "$REPO_ROOT\makecapk.apk", "$REPO_ROOT\$APKFILE"
New-Item -ItemType Directory -Force -Path "$REPO_ROOT\makecapk\assets" | Out-Null

& $AAPT package -f -F "$REPO_ROOT\temp.apk" -I $PLATFORM_JAR -M "$REPO_ROOT\src\AndroidManifest.xml" -S "$REPO_ROOT\src\res" -A "$REPO_ROOT\makecapk\assets" -v --target-sdk-version 29
if ($LASTEXITCODE -ne 0) { throw "aapt package failed with exit code $LASTEXITCODE" }

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

Write-Host "==> 8. Aligning APK with zipalign..." -ForegroundColor Cyan
& $ZIPALIGN -v -f 4 "$REPO_ROOT\makecapk.apk" "$REPO_ROOT\$APKFILE"
if ($LASTEXITCODE -ne 0) { throw "zipalign failed with exit code $LASTEXITCODE" }

Write-Host "==> 9. Signing APK..." -ForegroundColor Cyan
if ((-not $HAS_EXPLICIT_SIGNING) -and (-not (Test-Path $KEYSTOREFILE))) {
    Write-Host "Generating local dev keystore ($KEYSTOREFILE)..." -ForegroundColor Yellow
    & $KEYTOOL -genkeypair -v -keystore $KEYSTOREFILE -alias $ALIASNAME -keyalg RSA -keysize 2048 -validity 10000 -storepass $STOREPASS -keypass $KEYPASS -dname "CN=Spectrogrammer, OU=NanoOrg, O=NanoOrg, L=Unknown, ST=Unknown, C=CN"
    if ($LASTEXITCODE -ne 0) { throw "keytool failed with exit code $LASTEXITCODE" }
}

& $APKSIGNER sign --ks $KEYSTOREFILE --ks-key-alias $ALIASNAME --ks-pass "pass:$STOREPASS" --key-pass "pass:$KEYPASS" "$REPO_ROOT\$APKFILE"
if ($LASTEXITCODE -ne 0) { throw "apksigner sign failed with exit code $LASTEXITCODE" }
& $APKSIGNER verify "$REPO_ROOT\$APKFILE"
if ($LASTEXITCODE -ne 0) { throw "apksigner verify failed with exit code $LASTEXITCODE" }

# Clean up temporary build artifacts in makecapk and root
Remove-Item -Force -ErrorAction SilentlyContinue "$REPO_ROOT\makecapk\classes.dex", "$REPO_ROOT\makecapk\resources.arsc", "$REPO_ROOT\makecapk\AndroidManifest.xml", "$REPO_ROOT\temp.apk", "$REPO_ROOT\makecapk.apk"

Write-Host "==> BUILD SUCCESSFUL! Output: $REPO_ROOT\$APKFILE" -ForegroundColor Green
Get-Item "$REPO_ROOT\$APKFILE" | Select-Object Name, Length, LastWriteTime

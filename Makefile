#Copyright (c) 2019-2020 <>< Charles Lohr - Under the MIT/x11 or NewBSD License you choose.
# NO WARRANTY! NO GUARANTEE OF SUPPORT! USE AT YOUR OWN RISK

BUILD_ANDROID:=y
DEBUG:=n
CXX?=c++

ifeq ($(BUILD_ANDROID),y)
ARCH:=arm64-v8a
all : makecapk.apk
else
ARCH:=x86_64
all: linux_version
endif

.PHONY: all push run uninstall logcat clean testsdk doctor-android check-adb init-submodules keystore folders kissfft manifest

# WARNING WARNING WARNING!  YOU ABSOLUTELY MUST OVERRIDE THE PROJECT NAME
# you should also override these parameters, get your own signatre file and make your own manifest.
APPNAME?=Spectrogrammer
SRC_DIR?=./src
KISSFFT_PKGCONFIG:=libs/$(ARCH)/kissfft/lib64/pkgconfig/kissfft-float.pc

ifeq ($(BUILD_ANDROID),y)
LABEL?=频谱仪
APKFILE?=$(APPNAME).apk
PACKAGENAME?=org.nanoorg.$(APPNAME)
SRC=$(SRC_DIR)/main_android.cpp $(SRC_DIR)/android_native_app_glue.c
SRC+=$(SRC_DIR)/audio/audio_main.cpp
SRC+=$(SRC_DIR)/audio/audio_recorder.cpp
SRC+=$(SRC_DIR)/audio/debug_utils.cpp
else
SRC=$(SRC_DIR)/main_linux.cpp
#SRC+=$(SRC_DIR)/audio/audio_driver_sdl.cpp
CFLAGS+= -DALSA_DRIVER
SRC+=$(SRC_DIR)/audio/audio_driver_alsa.cpp
endif

# Add app source files
SRC+=$(shell find $(SRC_DIR)/app -name '*.cpp')

CFLAGS?=-ffunction-sections  -fdata-sections -Wall -fvisibility=hidden -fno-exceptions -fno-rtti -fno-sized-deallocation
# For really tight compiles....
CFLAGS += -fvisibility=hidden

LDFLAGS?=-Wl,--gc-sections -Wl,-Map=output.map 
ifeq ($(DEBUG),y)
LDFLAGS += -g  
else
LDFLAGS += -s -Os
endif

UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
OS_NAME = linux-x86_64
endif
ifeq ($(UNAME), Darwin)
OS_NAME = darwin-x86_64
endif
ifeq ($(OS), Windows_NT)
OS_NAME = windows-x86_64
endif

#if you have a custom Android Home location you can add it to this list.  
#This makefile will select the first present folder.
#We've tested it with android version 22, 24, 28, 29 and 30 and 32.
#You can target something like Android 28, but if you set ANDROIDVERSION to say 22, then
#Your app should (though not necessarily) support all the way back to Android 22. 
ANDROIDVERSION ?= 29
ANDROIDTARGET ?= $(ANDROIDVERSION)
ANDROID_FULLSCREEN ?= y
ADB ?= adb

# Search list for where to try to find the SDK
SDK_LOCATIONS += $(ANDROID_HOME) $(ANDROID_SDK_ROOT) $(HOME)/Android/Sdk $(HOME)/Library/Android/sdk

#Just a little Makefile witchcraft to find the first SDK_LOCATION that exists
#Then find an ndk folder and build tools folder in there.
ANDROIDSDK ?= $(firstword $(foreach dir,$(SDK_LOCATIONS),$(if $(wildcard $(dir)),$(dir))))
NDK ?= $(firstword $(ANDROID_NDK) $(ANDROID_NDK_HOME) $(wildcard $(ANDROIDSDK)/ndk/*) $(wildcard $(ANDROIDSDK)/ndk-bundle))
BUILD_TOOLS ?= $(lastword $(sort $(wildcard $(ANDROIDSDK)/build-tools/*)))
AAPT := $(BUILD_TOOLS)/aapt
ZIPALIGN := $(BUILD_TOOLS)/zipalign
APKSIGNER := $(BUILD_TOOLS)/apksigner
ANDROID_JAR := $(ANDROIDSDK)/platforms/android-$(ANDROIDVERSION)/android.jar
ANDROID_TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)
ANDROID_SYSROOT := $(ANDROID_TOOLCHAIN)/sysroot

ifeq ($(BUILD_ANDROID),y)
ANDROIDSRCS:= $(SRC)
ANDROID_CPP_SRCS := $(filter %.cpp,$(ANDROIDSRCS))
ANDROID_C_SRCS := $(filter %.c,$(ANDROIDSRCS))
ANDROID_OBJ_DIR := libs/$(ARCH)/app/objs
ANDROID_OBJS := $(patsubst %.cpp,$(ANDROID_OBJ_DIR)/%.o,$(ANDROID_CPP_SRCS)) \
	$(patsubst %.c,$(ANDROID_OBJ_DIR)/%.o,$(ANDROID_C_SRCS))
JAVA_SOURCES := $(shell find $(SRC_DIR)/java -name '*.java' 2>/dev/null)
ANDROID_BUILD_DIR := build/android
JAVA_CLASS_DIR := $(ANDROID_BUILD_DIR)/classes
JAVA_JAR_FILE := $(ANDROID_BUILD_DIR)/classes.jar
ANDROID_DEX_DIR := $(ANDROID_BUILD_DIR)/dex
ANDROID_DEX_FILE := $(ANDROID_DEX_DIR)/classes.dex
JAVAC ?= javac
D8 := $(BUILD_TOOLS)/d8

CFLAGS+=-Os -DANDROID -DAPPNAME=\"$(APPNAME)\"
ifeq ($(ANDROID_FULLSCREEN),y)
CFLAGS +=-DANDROID_FULLSCREEN
endif

CFLAGS+= --sysroot=$(ANDROID_SYSROOT) -fPIC -DANDROIDVERSION=$(ANDROIDVERSION)
endif

CFLAGS+= -I$(SRC_DIR) -I$(SRC_DIR)/app -I$(SRC_DIR)/audio 
CFLAGS+= -Isubmodules/kissfft -Isubmodules/imgui

ifeq ($(BUILD_ANDROID),y)
LDFLAGS += -landroid -lGLESv3 -lEGL  -llog -lOpenSLES 
LDFLAGS += -shared -uANativeActivity_onCreate
endif

########################### ARCHITECTURE ######################################

ifeq ($(ARCH),arm64-v8a)
CFLAGS_2+=-m64

LDFLAGS+= `pkg-config --libs $(KISSFFT_PKGCONFIG)`
ARCH_DIR=$(ARCH)
AR:=$(ANDROID_TOOLCHAIN)/bin/llvm-ar
CC:=$(ANDROID_TOOLCHAIN)/bin/aarch64-linux-android$(ANDROIDVERSION)-clang
CXX:=$(ANDROID_TOOLCHAIN)/bin/aarch64-linux-android$(ANDROIDVERSION)-clang++
endif

ifeq ($(ARCH),armeabi-v7a)
CFLAGS_2+=-mfloat-abi=softfp -m32

LDFLAGS+= `pkg-config --libs $(KISSFFT_PKGCONFIG)`
ARCH_DIR=arm
AR:=$(ANDROID_TOOLCHAIN)/bin/llvm-ar
CC:=$(ANDROID_TOOLCHAIN)/bin/armv7a-linux-androideabi$(ANDROIDVERSION)-clang
CXX:=$(ANDROID_TOOLCHAIN)/bin/armv7a-linux-androideabi$(ANDROIDVERSION)-clang++
endif

ifeq ($(ARCH),x86_64)
LDFLAGS += -lGL `pkg-config --static --libs glfw3` 
LDFLAGS += -lX11 -lpthread -lXinerama -lXext -lGL -lm -ldl -lstdc++
LDFLAGS += -lasound
#LDFLAGS += -lSDL2
LDFLAGS += `pkg-config --libs $(KISSFFT_PKGCONFIG)`
ARCH_DIR=$(ARCH)
CFLAGS += `pkg-config --cflags glfw3` 
CFLAGS += -std=c++11
CFLAGS += -Wall -Wformat
#CFLAGS_2:=-march=i686 -mtune=intel -mssse3 -mfpmath=sse -m32
CFLAGS_2:=-march=x86-64 -msse4.2 -mpopcnt -m64 -mtune=intel#CFLAGS_x86:=-march=i686 -mtune=intel -mssse3 -mfpmath=sse -m32

#AR:=$(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/bin/i686-linux-android$(ANDROIDVERSION)-ar
#LD:=$(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/bin/i686-linux-android$(ANDROIDVERSION)-ld
#CC:=$(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/bin/x86_64-linux-android$(ANDROIDVERSION)-clang

endif

LDFLAGS += -lm  
LDFLAGS += -Wl,--no-undefined

STOREPASS?=password
DNAME:="CN=example.com, OU=ID, O=Example, L=Doe, S=John, C=GB"
KEYSTOREFILE:=my-release-key.keystore
ALIASNAME?=standkey

keystore : $(KEYSTOREFILE)

$(KEYSTOREFILE) :
	keytool -genkey -v -keystore $(KEYSTOREFILE) -alias $(ALIASNAME) -keyalg RSA -keysize 2048 -validity 10000 -storepass $(STOREPASS) -keypass $(STOREPASS) -dname $(DNAME)

init-submodules:
	git submodule sync --recursive
	git submodule update --init --recursive

doctor-android:
	@status=0; \
	echo "SDK:\t\t$(if $(ANDROIDSDK),$(ANDROIDSDK),missing)"; \
	if [ -z "$(ANDROIDSDK)" ] || [ ! -d "$(ANDROIDSDK)" ]; then \
		echo "error: Android SDK not found. Set ANDROID_HOME or ANDROID_SDK_ROOT, or install it under $$HOME/Android/Sdk."; \
		status=1; \
	fi; \
	echo "NDK:\t\t$(if $(NDK),$(NDK),missing)"; \
	if [ -z "$(NDK)" ] || [ ! -d "$(NDK)" ]; then \
		echo "error: Android NDK not found. Install a side-by-side NDK under the Android SDK."; \
		status=1; \
	fi; \
	echo "Build Tools:\t$(if $(BUILD_TOOLS),$(BUILD_TOOLS),missing)"; \
	if [ -z "$(BUILD_TOOLS)" ] || [ ! -d "$(BUILD_TOOLS)" ]; then \
		echo "error: Android build-tools not found."; \
		status=1; \
	fi; \
	if [ -n "$(BUILD_TOOLS)" ]; then \
		for tool in "$(AAPT)" "$(ZIPALIGN)" "$(APKSIGNER)" "$(D8)"; do \
			if [ ! -x "$$tool" ]; then \
				echo "error: missing Android build tool $$tool"; \
				status=1; \
			fi; \
		done; \
	fi; \
	if [ -n "$(ANDROIDSDK)" ] && [ ! -f "$(ANDROID_JAR)" ]; then \
		echo "error: missing Android platform jar $(ANDROID_JAR)"; \
		status=1; \
	fi; \
	if [ ! -f submodules/imgui/imgui.cpp ] || [ ! -f submodules/kissfft/Makefile ]; then \
		echo "error: submodules are not initialized. Run: make init-submodules"; \
		status=1; \
	fi; \
	for tool in java javac keytool envsubst zip unzip pkg-config; do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			echo "error: missing $$tool in PATH"; \
			status=1; \
		fi; \
	done; \
	if ! command -v $(ADB) >/dev/null 2>&1; then \
		echo "note: adb not found; required only for make push, make run, and make logcat"; \
	fi; \
	exit $$status

testsdk: doctor-android

check-adb:
	@command -v $(ADB) >/dev/null 2>&1 || { \
		echo "error: adb not found in PATH. Install Android platform-tools or set ADB=/path/to/adb."; \
		exit 1; \
	}

folders:
	mkdir -p makecapk/lib/arm64-v8a
	mkdir -p makecapk/lib/armeabi-v7a
	mkdir -p makecapk/lib/x86
	mkdir -p makecapk/lib/x86_64

ifeq ($(BUILD_ANDROID),y)
kissfft: doctor-android
endif

kissfft:
	@if [ -d "libs/$(ARCH)/kissfft" ]; then \
		echo "KissFFT already built libs/$(ARCH)/kissfft"; \
	else \
		$(MAKE) -C submodules/kissfft PREFIX=../../libs/$(ARCH)/kissfft CC=$(CC) AR=$(AR) KISSFFT_DATATYPE=float KISSFFT_STATIC=1 KISSFFT_TOOLS=0 KISSFFT_OPENMP=0 CFLAGS=-DNDEBUG=1 install; \
		$(MAKE) -C submodules/kissfft clean; \
	fi

################## IMGUI

IMGUI_SRCS := imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp backends/imgui_impl_opengl3.cpp
ifeq ($(BUILD_ANDROID),y)
IMGUI_SRCS += backends/imgui_impl_android.cpp 
else
IMGUI_SRCS += backends/imgui_impl_glfw.cpp
endif

libs/$(ARCH)/imgui/objs/%.o : submodules/imgui/%.cpp
	mkdir -p $(dir $@)
	$(CXX) -c $(CFLAGS) -Isubmodules/imgui $(CFLAGS_2) $< -o $@

libs/$(ARCH)/imgui/libimgui.a : $(addprefix libs/$(ARCH)/imgui/objs/,$(subst .cpp,.o,$(IMGUI_SRCS)))
	$(AR) ru $@ $^

###############

ifeq ($(BUILD_ANDROID),y)
$(ANDROID_OBJ_DIR)/%.o : %.cpp | doctor-android
	mkdir -p $(dir $@)
	$(CXX) -c $(CFLAGS) $(CFLAGS_2) $< -o $@

$(ANDROID_OBJ_DIR)/%.o : %.c | doctor-android
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(CFLAGS_2) $< -o $@

makecapk/lib/$(ARCH_DIR)/lib$(APPNAME).so : $(ANDROID_OBJS) libs/$(ARCH)/imgui/libimgui.a | doctor-android kissfft
	mkdir -p makecapk/lib/$(ARCH_DIR)
	$(CXX) $(CFLAGS_2) -static-libstdc++ -o $@ $(ANDROID_OBJS) libs/$(ARCH)/imgui/libimgui.a $(LDFLAGS)
endif

ifeq ($(BUILD_ANDROID),y)
$(ANDROID_DEX_FILE) : $(JAVA_SOURCES) | doctor-android
	rm -rf $(JAVA_CLASS_DIR) $(ANDROID_DEX_DIR) $(JAVA_JAR_FILE)
	mkdir -p $(JAVA_CLASS_DIR) $(ANDROID_DEX_DIR)
	$(JAVAC) -encoding UTF-8 -source 8 -target 8 -classpath $(ANDROID_JAR) -d $(JAVA_CLASS_DIR) $(JAVA_SOURCES)
	jar cf $(JAVA_JAR_FILE) -C $(JAVA_CLASS_DIR) .
	$(D8) --lib $(ANDROID_JAR) --output $(ANDROID_DEX_DIR) $(JAVA_JAR_FILE)
endif

#App name and other localized labels now come from Android string resources under src/res.

#Notes for the past:  These lines used to work, but don't seem to anymore.  Switched to newer jarsigner.
#(zipalign -c -v 8 makecapk.apk)||true #This seems to not work well.
#jarsigner -verify -verbose -certs makecapk.apk


linux_version : $(SRC) libs/$(ARCH)/imgui/libimgui.a | kissfft
	echo $@ $^
	$(CXX) $(CFLAGS) -o $@ $^  $(LDFLAGS)
	mv $@ $(APPNAME)

makecapk.apk : doctor-android makecapk/lib/$(ARCH_DIR)/lib$(APPNAME).so $(SRC_DIR)/AndroidManifest.xml $(ANDROID_DEX_FILE)
	mkdir -p makecapk/assets
	#cp -r $(SRC_DIR)/assets/* makecapk/assets
	rm -rf temp.apk
	$(AAPT) package -f -F temp.apk -I $(ANDROID_JAR) -M $(SRC_DIR)/AndroidManifest.xml -S $(SRC_DIR)/res -A makecapk/assets -v --target-sdk-version $(ANDROIDTARGET)
	unzip -o temp.apk -d makecapk
	cp $(ANDROID_DEX_FILE) makecapk/classes.dex
	rm -rf makecapk.apk
	# We use -4 here for the compression ratio, as it's a good balance of speed and size. -9 will make a slightly smaller executable but takes longer to build
	cd makecapk && zip -D4r ../makecapk.apk . && zip -D0r ../makecapk.apk ./resources.arsc ./AndroidManifest.xml
	# jarsigner is only necessary when targetting Android < 7.0
	#jarsigner -sigalg SHA1withRSA -digestalg SHA1 -verbose -keystore $(KEYSTOREFILE) -storepass $(STOREPASS) makecapk.apk $(ALIASNAME)
	rm -rf $(APKFILE)
	$(ZIPALIGN) -v 4 makecapk.apk $(APKFILE)
	#Using the apksigner in this way is only required on Android 30+
	$(APKSIGNER) sign --key-pass pass:$(STOREPASS) --ks-pass pass:$(STOREPASS) --ks $(KEYSTOREFILE) $(APKFILE)
	rm -rf temp.apk
	rm -rf makecapk.apk
	@ls -l $(APKFILE)

manifest: $(SRC_DIR)/AndroidManifest.xml

$(SRC_DIR)/AndroidManifest.xml : doctor-android
	rm -rf $(SRC_DIR)/AndroidManifest.xml
	PACKAGENAME=$(PACKAGENAME) \
		ANDROIDVERSION=$(ANDROIDVERSION) \
		ANDROIDTARGET=$(ANDROIDTARGET) \
		APPNAME=$(APPNAME) envsubst '$$ANDROIDTARGET $$ANDROIDVERSION $$APPNAME $$PACKAGENAME' \
		< $(SRC_DIR)/AndroidManifest.xml.template > $(SRC_DIR)/AndroidManifest.xml

uninstall : check-adb
	($(ADB) uninstall $(PACKAGENAME))||true

push : check-adb makecapk.apk
	@echo "Installing" $(PACKAGENAME)
	$(ADB) install -r $(APKFILE)

logcat: check-adb push
	$(ADB) logcat | $(NDK)/ndk-stack -sym makecapk/lib/arm64-v8a/libSpectrogrammer.so
	#$(ADB) logcat | $(NDK)/ndk-stack -sym makecapk/lib/armeabi-v7a/libSpectrogrammer.so

run : check-adb push
	$(eval ACTIVITYNAME:=$(shell $(AAPT) dump badging $(APKFILE) | grep "launchable-activity" | cut -f 2 -d"'"))
	$(ADB) shell am start -n $(PACKAGENAME)/$(ACTIVITYNAME)

clean :
	rm -rf $(SRC_DIR)/AndroidManifest.xml temp.apk makecapk.apk $(APKFILE)
	rm -rf makecapk/assets makecapk/lib
	rm -rf libs
	rm -rf build
	rm -rf linux_version $(APPNAME)

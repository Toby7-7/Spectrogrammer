// dear imgui: standalone example application for Android + OpenGL ES 3

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
//#include "imgui_impl_android.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/window.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <unistd.h>
#include <string>
#include "Spectrogrammer.h"

// Data
static EGLDisplay           g_EglDisplay = EGL_NO_DISPLAY;
static EGLSurface           g_EglSurface = EGL_NO_SURFACE;
static EGLContext           g_EglContext = EGL_NO_CONTEXT;
static struct android_app*  g_App = nullptr;
static bool                 g_WindowInitialized = false;
static bool                 g_ImGuiInitialized = false;
static bool                 g_SpectrogrammerInitialized = false;
static bool                 g_RecordAudioPermissionRequested = false;
static bool                 g_BackgroundServiceRunning = false;
static char                 g_LogTag[] = "ImGuiExample";
static std::string          g_IniFilename = "";
static ImVector<ImWchar>    g_FontGlyphRanges;

// Forward declarations of helper functions
static void Start(struct android_app* app);
static void Stop();
static void Init(struct android_app* app);
static void Resume(struct android_app* app);
static void ShutdownWindow();
static void Shutdown();
static void MainLoopStep();
static void AndroidDisplayKeyboard(int pShow);
static int GetAssetData(const char* filename, void** out_data);
static void keep_screen_on();
static bool HasRecordAudioPermission();
static void RequestRecordAudioPermission();
static void DrawPermissionPrompt();
static void ConfigureImGuiStyle();
static const ImWchar* BuildUiGlyphRanges();
static void LoadBestAvailableFont();
static void StartCaptureForegroundService();
static void StopCaptureForegroundService();
static void MoveTaskToBack();

// Main code
static void handleAppCmd(struct android_app* app, int32_t appCmd)
{
    switch (appCmd)
    {
    case APP_CMD_START:
        Start(app);
        break;
    case APP_CMD_STOP:
        Stop();
        break;
    case APP_CMD_SAVE_STATE:
        break;
    case APP_CMD_INIT_WINDOW:
        Init(app);
        break;
    case APP_CMD_TERM_WINDOW:
        ShutdownWindow();
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
        break;
    case APP_CMD_RESUME:
        Resume(app);
        break;        
    }
}

static int32_t handleInputEvent(struct android_app* app, AInputEvent* inputEvent)
{
    if (AInputEvent_getType(inputEvent) == AINPUT_EVENT_TYPE_KEY &&
        AKeyEvent_getKeyCode(inputEvent) == AKEYCODE_BACK)
    {
        if (AKeyEvent_getAction(inputEvent) == AKEY_EVENT_ACTION_UP)
        {
            if (!Spectrogrammer_HandleBackPressed())
                MoveTaskToBack();
        }
        return 1;
    }

    if (!g_WindowInitialized || ImGui::GetCurrentContext() == nullptr)
        return 0;

    if (AInputEvent_getType(inputEvent) == AINPUT_EVENT_TYPE_MOTION)
    {
        const int32_t pointer_count = AMotionEvent_getPointerCount(inputEvent);
        const int32_t action = AMotionEvent_getAction(inputEvent) & AMOTION_EVENT_ACTION_MASK;
        if (pointer_count >= 2)
        {
            float xs[2] = {
                AMotionEvent_getX(inputEvent, 0),
                AMotionEvent_getX(inputEvent, 1),
            };
            float ys[2] = {
                AMotionEvent_getY(inputEvent, 0),
                AMotionEvent_getY(inputEvent, 1),
            };
            if (Spectrogrammer_HandleTouchGesture(action, 2, xs, ys))
                return 1;
        }
    }

    return ImGui_ImplAndroid_HandleInputEvent(inputEvent);
}

extern "C" void android_main(struct android_app* app)
{
    app->onAppCmd = handleAppCmd;
    app->onInputEvent = handleInputEvent;

    while (true)
    {
        int out_events;
        struct android_poll_source* out_data;

        // Poll all events. If the app is not visible, this loop blocks until a window is ready.
        while (ALooper_pollAll(g_WindowInitialized ? 0 : -1, nullptr, &out_events, (void**)&out_data) >= 0)
        {
            // Process one event
            if (out_data != nullptr)
                out_data->process(app, out_data);

            // Exit the app by returning from within the infinite loop
            if (app->destroyRequested != 0)
            {
                if (g_SpectrogrammerInitialized)
                {
                    StopCaptureForegroundService();
                    Spectrogrammer_Shutdown();
                    g_SpectrogrammerInitialized = false;
                }

                if (g_WindowInitialized || g_ImGuiInitialized)
                    Shutdown();

                return;
            }
        }

        // Initiate a new frame
        MainLoopStep();
    }
}

void Start(struct android_app* app)
{
}

void Stop(  )
{   
}

void Resume(struct android_app* app)
{
    g_App = app;
    keep_screen_on();
}

void Init(struct android_app* app)
{
    if (g_WindowInitialized)
        return;

    g_App = app;
    if (g_App == nullptr || g_App->window == nullptr)
        return;

    ANativeWindow_acquire(g_App->window);

    keep_screen_on();

    // Initialize EGL
    // This is mostly boilerplate code for EGL...
    {
        g_EglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (g_EglDisplay == EGL_NO_DISPLAY)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglGetDisplay(EGL_DEFAULT_DISPLAY) returned EGL_NO_DISPLAY");

        if (eglInitialize(g_EglDisplay, 0, 0) != EGL_TRUE)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglInitialize() returned with an error");

        const EGLint egl_attributes[] = { EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
        EGLint num_configs = 0;
        if (eglChooseConfig(g_EglDisplay, egl_attributes, nullptr, 0, &num_configs) != EGL_TRUE)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglChooseConfig() returned with an error");
        if (num_configs == 0)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglChooseConfig() returned 0 matching config");

        // Get the first matching config
        EGLConfig egl_config;
        eglChooseConfig(g_EglDisplay, egl_attributes, &egl_config, 1, &num_configs);
        EGLint egl_format;
        eglGetConfigAttrib(g_EglDisplay, egl_config, EGL_NATIVE_VISUAL_ID, &egl_format);
        ANativeWindow_setBuffersGeometry(g_App->window, 0, 0, egl_format);

        const EGLint egl_context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        g_EglContext = eglCreateContext(g_EglDisplay, egl_config, EGL_NO_CONTEXT, egl_context_attributes);

        if (g_EglContext == EGL_NO_CONTEXT)
            __android_log_print(ANDROID_LOG_ERROR, g_LogTag, "%s", "eglCreateContext() returned EGL_NO_CONTEXT");

        g_EglSurface = eglCreateWindowSurface(g_EglDisplay, egl_config, g_App->window, nullptr);
        eglMakeCurrent(g_EglDisplay, g_EglSurface, g_EglSurface, g_EglContext);
    }

    if (!g_ImGuiInitialized)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        // Redirect loading/saving of .ini file to our location.
        // Make sure 'g_IniFilename' persists while we use Dear ImGui.
        g_IniFilename = std::string(app->activity->internalDataPath) + "/imgui.ini";
        io.IniFilename = g_IniFilename.c_str();

        ImGui::StyleColorsDark();
        ConfigureImGuiStyle();
        LoadBestAvailableFont();
        g_ImGuiInitialized = true;
    }

    ImGui_ImplAndroid_Init(g_App->window);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_WindowInitialized = true;
}

void MainLoopStep()
{
    ImGuiIO& io = ImGui::GetIO();
    if (g_EglDisplay == EGL_NO_DISPLAY)
        return;

    // Open on-screen (soft) input if requested by Dear ImGui
    static bool WantTextInputLast = false;
    if (io.WantTextInput && !WantTextInputLast)
        AndroidDisplayKeyboard(io.WantTextInput?1:0);
    WantTextInputLast = io.WantTextInput;

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    if (!g_SpectrogrammerInitialized)
    {
        if (HasRecordAudioPermission())
        {
            Spectrogrammer_Init(g_App);
            g_SpectrogrammerInitialized = true;
            StartCaptureForegroundService();
        }
        else
        {
            if (!g_RecordAudioPermissionRequested)
                RequestRecordAudioPermission();
            DrawPermissionPrompt();
        }
    }

    if (g_SpectrogrammerInitialized)
    {
        Spectrogrammer_MainLoopStep();
        if (Spectrogrammer_ShouldRunInBackground())
            StartCaptureForegroundService();
        else
            StopCaptureForegroundService();
    }

    keep_screen_on();

    // Rendering
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    const ImVec4 clear_color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    eglSwapBuffers(g_EglDisplay, g_EglSurface);
}

void ShutdownWindow()
{
    if (!g_WindowInitialized)
        return;

    Spectrogrammer_ReleaseGraphics();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();

    if (g_EglDisplay != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(g_EglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (g_EglContext != EGL_NO_CONTEXT)
            eglDestroyContext(g_EglDisplay, g_EglContext);

        if (g_EglSurface != EGL_NO_SURFACE)
            eglDestroySurface(g_EglDisplay, g_EglSurface);

        eglTerminate(g_EglDisplay);
    }

    g_EglDisplay = EGL_NO_DISPLAY;
    g_EglContext = EGL_NO_CONTEXT;
    g_EglSurface = EGL_NO_SURFACE;
    ANativeWindow_release(g_App->window);
    g_WindowInitialized = false;
}

void Shutdown()
{
    ShutdownWindow();

    if (g_ImGuiInitialized)
    {
        ImGui::DestroyContext();
        g_ImGuiInitialized = false;
    }

    g_RecordAudioPermissionRequested = false;
}

// Helper functions

#ifdef __cplusplus
#define SETUP_FOR_JAVA_CALL \
	JNIEnv * env = 0; \
	JNIEnv ** envptr = &env; \
	JavaVM * jniiptr = g_App->activity->vm; \
	jniiptr->AttachCurrentThread( (JNIEnv**)&env, 0 ); \
	env = (*envptr);
#define ENVCALL
#define JAVA_CALL_DETACH 	jniiptr->DetachCurrentThread();
#else
#define SETUP_FOR_JAVA_CALL \
	const struct JNINativeInterface * env = (struct JNINativeInterface*)g_App->activity->env; \
	const struct JNINativeInterface ** envptr = &env; \
	const struct JNIInvokeInterface ** jniiptr = gag_Apppp->activity->vm; \
	const struct JNIInvokeInterface * jnii = *jniiptr; \
	jnii->AttachCurrentThread( jniiptr, &envptr, NULL); \
	env = (*envptr);
#define ENVCALL envptr,
#define JAVA_CALL_DETACH       	jnii->DetachCurrentThread( jniiptr );
#endif

void keep_screen_on() 
{
    if (g_App == nullptr || g_App->activity == nullptr)
        return;

    ANativeActivity_setWindowFlags(
        g_App->activity,
        Spectrogrammer_ShouldStayAwake() ? AWINDOW_FLAG_KEEP_SCREEN_ON : 0,
        Spectrogrammer_ShouldStayAwake() ? 0 : AWINDOW_FLAG_KEEP_SCREEN_ON);
}

void AndroidDisplayKeyboard(int pShow)
{
	//Based on https://stackoverflow.com/questions/5864790/how-to-show-the-soft-keyboard-on-native-activity
	jint lFlags = 0;
	SETUP_FOR_JAVA_CALL

	jclass activityClass = env->FindClass( ENVCALL "android/app/NativeActivity");

	// Retrieves NativeActivity.
	jobject lNativeActivity = g_App->activity->clazz;


	// Retrieves Context.INPUT_METHOD_SERVICE.
	jclass ClassContext = env->FindClass( ENVCALL "android/content/Context");
	jfieldID FieldINPUT_METHOD_SERVICE = env->GetStaticFieldID( ENVCALL ClassContext, "INPUT_METHOD_SERVICE", "Ljava/lang/String;" );
	jobject INPUT_METHOD_SERVICE = env->GetStaticObjectField( ENVCALL ClassContext, FieldINPUT_METHOD_SERVICE );

	// Runs getSystemService(Context.INPUT_METHOD_SERVICE).
	jclass ClassInputMethodManager = env->FindClass( ENVCALL "android/view/inputmethod/InputMethodManager" );
	jmethodID MethodGetSystemService = env->GetMethodID( ENVCALL activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
	jobject lInputMethodManager = env->CallObjectMethod( ENVCALL lNativeActivity, MethodGetSystemService, INPUT_METHOD_SERVICE);

	// Runs getWindow().getDecorView().
	jmethodID MethodGetWindow = env->GetMethodID( ENVCALL activityClass, "getWindow", "()Landroid/view/Window;");
	jobject lWindow = env->CallObjectMethod( ENVCALL lNativeActivity, MethodGetWindow);
	jclass ClassWindow = env->FindClass( ENVCALL "android/view/Window");
	jmethodID MethodGetDecorView = env->GetMethodID( ENVCALL ClassWindow, "getDecorView", "()Landroid/view/View;");
	jobject lDecorView = env->CallObjectMethod( ENVCALL lWindow, MethodGetDecorView);

	if (pShow) {
		// Runs lInputMethodManager.showSoftInput(...).
		jmethodID MethodShowSoftInput = env->GetMethodID( ENVCALL ClassInputMethodManager, "showSoftInput", "(Landroid/view/View;I)Z");
		/*jboolean lResult = */env->CallBooleanMethod( ENVCALL lInputMethodManager, MethodShowSoftInput, lDecorView, lFlags);
	} else {
		// Runs lWindow.getViewToken()
		jclass ClassView = env->FindClass( ENVCALL "android/view/View");
		jmethodID MethodGetWindowToken = env->GetMethodID( ENVCALL ClassView, "getWindowToken", "()Landroid/os/IBinder;");
		jobject lBinder = env->CallObjectMethod( ENVCALL lDecorView, MethodGetWindowToken);

		// lInputMethodManager.hideSoftInput(...).
		jmethodID MethodHideSoftInput = env->GetMethodID( ENVCALL ClassInputMethodManager, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
		/*jboolean lRes = */env->CallBooleanMethod( ENVCALL lInputMethodManager, MethodHideSoftInput, lBinder, lFlags);
	}

	JAVA_CALL_DETACH
}

static bool HasRecordAudioPermission()
{
    if (g_App == nullptr)
        return false;

    SETUP_FOR_JAVA_CALL

    jclass activityClass = env->FindClass(ENVCALL "android/app/NativeActivity");
    jobject nativeActivity = g_App->activity->clazz;
    jmethodID checkSelfPermissionMethod = env->GetMethodID(
        ENVCALL activityClass, "checkSelfPermission", "(Ljava/lang/String;)I");
    jstring permission = env->NewStringUTF("android.permission.RECORD_AUDIO");
    jint permissionState = env->CallIntMethod(
        ENVCALL nativeActivity, checkSelfPermissionMethod, permission);
    env->DeleteLocalRef(permission);

    JAVA_CALL_DETACH

    return permissionState == 0;
}

static void RequestRecordAudioPermission()
{
    if (g_App == nullptr)
        return;

    SETUP_FOR_JAVA_CALL

    jclass activityClass = env->FindClass(ENVCALL "android/app/NativeActivity");
    jobject nativeActivity = g_App->activity->clazz;
    jclass stringClass = env->FindClass(ENVCALL "java/lang/String");
    jobjectArray permissions = env->NewObjectArray(1, stringClass, nullptr);
    jstring permission = env->NewStringUTF("android.permission.RECORD_AUDIO");
    env->SetObjectArrayElement(ENVCALL permissions, 0, permission);

    jmethodID requestPermissionsMethod = env->GetMethodID(
        ENVCALL activityClass, "requestPermissions", "([Ljava/lang/String;I)V");
    env->CallVoidMethod(ENVCALL nativeActivity, requestPermissionsMethod, permissions, 1001);

    env->DeleteLocalRef(permission);
    env->DeleteLocalRef(permissions);
    g_RecordAudioPermissionRequested = true;

    JAVA_CALL_DETACH
}

static void DrawPermissionPrompt()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize;

    ImGui::Begin("麦克风权限", nullptr, flags);
    ImGui::SetCursorPosY(viewport->WorkSize.y * 0.25f);
    ImGui::TextWrapped("频谱仪需要麦克风权限后才能启动频谱分析。");
    ImGui::Spacing();
    ImGui::TextWrapped("请在系统弹窗中授予录音权限，然后点击“重试”。");
    ImGui::Spacing();
    if (ImGui::Button("重试", ImVec2(-1.0f, 0.0f)))
    {
        g_RecordAudioPermissionRequested = false;
        RequestRecordAudioPermission();
    }
    ImGui::End();
}

static float CalculateUiScale()
{
    if (g_App == nullptr || g_App->window == nullptr)
        return 2.0f;

    const int width = ANativeWindow_getWidth(g_App->window);
    const int height = ANativeWindow_getHeight(g_App->window);
    const int short_side = width < height ? width : height;
    float scale = short_side / 390.0f;
    if (scale < 2.0f)
        scale = 2.0f;
    if (scale > 3.15f)
        scale = 3.15f;
    return scale;
}

static void ConfigureImGuiStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    const float ui_scale = CalculateUiScale();
    style.ScaleAllSizes(ui_scale);
    style.TouchExtraPadding = ImVec2(8.0f * ui_scale, 8.0f * ui_scale);
    style.WindowBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.FrameRounding = 10.0f * ui_scale;
    style.FramePadding = ImVec2(12.0f * ui_scale, 10.0f * ui_scale);
    style.GrabRounding = 10.0f * ui_scale;
    style.ScrollbarRounding = 10.0f * ui_scale;
    style.ScrollbarSize = 22.0f * ui_scale;
    style.ItemSpacing = ImVec2(12.0f * ui_scale, 12.0f * ui_scale);
    style.WindowPadding = ImVec2(16.0f * ui_scale, 16.0f * ui_scale);
}

static bool TryLoadSystemFont(const char* path, float size_pixels)
{
    if (path == nullptr || access(path, R_OK) != 0)
        return false;

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig font_cfg;
    font_cfg.SizePixels = size_pixels;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    font_cfg.PixelSnapH = false;
    if (io.Fonts->AddFontFromFileTTF(path, size_pixels, &font_cfg, BuildUiGlyphRanges()) != nullptr)
    {
        __android_log_print(ANDROID_LOG_INFO, g_LogTag, "loaded system font: %s", path);
        return true;
    }

    return false;
}

static const ImWchar* BuildUiGlyphRanges()
{
    if (!g_FontGlyphRanges.empty())
        return g_FontGlyphRanges.Data;

    ImGuiIO& io = ImGui::GetIO();
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

    static const char* kUiTexts[] = {
        "频谱仪",
        "主窗口",
        "麦克风权限",
        "频谱仪需要麦克风权限后才能启动频谱分析。",
        "请在系统弹窗中授予录音权限，然后点击“重试”。",
        "重试",
        "返回主界面",
        "暂停",
        "继续",
        "对比",
        "清除峰值",
        "音频",
        "显示",
        "语言",
        "中文",
        "英文",
        "默认",
        "通用",
        "语音识别",
        "摄像机",
        "未处理",
        "采样率",
        "自动",
        "处理",
        "FFT 大小",
        "抽取级数",
        "窗函数",
        "矩形窗",
        "汉宁窗",
        "汉明窗",
        "布莱克曼-哈里斯窗",
        "滚动速度",
        "越小越快",
        "越大越平滑",
        "指数平滑因子",
        "频率轴刻度",
        "普通对数",
        "音乐对数",
        "实时峰值",
        "短时峰值",
        "峰值标记来源",
        "视图",
        "运行",
        "后台采集",
        "显示瀑布图",
        "分钟",
        "Mel",
        "Bark",
        "ERB",
        "线性",
        "瀑布图高度",
        "显示峰值保持曲线",
        "峰值回落时长",
        "峰值标记",
        "保持亮屏",
        "频谱",
        "当前",
        "上限",
        "输入",
        "分辨率",
        "秒",
        "个",
        "记录当前曲线",
        "清除基线",
        "关闭",
    };

    for (const char* text : kUiTexts)
        builder.AddText(text);

    builder.BuildRanges(&g_FontGlyphRanges);
    return g_FontGlyphRanges.Data;
}

static void LoadBestAvailableFont()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float ui_scale = CalculateUiScale();
    const float font_size = 20.0f * ui_scale;
    static const char* kFontCandidates[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/NotoSansCJKsc-Regular.otf",
        "/system/fonts/NotoSansHans-Regular.otf",
        "/system/fonts/SourceHanSansSC-Regular.otf",
        "/system/fonts/SourceHanSansCN-Regular.otf",
        "/system/fonts/NotoSerifCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/MiSans-Normal.ttf",
        "/system/fonts/ColorOSUI-Regular.ttf",
        "/system/fonts/OPlusSans3VF.ttf",
        "/system/fonts/OPlusSans-Regular.ttf",
        "/system/fonts/OPPOSans-R.ttf",
        "/system/fonts/HarmonyOS_Sans_SC_Regular.ttf",
    };

    for (const char* candidate : kFontCandidates)
    {
        if (TryLoadSystemFont(candidate, font_size))
            return;
    }

    ImFontConfig font_cfg;
    font_cfg.SizePixels = font_size;
    io.Fonts->AddFontDefault(&font_cfg);
    __android_log_print(ANDROID_LOG_WARN, g_LogTag, "%s", "no system CJK font found; using fallback font");
}

static jobject BuildForegroundServiceIntent(JNIEnv *env, jobject nativeActivity)
{
    jclass intentClass = env->FindClass("android/content/Intent");
    jmethodID ctor = env->GetMethodID(intentClass, "<init>", "()V");
    jobject intent = env->NewObject(intentClass, ctor);
    jmethodID setClassNameMethod = env->GetMethodID(
        intentClass, "setClassName",
        "(Landroid/content/Context;Ljava/lang/String;)Landroid/content/Intent;");
    jstring className = env->NewStringUTF("org.nanoorg.Spectrogrammer.CaptureForegroundService");
    env->CallObjectMethod(intent, setClassNameMethod, nativeActivity, className);
    env->DeleteLocalRef(className);
    return intent;
}

static void StartCaptureForegroundService()
{
    if (g_BackgroundServiceRunning || g_App == nullptr || g_App->activity == nullptr)
        return;

    if (!Spectrogrammer_ShouldRunInBackground())
        return;

    SETUP_FOR_JAVA_CALL

    jobject nativeActivity = g_App->activity->clazz;
    jclass activityClass = env->GetObjectClass(nativeActivity);
    jobject intent = BuildForegroundServiceIntent(env, nativeActivity);

    jmethodID startForegroundServiceMethod = env->GetMethodID(
        activityClass, "startForegroundService", "(Landroid/content/Intent;)Landroid/content/ComponentName;");
    env->CallObjectMethod(ENVCALL nativeActivity, startForegroundServiceMethod, intent);
    env->DeleteLocalRef(intent);

    g_BackgroundServiceRunning = true;

    JAVA_CALL_DETACH
}

static void StopCaptureForegroundService()
{
    if (!g_BackgroundServiceRunning || g_App == nullptr || g_App->activity == nullptr)
        return;

    SETUP_FOR_JAVA_CALL

    jobject nativeActivity = g_App->activity->clazz;
    jclass activityClass = env->GetObjectClass(nativeActivity);
    jobject intent = BuildForegroundServiceIntent(env, nativeActivity);

    jmethodID stopServiceMethod = env->GetMethodID(activityClass, "stopService", "(Landroid/content/Intent;)Z");
    env->CallBooleanMethod(ENVCALL nativeActivity, stopServiceMethod, intent);
    env->DeleteLocalRef(intent);

    g_BackgroundServiceRunning = false;

    JAVA_CALL_DETACH
}

static void MoveTaskToBack()
{
    if (g_App == nullptr || g_App->activity == nullptr)
        return;

    SETUP_FOR_JAVA_CALL

    jobject nativeActivity = g_App->activity->clazz;
    jclass activityClass = env->GetObjectClass(nativeActivity);
    jmethodID moveTaskToBackMethod = env->GetMethodID(activityClass, "moveTaskToBack", "(Z)Z");
    if (moveTaskToBackMethod != nullptr)
        env->CallBooleanMethod(ENVCALL nativeActivity, moveTaskToBackMethod, JNI_TRUE);

    JAVA_CALL_DETACH
}


// Helper to retrieve data placed into the assets/ directory (android/app/src/main/assets)
static int GetAssetData(const char* filename, void** outData)
{
    int num_bytes = 0;
    AAsset* asset_descriptor = AAssetManager_open(g_App->activity->assetManager, filename, AASSET_MODE_BUFFER);
    if (asset_descriptor)
    {
        num_bytes = AAsset_getLength(asset_descriptor);
        *outData = IM_ALLOC(num_bytes);
        int64_t num_bytes_read = AAsset_read(asset_descriptor, *outData, num_bytes);
        AAsset_close(asset_descriptor);
        IM_ASSERT(num_bytes_read == num_bytes);
    }
    return num_bytes;
}

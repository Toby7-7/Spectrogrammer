#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <string.h>
#include "Processor.h"
#include "fft.h"
#include "pass_through.h"
#include "ScaleUI.h"
#include "ScaleBufferX.h"
#include "ScaleBufferY.h"
#include "ChunkerProcessor.h"
#include "Spectrogrammer.h"
#include "AppConfig.h"
#include "colormaps.h"
#include "ModalHoldPicker.h"

#include <GLES3/gl3.h>
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_helpers.h"
#include "waterfall.h"
#include "audio/audio_main.h"
#ifdef ANDROID
#include "audio/audio_SLES.h"
#include <EGL/egl.h>
#include <jni.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>
#include "../android_native_app_glue.h"
#include "backends/imgui_impl_android.h"
#else
#include <GLFW/glfw3.h>
#include "backends/imgui_impl_glfw.h"
#endif

namespace
{
enum HOLDING_STATE
{
    HOLDING_STATE_NO_HOLD = 0,
    HOLDING_STATE_STARTED = 1,
    HOLDING_STATE_READY = 2
};

enum class SettingsPage
{
    None = 0,
    Settings = 1,
};

struct PeakMarker
{
    bool active = false;
    int bin = 0;
    float freq_hz = 0.0f;
    float value_db = -120.0f;
    float normalized_x = 0.0f;
    float normalized_y = 0.0f;
};

struct SharedState
{
    struct ChannelState
    {
        BufferIODouble live_power_raw;
        BufferIODouble live_line;
        BufferIODouble peak_hold_raw;
        BufferIODouble peak_hold_line;
        PeakMarker peak_markers[5];
        int peak_marker_count = 0;
        bool peak_hold_valid = false;
    };

    ChannelState channels[2];
    BufferIODouble reference_hold_raw;
    BufferIODouble reference_hold_line;
};

struct DisplayChannelState
{
    BufferIODouble live_power_raw;
    BufferIODouble live_line;
    BufferIODouble peak_hold_raw;
    BufferIODouble peak_hold_line;
    PeakMarker peak_markers[5];
    int peak_marker_count = 0;
    bool peak_hold_valid = false;
};

struct PinchGestureState
{
    bool active = false;
    float previous_left_x = 0.0f;
    float previous_right_x = 1.0f;
};

constexpr int kMaxInputChannels = 2;

AppConfig gConfig = MakeDefaultAppConfig();
std::string gConfigPath;
std::string gWorkingDirectory;
bool gConfigLoaded = false;
bool gConfigDirty = false;

SharedState gSharedState;
DisplayChannelState gDisplayChannels[kMaxInputChannels];
std::mutex gStateMutex;
std::thread gProcessingThread;
std::atomic<bool> gProcessingThreadStop{false};
bool gSessionInitialized = false;

Processor *gProcessors[kMaxInputChannels] = {nullptr, nullptr};
ScaleBufferBase *gScaleBufferX = nullptr;
ChunkerProcessor gChunker;

float gInputSampleRate = 48000.0f;
float gEffectiveSampleRate = 48000.0f;
int gRequestedInputChannels = 1;
int gActiveInputChannels = 1;
int gProcessingChannelCount = 1;
int gDisplayChannelCount = 1;
bool gInputChannelsFallbackActive = false;
float gMinFreq = 0.0f;
float gMaxFreq = 0.0f;
float gAxisYMin = -130.0f;
float gAxisYMax = -20.0f;
float gAxisFreqMin = 0.0f;
float gAxisFreqMax = 0.0f;
int gHopSamples = 1024;
float gAnalysisSecondsPerFrame = 0.02f;
float gWaterfallSecondsPerRow = 0.02f;
float gWaterfallRowAccumulator = 0.0f;

bool gDisplayPaused = false;
bool gWaterfallInitialized[kMaxInputChannels] = {false, false};
int gFrequencyPlotWidth = 0;
int gWaterfallWidth[kMaxInputChannels] = {0, 0};
int gWaterfallHeight[kMaxInputChannels] = {0, 0};
SettingsPage gSettingsPage = SettingsPage::None;
bool gSettingsScrollTracking = false;
bool gSettingsScrollDragging = false;
ImVec2 gSettingsScrollStartMousePos = ImVec2(0.0f, 0.0f);
float gSettingsScrollStartY = 0.0f;
bool gCloseHoldPopupRequested = false;
PinchGestureState gPinchGesture;

ImRect gFrequencyGestureFrame;
bool gFrequencyGestureFrameValid = false;

HOLDING_STATE gHoldingState = HOLDING_STATE_NO_HOLD;

bool gCursorActive = false;
bool gCursorDragging = false;
float gCursorFrequencyHz = 0.0f;
float gCursorDb[kMaxInputChannels] = {-120.0f, -120.0f};

BufferIODouble gScaledPowerY[kMaxInputChannels];
BufferIODouble gScaledPowerXY[kMaxInputChannels];
BufferIODouble gPeakScaledPowerY[kMaxInputChannels];
BufferIODouble gPeakScaledPowerXY[kMaxInputChannels];
BufferIODouble gReferenceScaledPowerY;
BufferIODouble gReferenceScaledPowerXY;

#ifdef ANDROID
android_app *gAndroidApp = nullptr;
#endif

constexpr int kDefaultAndroidSampleRate = 48000;
constexpr int kAudioBufferLength = 1024;
constexpr int kPeakMarkerCapacity = 5;
constexpr float kToolbarTopPadding = 72.0f;
constexpr float kSpectrumMinimumHeight = 260.0f;
constexpr float kMinimumTouchButtonHeight = 88.0f;
constexpr int kProcessingBurstLimit = 32;
constexpr float kMinimumPinchDistanceNormalized = 0.04f;
constexpr float kSettingsSideMargin = 28.0f;
constexpr char kGitHubRepositoryUrl[] = "https://github.com/Toby7-7/Spectrogrammer";

void rebuild_scale_locked(int outputWidth);
void refresh_display_state_locked(bool updateWaterfall);

float top_safe_padding()
{
    return std::max(kToolbarTopPadding, ImGui::GetIO().DisplaySize.y * 0.03f);
}

float large_button_height()
{
    return std::max(kMinimumTouchButtonHeight, ImGui::GetFrameHeight() * 1.25f);
}

void set_full_width_item()
{
    ImGui::SetNextItemWidth(-FLT_MIN);
}

float settings_side_margin()
{
    return std::max(kSettingsSideMargin, ImGui::GetIO().DisplaySize.x * 0.055f);
}

bool open_url_external(const char *url)
{
#ifdef ANDROID
    if (url == nullptr || gAndroidApp == nullptr || gAndroidApp->activity == nullptr || gAndroidApp->activity->vm == nullptr)
        return false;

    JNIEnv *env = nullptr;
    JavaVM *vm = gAndroidApp->activity->vm;
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || env == nullptr)
        return false;

    bool opened = false;
    jobject nativeActivity = gAndroidApp->activity->clazz;
    jclass activityClass = env->GetObjectClass(nativeActivity);
    jclass intentClass = env->FindClass("android/content/Intent");
    jclass uriClass = env->FindClass("android/net/Uri");

    if (activityClass != nullptr && intentClass != nullptr && uriClass != nullptr)
    {
        jfieldID actionViewField = env->GetStaticFieldID(intentClass, "ACTION_VIEW", "Ljava/lang/String;");
        jmethodID intentCtor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;)V");
        jmethodID parseMethod = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        jmethodID setDataMethod = env->GetMethodID(intentClass, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;");
        jmethodID startActivityMethod = env->GetMethodID(activityClass, "startActivity", "(Landroid/content/Intent;)V");

        if (actionViewField != nullptr && intentCtor != nullptr && parseMethod != nullptr &&
            setDataMethod != nullptr && startActivityMethod != nullptr)
        {
            jstring actionView = static_cast<jstring>(env->GetStaticObjectField(intentClass, actionViewField));
            jstring urlString = env->NewStringUTF(url);
            jobject uri = env->CallStaticObjectMethod(uriClass, parseMethod, urlString);
            jobject intent = env->NewObject(intentClass, intentCtor, actionView);

            if (actionView != nullptr && urlString != nullptr && uri != nullptr && intent != nullptr)
            {
                env->CallObjectMethod(intent, setDataMethod, uri);
                env->CallVoidMethod(nativeActivity, startActivityMethod, intent);
                opened = !env->ExceptionCheck();
            }

            if (env->ExceptionCheck())
                env->ExceptionClear();

            if (intent != nullptr)
                env->DeleteLocalRef(intent);
            if (uri != nullptr)
                env->DeleteLocalRef(uri);
            if (urlString != nullptr)
                env->DeleteLocalRef(urlString);
            if (actionView != nullptr)
                env->DeleteLocalRef(actionView);
        }
    }

    if (uriClass != nullptr)
        env->DeleteLocalRef(uriClass);
    if (intentClass != nullptr)
        env->DeleteLocalRef(intentClass);
    if (activityClass != nullptr)
        env->DeleteLocalRef(activityClass);

    vm->DetachCurrentThread();
    return opened;
#else
    (void)url;
    return false;
#endif
}

bool waterfall_visible()
{
    return gConfig.show_waterfall;
}

bool spectrum_visible()
{
    return gConfig.show_spectrum;
}

SharedState::ChannelState &channel_state(int channel)
{
    return gSharedState.channels[channel];
}

DisplayChannelState &display_channel_state(int channel)
{
    return gDisplayChannels[channel];
}

int active_input_channel_count()
{
    return std::max(1, std::min(gActiveInputChannels, kMaxInputChannels));
}

bool stereo_input_available()
{
    return active_input_channel_count() >= 2;
}

bool mode_requires_stereo(InputChannelMode mode)
{
    return mode != InputChannelMode::Mono;
}

int requested_input_channel_count_for_mode(InputChannelMode mode)
{
    return mode_requires_stereo(mode) ? 2 : 1;
}

int processing_channel_count_for_mode(InputChannelMode mode, int input_channels)
{
    if (input_channels < 2)
        return 1;

    if (mode == InputChannelMode::StereoDifference)
        return 1;

    if (mode == InputChannelMode::Mono)
        return 1;

    return 2;
}

bool mode_uses_stereo_difference(InputChannelMode mode, int input_channels)
{
    return input_channels >= 2 && mode == InputChannelMode::StereoDifference;
}

int processing_channel_count()
{
    return std::max(1, std::min(gProcessingChannelCount, kMaxInputChannels));
}

bool input_channel_mode_change_requires_restart(InputChannelMode from, InputChannelMode to)
{
    if (requested_input_channel_count_for_mode(from) != requested_input_channel_count_for_mode(to))
        return true;

    const int input_channels = active_input_channel_count();
    return processing_channel_count_for_mode(from, input_channels) != processing_channel_count_for_mode(to, input_channels);
}

int display_channel_count()
{
    return std::max(1, std::min(gDisplayChannelCount, kMaxInputChannels));
}

int mapped_left_source_channel()
{
    return stereo_input_available() ? 1 : 0;
}

int mapped_right_source_channel()
{
    return stereo_input_available() ? 0 : 0;
}

const char *mapped_source_channel_title(int source_channel)
{
    if (!stereo_input_available())
        return "单声道";
    return source_channel == mapped_left_source_channel() ? "左" : "右";
}

bool display_mode_is_split()
{
    return gConfig.input_channel_mode == InputChannelMode::StereoIndependent && stereo_input_available();
}

int display_source_channel(int display_channel)
{
    if (!stereo_input_available())
        return 0;

    if (gConfig.input_channel_mode == InputChannelMode::Right)
        return mapped_right_source_channel();

    if (gConfig.input_channel_mode == InputChannelMode::StereoIndependent)
        return gConfig.swap_stereo_order
                   ? (display_channel == 0 ? mapped_right_source_channel() : mapped_left_source_channel())
                   : (display_channel == 0 ? mapped_left_source_channel() : mapped_right_source_channel());

    return 0;
}

const char *display_channel_title(int display_channel)
{
    if (!display_mode_is_split())
        return "";

    return mapped_source_channel_title(display_source_channel(display_channel));
}

ImU32 display_channel_live_color(int display_channel)
{
    if (!stereo_input_available())
        return IM_COL32(255, 212, 0, 240);
    if (display_mode_is_split())
        return display_source_channel(display_channel) == mapped_left_source_channel() ? IM_COL32(255, 212, 0, 240) : IM_COL32(72, 192, 255, 240);
    if (gConfig.input_channel_mode == InputChannelMode::Right)
        return IM_COL32(72, 192, 255, 240);
    return IM_COL32(255, 212, 0, 240);
}

ImU32 display_channel_peak_color(int display_channel)
{
    if (!stereo_input_available())
        return IM_COL32(220, 64, 64, 220);
    if (display_mode_is_split())
        return display_source_channel(display_channel) == mapped_left_source_channel() ? IM_COL32(220, 64, 64, 220) : IM_COL32(64, 160, 220, 220);
    if (gConfig.input_channel_mode == InputChannelMode::Right)
        return IM_COL32(64, 160, 220, 220);
    return IM_COL32(220, 64, 64, 220);
}

void clear_frequency_gesture_frame()
{
    gFrequencyGestureFrame = ImRect();
    gFrequencyGestureFrameValid = false;
}

void reset_frequency_view_locked()
{
    gAxisFreqMin = gMinFreq;
    gAxisFreqMax = gMaxFreq;
}

void rebuild_scale_if_ready_locked()
{
    if (gProcessors[0] != nullptr && gFrequencyPlotWidth > 0)
        rebuild_scale_locked(gFrequencyPlotWidth);
}

void apply_frequency_view_locked(float min_freq, float max_freq, bool clear_waterfall)
{
    if (gProcessors[0] == nullptr)
        return;

    gAxisFreqMin = std::max(gMinFreq, std::min(min_freq, gMaxFreq));
    gAxisFreqMax = std::max(gAxisFreqMin + 1e-3f, std::min(max_freq, gMaxFreq));
    if (gAxisFreqMax > gMaxFreq)
    {
        const float span = gAxisFreqMax - gAxisFreqMin;
        gAxisFreqMax = gMaxFreq;
        gAxisFreqMin = std::max(gMinFreq, gAxisFreqMax - span);
    }

    rebuild_scale_if_ready_locked();
    if (clear_waterfall)
        Reset_waterfall_storage();
    refresh_display_state_locked(false);
}

bool update_frequency_view_from_pinch_locked(float previous_left_x, float previous_right_x, float current_left_x, float current_right_x)
{
    if (gProcessors[0] == nullptr)
        return false;

    const float previous_span = previous_right_x - previous_left_x;
    const float current_span = current_right_x - current_left_x;
    if (previous_span < kMinimumPinchDistanceNormalized || current_span < kMinimumPinchDistanceNormalized)
        return false;

    const FrequencyAxisScale curve = gConfig.frequency_axis_scale;
    const float full_min_scale = frequency_to_scale_value(curve, gMinFreq);
    const float full_max_scale = frequency_to_scale_value(curve, gMaxFreq);
    const float current_min_scale = frequency_to_scale_value(curve, gAxisFreqMin);
    const float current_max_scale = frequency_to_scale_value(curve, gAxisFreqMax);
    const float current_scale_span = current_max_scale - current_min_scale;
    if (current_scale_span <= 1e-6f)
        return false;

    const float anchored_left_scale = lerp(previous_left_x, current_min_scale, current_max_scale);
    const float anchored_right_scale = lerp(previous_right_x, current_min_scale, current_max_scale);

    float new_scale_span = (anchored_right_scale - anchored_left_scale) / current_span;
    const float minimum_frequency_span = std::max(25.0f, (gEffectiveSampleRate / (float)gConfig.fft_size) * 8.0f);
    const float minimum_scale_span = fabsf(
        frequency_to_scale_value(curve, std::min(gMaxFreq, gMinFreq + minimum_frequency_span)) -
        frequency_to_scale_value(curve, gMinFreq));
    const float maximum_scale_span = full_max_scale - full_min_scale;
    new_scale_span = std::max(minimum_scale_span, std::min(new_scale_span, maximum_scale_span));

    float new_min_scale = anchored_left_scale - current_left_x * new_scale_span;
    float new_max_scale = new_min_scale + new_scale_span;
    if (new_min_scale < full_min_scale)
    {
        new_max_scale += full_min_scale - new_min_scale;
        new_min_scale = full_min_scale;
    }
    if (new_max_scale > full_max_scale)
    {
        new_min_scale -= new_max_scale - full_max_scale;
        new_max_scale = full_max_scale;
    }
    new_min_scale = std::max(full_min_scale, new_min_scale);
    new_max_scale = std::min(full_max_scale, new_max_scale);
    if (new_max_scale - new_min_scale < minimum_scale_span * 0.999f)
        return false;

    const float new_min_freq = scale_value_to_frequency(curve, new_min_scale);
    const float new_max_freq = scale_value_to_frequency(curve, new_max_scale);
    if ((new_max_freq - new_min_freq) < minimum_frequency_span * 0.999f)
        return false;

    apply_frequency_view_locked(new_min_freq, new_max_freq, true);
    return true;
}

float power_to_db(float value)
{
    if (value <= 0.001f)
        value = 0.001f;
    return 20.0f * log10f(value / 32768.0f);
}

float db_to_power(float value_db)
{
    return 32768.0f * powf(10.0f, value_db / 20.0f);
}

float normalized_db(float value_db)
{
    return clamp(unlerp(gAxisYMin, gAxisYMax, value_db), 0.0f, 1.0f);
}

float overlay_text_font_size()
{
    return ImGui::GetFontSize() * gConfig.overlay_text_scale;
}

float overlay_text_alpha()
{
    return clamp(gConfig.overlay_text_alpha, 0.25f, 1.0f);
}

ImVec2 calc_overlay_text_size(const char *text)
{
    return ImGui::GetFont()->CalcTextSizeA(overlay_text_font_size(), FLT_MAX, 0.0f, text);
}

ImU32 apply_overlay_text_alpha(ImU32 color, float multiplier = 1.0f)
{
    ImVec4 color_vec = ImGui::ColorConvertU32ToFloat4(color);
    color_vec.w *= overlay_text_alpha() * multiplier;
    return ImGui::ColorConvertFloat4ToU32(color_vec);
}

void format_frequency(char *buffer, size_t buffer_size, float freq_hz)
{
    if (freq_hz >= 1000.0f)
        snprintf(buffer, buffer_size, "%.2f kHz", freq_hz / 1000.0f);
    else
        snprintf(buffer, buffer_size, "%.0f Hz", freq_hz);
}

int configured_sample_rate()
{
    if (gConfig.sampling_rate_mode == SamplingRateMode::Auto)
        return kDefaultAndroidSampleRate;
    return gConfig.sample_rate_hz;
}

float clamp_transform_interval_ms(float interval_ms)
{
    return clamp(interval_ms, 2.0f, 250.0f);
}

float interval_ms_to_scroll_speed_percent(float interval_ms)
{
    const float clamped_interval = clamp_transform_interval_ms(interval_ms);
    const float min_log = logf(2.0f);
    const float max_log = logf(250.0f);
    return ((max_log - logf(clamped_interval)) / (max_log - min_log)) * 100.0f;
}

float scroll_speed_percent_to_interval_ms(float speed_percent)
{
    const float clamped_speed = clamp(speed_percent, 0.0f, 100.0f);
    const float min_log = logf(2.0f);
    const float max_log = logf(250.0f);
    const float interval_log = max_log - ((max_log - min_log) * (clamped_speed / 100.0f));
    return expf(interval_log);
}

std::vector<int> sample_rate_fallbacks(int preferred_rate)
{
    std::vector<int> rates;
    const int known_rates[] = {192000, 176400, 96000, 88200, 48000, 44100, 32000, 24000, 16000, 8000};

    auto push_unique = [&rates](int value) {
        if (value <= 0)
            return;
        if (std::find(rates.begin(), rates.end(), value) == rates.end())
            rates.push_back(value);
    };

    push_unique(preferred_rate);
    for (int value : known_rates)
    {
        if (value <= preferred_rate)
            push_unique(value);
    }

    if (rates.empty())
        push_unique(kDefaultAndroidSampleRate);
    return rates;
}

int clamp_peak_marker_count(int count)
{
    if (count <= 0)
        return 0;
    if (count <= 1)
        return 1;
    if (count <= 3)
        return 3;
    return 5;
}

int map_audio_source_preset(AudioSourceMode mode)
{
#ifdef ANDROID
    switch (mode)
    {
    case AudioSourceMode::Default:
        return SL_ANDROID_RECORDING_PRESET_NONE;
    case AudioSourceMode::Generic:
        return SL_ANDROID_RECORDING_PRESET_GENERIC;
    case AudioSourceMode::VoiceRecognition:
        return SL_ANDROID_RECORDING_PRESET_VOICE_RECOGNITION;
    case AudioSourceMode::Camcorder:
        return SL_ANDROID_RECORDING_PRESET_CAMCORDER;
    case AudioSourceMode::Unprocessed:
    default:
        return SL_ANDROID_RECORDING_PRESET_UNPROCESSED;
    }
#else
    (void)mode;
    return 0;
#endif
}

int preferred_input_channel_count()
{
    return requested_input_channel_count_for_mode(gConfig.input_channel_mode);
}

void clear_peak_markers_locked(int channel = -1)
{
    if (channel < 0)
    {
        for (int i = 0; i < kMaxInputChannels; i++)
            clear_peak_markers_locked(i);
        return;
    }

    SharedState::ChannelState &state = channel_state(channel);
    for (PeakMarker &marker : state.peak_markers)
        marker = PeakMarker{};
    state.peak_marker_count = 0;
}

void clear_display_channel_locked(int channel)
{
    DisplayChannelState &state = display_channel_state(channel);
    state.live_power_raw.Resize(0);
    state.live_line.Resize(0);
    state.peak_hold_raw.Resize(0);
    state.peak_hold_line.Resize(0);
    for (PeakMarker &marker : state.peak_markers)
        marker = PeakMarker{};
    state.peak_marker_count = 0;
    state.peak_hold_valid = false;
}

void clear_display_channels_locked()
{
    for (int channel = 0; channel < kMaxInputChannels; channel++)
        clear_display_channel_locked(channel);
    gDisplayChannelCount = 1;
}

void average_buffers(const BufferIODouble &lhs, const BufferIODouble &rhs, BufferIODouble *out)
{
    const int size = std::min(lhs.GetSize(), rhs.GetSize());
    out->Resize(size);
    float *out_data = out->GetData();
    const float *lhs_data = lhs.GetData();
    const float *rhs_data = rhs.GetData();
    for (int i = 0; i < size; i++)
        out_data[i] = (lhs_data[i] + rhs_data[i]) * 0.5f;
}

float display_gain_linear()
{
    return powf(10.0f, clamp(gConfig.input_gain_db, -24.0f, 24.0f) / 20.0f);
}

void apply_gain_to_buffer(BufferIODouble *buffer, float gain_linear)
{
    if (buffer == nullptr || buffer->GetSize() <= 0 || fabsf(gain_linear - 1.0f) <= 1e-5f)
        return;

    float *data = buffer->GetData();
    for (int i = 0; i < buffer->GetSize(); i++)
        data[i] *= gain_linear;
}

void rebuild_display_channels_locked()
{
    clear_display_channels_locked();

    if (channel_state(0).live_power_raw.GetSize() <= 0)
        return;

    const float gain_linear = display_gain_linear();

    if (!stereo_input_available())
    {
        display_channel_state(0).live_power_raw.copy(&channel_state(0).live_power_raw);
        apply_gain_to_buffer(&display_channel_state(0).live_power_raw, gain_linear);
        if (channel_state(0).peak_hold_valid)
        {
            display_channel_state(0).peak_hold_raw.copy(&channel_state(0).peak_hold_raw);
            apply_gain_to_buffer(&display_channel_state(0).peak_hold_raw, gain_linear);
            display_channel_state(0).peak_hold_valid = true;
        }
        gDisplayChannelCount = 1;
        return;
    }

    switch (gConfig.input_channel_mode)
    {
    case InputChannelMode::Left:
    case InputChannelMode::Right:
    {
        const int source_channel = gConfig.input_channel_mode == InputChannelMode::Right
                                       ? mapped_right_source_channel()
                                       : mapped_left_source_channel();
        display_channel_state(0).live_power_raw.copy(&channel_state(source_channel).live_power_raw);
        apply_gain_to_buffer(&display_channel_state(0).live_power_raw, gain_linear);
        if (channel_state(source_channel).peak_hold_valid)
        {
            display_channel_state(0).peak_hold_raw.copy(&channel_state(source_channel).peak_hold_raw);
            apply_gain_to_buffer(&display_channel_state(0).peak_hold_raw, gain_linear);
            display_channel_state(0).peak_hold_valid = true;
        }
        gDisplayChannelCount = 1;
        break;
    }
    case InputChannelMode::StereoMixed:
        average_buffers(channel_state(0).live_power_raw, channel_state(1).live_power_raw, &display_channel_state(0).live_power_raw);
        apply_gain_to_buffer(&display_channel_state(0).live_power_raw, gain_linear);
        if (channel_state(0).peak_hold_valid && channel_state(1).peak_hold_valid)
        {
            average_buffers(channel_state(0).peak_hold_raw, channel_state(1).peak_hold_raw, &display_channel_state(0).peak_hold_raw);
            apply_gain_to_buffer(&display_channel_state(0).peak_hold_raw, gain_linear);
            display_channel_state(0).peak_hold_valid = true;
        }
        gDisplayChannelCount = 1;
        break;
    case InputChannelMode::StereoDifference:
        display_channel_state(0).live_power_raw.copy(&channel_state(0).live_power_raw);
        apply_gain_to_buffer(&display_channel_state(0).live_power_raw, gain_linear);
        if (channel_state(0).peak_hold_valid)
        {
            display_channel_state(0).peak_hold_raw.copy(&channel_state(0).peak_hold_raw);
            apply_gain_to_buffer(&display_channel_state(0).peak_hold_raw, gain_linear);
            display_channel_state(0).peak_hold_valid = true;
        }
        gDisplayChannelCount = 1;
        break;
    case InputChannelMode::StereoIndependent:
        for (int display_channel = 0; display_channel < 2; display_channel++)
        {
            const int source_channel = display_source_channel(display_channel);
            display_channel_state(display_channel).live_power_raw.copy(&channel_state(source_channel).live_power_raw);
            apply_gain_to_buffer(&display_channel_state(display_channel).live_power_raw, gain_linear);
            if (channel_state(source_channel).peak_hold_valid)
            {
                display_channel_state(display_channel).peak_hold_raw.copy(&channel_state(source_channel).peak_hold_raw);
                apply_gain_to_buffer(&display_channel_state(display_channel).peak_hold_raw, gain_linear);
                display_channel_state(display_channel).peak_hold_valid = true;
            }
        }
        gDisplayChannelCount = 2;
        break;
    case InputChannelMode::Mono:
    default:
        display_channel_state(0).live_power_raw.copy(&channel_state(0).live_power_raw);
        apply_gain_to_buffer(&display_channel_state(0).live_power_raw, gain_linear);
        if (channel_state(0).peak_hold_valid)
        {
            display_channel_state(0).peak_hold_raw.copy(&channel_state(0).peak_hold_raw);
            apply_gain_to_buffer(&display_channel_state(0).peak_hold_raw, gain_linear);
            display_channel_state(0).peak_hold_valid = true;
        }
        gDisplayChannelCount = 1;
        break;
    }
}

void clear_peak_hold_locked(int channel = -1)
{
    if (channel < 0)
    {
        for (int i = 0; i < kMaxInputChannels; i++)
            clear_peak_hold_locked(i);
        return;
    }

    SharedState::ChannelState &state = channel_state(channel);
    state.peak_hold_raw.Resize(0);
    state.peak_hold_line.Resize(0);
    state.peak_hold_valid = false;
}

void clear_reference_hold_locked()
{
    gSharedState.reference_hold_raw.Resize(0);
    gSharedState.reference_hold_line.Resize(0);
    gHoldingState = HOLDING_STATE_NO_HOLD;
}

void update_cursor_db_locked()
{
    for (int channel = 0; channel < kMaxInputChannels; channel++)
        gCursorDb[channel] = -120.0f;

    if (!gCursorActive || gProcessors[0] == nullptr)
        return;

    for (int channel = 0; channel < display_channel_count(); channel++)
    {
        const DisplayChannelState &state = display_channel_state(channel);
        if (state.live_power_raw.GetSize() <= 0)
            continue;

        int bin = static_cast<int>(roundf(gProcessors[0]->freq2Bin(gCursorFrequencyHz)));
        bin = std::max(1, std::min(bin, state.live_power_raw.GetSize() - 1));
        gCursorDb[channel] = power_to_db(state.live_power_raw.GetData()[bin]);
    }
}

void apply_frequency_axis_defaults_locked()
{
    gMinFreq = gProcessors[0] != nullptr ? gProcessors[0]->bin2Freq(1) : 0.0f;
    gMaxFreq = gProcessors[0] != nullptr ? gProcessors[0]->bin2Freq(gProcessors[0]->getBinCount() - 1) : 0.0f;
    reset_frequency_view_locked();
}

void rebuild_scale_locked(int outputWidth)
{
    if (outputWidth <= 0 || gProcessors[0] == nullptr)
        return;

    delete gScaleBufferX;
    gScaleBufferX = new ScaleBufferXCurve(gConfig.frequency_axis_scale);

    gScaleBufferX->setOutputWidth(outputWidth, gAxisFreqMin, gAxisFreqMax);
    gScaleBufferX->PreBuild(gProcessors[0]);
}

void generate_spectrum_lines_from_bin_data(BufferIODouble *pBins, BufferIODouble *pLines)
{
    if (pBins == nullptr || pLines == nullptr || gScaleBufferX == nullptr || gProcessors[0] == nullptr)
        return;

    float *pDataIn = pBins->GetData();
    pLines->Resize(2 * (pBins->GetSize() - 1));
    float *pDataOut = pLines->GetData();
    int i = 0;
    for (int bin = 1; bin < pBins->GetSize(); bin++)
    {
        pDataOut[2 * i + 0] = gScaleBufferX->FreqToX(gProcessors[0]->bin2Freq(bin));
        pDataOut[2 * i + 1] = pDataIn[bin];
        i++;
    }
}

void update_peak_markers_locked(int channel)
{
    DisplayChannelState &state = display_channel_state(channel);
    for (PeakMarker &marker : state.peak_markers)
        marker = PeakMarker{};
    state.peak_marker_count = 0;

    if (gProcessors[0] == nullptr || gScaleBufferX == nullptr || state.live_power_raw.GetSize() < 3)
        return;

    const int desired_count = clamp_peak_marker_count(gConfig.peak_marker_count);
    if (desired_count == 0)
        return;

    struct PeakCandidate
    {
        int bin;
        float value_db;
    };

    BufferIODouble *source = &state.live_power_raw;
    if (gConfig.peak_marker_source_mode == PeakMarkerSourceMode::ShortHold &&
        state.peak_hold_valid &&
        state.peak_hold_raw.GetSize() == state.live_power_raw.GetSize())
        source = &state.peak_hold_raw;

    const int first_bin = std::max(2, static_cast<int>(floorf(gProcessors[0]->freq2Bin(gAxisFreqMin))));
    const int last_bin = std::min(source->GetSize() - 2, static_cast<int>(ceilf(gProcessors[0]->freq2Bin(gAxisFreqMax))));
    const int minimum_bin_distance = std::max(4, source->GetSize() / 64);

    std::vector<PeakCandidate> candidates;
    float *raw = source->GetData();
    for (int bin = first_bin; bin <= last_bin; bin++)
    {
        float current = raw[bin];
        if (current < raw[bin - 1] || current < raw[bin + 1])
            continue;

        float value_db = power_to_db(current);
        if (value_db < gAxisYMin)
            continue;

        candidates.push_back({bin, value_db});
    }

    std::sort(candidates.begin(), candidates.end(), [](const PeakCandidate &lhs, const PeakCandidate &rhs) {
        return lhs.value_db > rhs.value_db;
    });

    int active_count = 0;
    for (const PeakCandidate &candidate : candidates)
    {
        bool too_close = false;
        for (int i = 0; i < active_count; i++)
        {
            if (abs(state.peak_markers[i].bin - candidate.bin) < minimum_bin_distance)
            {
                too_close = true;
                break;
            }
        }
        if (too_close)
            continue;

        PeakMarker &marker = state.peak_markers[active_count];
        marker.active = true;
        marker.bin = candidate.bin;
        marker.freq_hz = gProcessors[0]->bin2Freq(candidate.bin);
        marker.value_db = candidate.value_db;
        marker.normalized_x = gScaleBufferX->FreqToX(marker.freq_hz);
        marker.normalized_y = normalized_db(candidate.value_db);
        active_count++;
        if (active_count >= desired_count || active_count >= kPeakMarkerCapacity)
            break;
    }

    state.peak_marker_count = active_count;
}

void refresh_display_state_locked(bool updateWaterfall)
{
    if (gProcessors[0] == nullptr || gScaleBufferX == nullptr)
        return;

    rebuild_display_channels_locked();

    for (int channel = 0; channel < display_channel_count(); channel++)
    {
        DisplayChannelState &state = display_channel_state(channel);
        if (state.live_power_raw.GetSize() == 0)
            continue;

        applyScaleY(&state.live_power_raw, gAxisYMin, gAxisYMax, true, &gScaledPowerY[channel]);
        generate_spectrum_lines_from_bin_data(&gScaledPowerY[channel], &state.live_line);

        if (gConfig.max_hold_trace_enabled && state.peak_hold_valid)
        {
            applyScaleY(&state.peak_hold_raw, gAxisYMin, gAxisYMax, true, &gPeakScaledPowerY[channel]);
            generate_spectrum_lines_from_bin_data(&gPeakScaledPowerY[channel], &state.peak_hold_line);
        }
        else
        {
            state.peak_hold_line.Resize(0);
        }

        update_peak_markers_locked(channel);
    }

    if (gHoldingState == HOLDING_STATE_READY && gSharedState.reference_hold_raw.GetSize() > 0)
    {
        applyScaleY(&gSharedState.reference_hold_raw, gAxisYMin, gAxisYMax, true, &gReferenceScaledPowerY);
        generate_spectrum_lines_from_bin_data(&gReferenceScaledPowerY, &gSharedState.reference_hold_line);
    }
    else
    {
        gSharedState.reference_hold_line.Resize(0);
    }

    update_cursor_db_locked();

    if (updateWaterfall && waterfall_visible())
    {
        for (int channel = 0; channel < display_channel_count(); channel++)
        {
            if (gWaterfallWidth[channel] <= 0)
                continue;
            gScaleBufferX->Build(&gScaledPowerY[channel], &gScaledPowerXY[channel]);
            Draw_update(channel, gScaledPowerXY[channel].GetData(), gScaledPowerXY[channel].GetSize());
        }
    }
}

void stop_processing_session()
{
    gProcessingThreadStop.store(true);
    if (gProcessingThread.joinable())
        gProcessingThread.join();

    std::lock_guard<std::mutex> lock(gStateMutex);
    if (gProcessors[0] != nullptr)
    {
        gChunker.end();
        for (int channel = 0; channel < kMaxInputChannels; channel++)
        {
            delete gProcessors[channel];
            gProcessors[channel] = nullptr;
        }
        Audio_deinit();
    }
    gProcessingChannelCount = 1;
    gProcessingThreadStop.store(false);
}

void processing_loop()
{
    while (!gProcessingThreadStop.load())
    {
        int processed_frames = 0;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            while (gProcessors[0] != nullptr &&
                   gChunker.Process(
                       gProcessors,
                       processing_channel_count(),
                       gHopSamples,
                       mode_uses_stereo_difference(gConfig.input_channel_mode, active_input_channel_count())))
            {
                for (int channel = 0; channel < processing_channel_count(); channel++)
                {
                    SharedState::ChannelState &state = channel_state(channel);
                    gProcessors[channel]->computePower(gConfig.exponential_smoothing_factor);
                    BufferIODouble *power = gProcessors[channel]->getBufferIO();
                    state.live_power_raw.copy(power);

                    if (gConfig.max_hold_trace_enabled)
                    {
                        if (!state.peak_hold_valid || state.peak_hold_raw.GetSize() != power->GetSize())
                        {
                            state.peak_hold_raw.copy(power);
                            state.peak_hold_valid = true;
                        }
                        else
                        {
                            float *peak_data = state.peak_hold_raw.GetData();
                            float *live_data = power->GetData();
                            for (int i = 0; i < power->GetSize(); i++)
                            {
                                const float live_db = power_to_db(live_data[i]);
                                if (gConfig.peak_hold_falloff_seconds <= 0.0f)
                                {
                                    const float held_db = power_to_db(peak_data[i]);
                                    peak_data[i] = db_to_power(std::max(live_db, held_db));
                                }
                                else
                                {
                                    const float falloff_db = (90.0f * gAnalysisSecondsPerFrame) / gConfig.peak_hold_falloff_seconds;
                                    const float held_db = power_to_db(peak_data[i]) - falloff_db;
                                    peak_data[i] = db_to_power(std::max(live_db, held_db));
                                }
                            }
                        }
                    }
                    else
                    {
                        clear_peak_hold_locked(channel);
                    }
                }

                bool update_waterfall = false;
                if (gWaterfallSecondsPerRow <= gAnalysisSecondsPerFrame + 1e-6f)
                {
                    gWaterfallRowAccumulator = 0.0f;
                    update_waterfall = true;
                }
                else
                {
                    gWaterfallRowAccumulator += gAnalysisSecondsPerFrame;
                    if (gWaterfallRowAccumulator + 1e-6f >= gWaterfallSecondsPerRow)
                    {
                        gWaterfallRowAccumulator = fmodf(gWaterfallRowAccumulator, gWaterfallSecondsPerRow);
                        update_waterfall = true;
                    }
                }

                if (!gDisplayPaused)
                    refresh_display_state_locked(update_waterfall);

                processed_frames++;
                if (processed_frames >= kProcessingBurstLimit)
                    break;
            }
        }

        if (processed_frames == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void start_processing_session()
{
    stop_processing_session();

    std::lock_guard<std::mutex> lock(gStateMutex);

    const int preferred_sample_rate = configured_sample_rate();
    gRequestedInputChannels = preferred_input_channel_count();
    gActiveInputChannels = 1;
    gProcessingChannelCount = 1;
    gDisplayChannelCount = 1;
    gInputChannelsFallbackActive = false;
    bool audio_started = false;
    const int channel_candidates[2] = {gRequestedInputChannels, 1};
    const int channel_candidate_count = gRequestedInputChannels >= 2 ? 2 : 1;
    for (int candidate_index = 0; candidate_index < channel_candidate_count; candidate_index++)
    {
        const int candidate_channels = channel_candidates[candidate_index];

        for (int candidate_rate : sample_rate_fallbacks(preferred_sample_rate))
        {
            if (!Audio_init(static_cast<unsigned int>(candidate_rate), kAudioBufferLength, map_audio_source_preset(gConfig.audio_source_mode), candidate_channels))
                continue;
            if (!Audio_startPlay())
            {
                Audio_deinit();
                continue;
            }

            gInputSampleRate = static_cast<float>(candidate_rate);
            gActiveInputChannels = Audio_getInputChannelCount();
            gInputChannelsFallbackActive = gActiveInputChannels < gRequestedInputChannels;
            if (gConfig.sampling_rate_mode == SamplingRateMode::Fixed && gConfig.sample_rate_hz != candidate_rate)
            {
                gConfig.sample_rate_hz = candidate_rate;
                gConfigDirty = true;
            }
            audio_started = true;
            break;
        }
        if (audio_started)
            break;
    }

    if (!audio_started)
        return;

    gEffectiveSampleRate = gInputSampleRate / static_cast<float>(GetDecimationFactor(gConfig));
    const float analysis_interval_ms = std::min(clamp_transform_interval_ms(gConfig.desired_transform_interval_ms), 20.0f);
    gHopSamples = std::max(1, static_cast<int>(roundf((analysis_interval_ms / 1000.0f) * gInputSampleRate)));
    gAnalysisSecondsPerFrame = gHopSamples / gInputSampleRate;
    gWaterfallSecondsPerRow = clamp_transform_interval_ms(gConfig.desired_transform_interval_ms) / 1000.0f;
    gWaterfallRowAccumulator = 0.0f;

    AudioQueue *pFreeQueue = nullptr;
    AudioQueue *pRecQueue = nullptr;
    Audio_getBufferQueues(&pFreeQueue, &pRecQueue);
    gChunker.SetQueues(pRecQueue, pFreeQueue, gActiveInputChannels);
    gChunker.begin();
    gProcessingChannelCount = processing_channel_count_for_mode(gConfig.input_channel_mode, gActiveInputChannels);

    for (int channel = 0; channel < kMaxInputChannels; channel++)
    {
        delete gProcessors[channel];
        gProcessors[channel] = nullptr;
    }
    for (int channel = 0; channel < processing_channel_count(); channel++)
    {
        gProcessors[channel] = new myFFT();
        gProcessors[channel]->init(gConfig.fft_size, gInputSampleRate, GetDecimationFactor(gConfig), gConfig.window_function);
    }

    apply_frequency_axis_defaults_locked();
    if (gFrequencyPlotWidth > 0)
        rebuild_scale_locked(gFrequencyPlotWidth);

    for (int channel = 0; channel < processing_channel_count(); channel++)
    {
        channel_state(channel).live_power_raw.Resize(gProcessors[channel]->getBinCount());
        channel_state(channel).live_power_raw.clear();
        channel_state(channel).live_line.Resize(0);
    }
    for (int channel = processing_channel_count(); channel < kMaxInputChannels; channel++)
    {
        channel_state(channel).live_power_raw.Resize(0);
        channel_state(channel).live_line.Resize(0);
    }

    clear_peak_hold_locked();
    clear_peak_markers_locked();
    clear_display_channels_locked();
    Reset_waterfall_storage();

    gProcessingThreadStop.store(false);
    gProcessingThread = std::thread(processing_loop);
}

void restart_processing_session()
{
    start_processing_session();
}

void load_config_if_needed(void *window)
{
    if (gConfigLoaded)
        return;

#ifdef ANDROID
    android_app *app = static_cast<android_app *>(window);
    gWorkingDirectory = app->activity->internalDataPath;
    gConfigPath = gWorkingDirectory + "/spectrogrammer.cfg";
#else
    (void)window;
    gWorkingDirectory = ".";
    gConfigPath = "./spectrogrammer.cfg";
#endif

    AppConfig loaded = MakeDefaultAppConfig();
    if (LoadAppConfig(gConfigPath.c_str(), &loaded))
        gConfig = loaded;
    else
        SaveAppConfig(gConfigPath.c_str(), gConfig);

    gConfigLoaded = true;
    RefreshFiles(gWorkingDirectory.c_str());
}

void save_config_if_needed()
{
    if (!gConfigLoaded)
        return;

    if (gConfigDirty)
    {
        SaveAppConfig(gConfigPath.c_str(), gConfig);
        gConfigDirty = false;
    }
}

void mark_config_dirty()
{
    gConfigDirty = true;
    save_config_if_needed();
}

void apply_display_change(bool rebuildScale, bool clearWaterfall)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (rebuildScale)
        rebuild_scale_if_ready_locked();
    if (clearWaterfall)
        Reset_waterfall_storage();
    refresh_display_state_locked(false);
}

void copy_current_to_reference()
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (display_channel_state(0).live_power_raw.GetSize() == 0)
        return;

    gSharedState.reference_hold_raw.copy(&display_channel_state(0).live_power_raw);
    gHoldingState = HOLDING_STATE_READY;
    refresh_display_state_locked(false);
}

const char *audio_source_label(AudioSourceMode mode)
{
    switch (mode)
    {
    case AudioSourceMode::Default:
        return "默认";
    case AudioSourceMode::Generic:
        return "通用";
    case AudioSourceMode::VoiceRecognition:
        return "语音识别";
    case AudioSourceMode::Camcorder:
        return "摄像机";
    case AudioSourceMode::Unprocessed:
    default:
        return "未处理";
    }
}

const char *input_channel_label(InputChannelMode mode)
{
    switch (mode)
    {
    case InputChannelMode::Mono:
        return "单声道";
    case InputChannelMode::Left:
        return "左声道";
    case InputChannelMode::Right:
        return "右声道";
    case InputChannelMode::StereoMixed:
        return "双声道混合";
    case InputChannelMode::StereoDifference:
        return "双声道相减";
    case InputChannelMode::StereoIndependent:
    default:
        return "双声道独立";
    }
}

const char *input_gain_label(float gain_db)
{
    static char buffer[48];
    const float gain_linear = powf(10.0f, clamp(gain_db, -24.0f, 24.0f) / 20.0f);
    snprintf(buffer, sizeof(buffer), "%+.1f dB（%.2fx）", gain_db, gain_linear);
    return buffer;
}

const char *runtime_input_channel_status_label()
{
    static char buffer[64];
    if (gInputChannelsFallbackActive)
    {
        snprintf(buffer, sizeof(buffer), "实际单声道（双声道回退）");
        return buffer;
    }

    snprintf(buffer, sizeof(buffer), "实际%s", active_input_channel_count() >= 2 ? "双声道" : "单声道");
    return buffer;
}

const char *sampling_rate_label(SamplingRateMode mode, int sampleRate)
{
    static char buffer[64];
    if (mode == SamplingRateMode::Auto)
        snprintf(buffer, sizeof(buffer), "自动（%d Hz）", sampleRate);
    else
        snprintf(buffer, sizeof(buffer), "%d Hz", sampleRate);
    return buffer;
}

const char *window_function_label(WindowFunctionType type)
{
    switch (type)
    {
    case WindowFunctionType::Rectangular:
        return "矩形窗";
    case WindowFunctionType::Hann:
        return "汉宁窗";
    case WindowFunctionType::Hamming:
        return "汉明窗";
    case WindowFunctionType::BlackmanHarris:
    default:
        return "布莱克曼-哈里斯窗";
    }
}

const char *frequency_axis_label(FrequencyAxisScale scale)
{
    switch (scale)
    {
    case FrequencyAxisScale::Linear:
        return "线性";
    case FrequencyAxisScale::Music:
        return "音乐对数";
    case FrequencyAxisScale::Mel:
        return "Mel";
    case FrequencyAxisScale::Bark:
        return "Bark";
    case FrequencyAxisScale::ERB:
        return "ERB";
    case FrequencyAxisScale::Logarithmic:
    default:
        return "普通对数";
    }
}

const char *waterfall_size_label(WaterfallSizeMode mode)
{
    switch (mode)
    {
    case WaterfallSizeMode::OneQuarter:
        return "1/4 屏";
    case WaterfallSizeMode::OneThird:
        return "1/3 屏";
    case WaterfallSizeMode::TwoFifths:
        return "2/5 屏";
    case WaterfallSizeMode::OneHalf:
        return "1/2 屏";
    case WaterfallSizeMode::ThreeFifths:
        return "3/5 屏";
    case WaterfallSizeMode::TwoThirds:
        return "2/3 屏";
    case WaterfallSizeMode::ThreeQuarters:
        return "3/4 屏";
    default:
        return "2/3 屏";
    }
}

const char *fft_size_label(const AppConfig &config)
{
    static char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d（%.1f Hz/bin）", config.fft_size, GetEffectiveSampleRate(config) / (float)config.fft_size);
    return buffer;
}

const char *decimations_label(const AppConfig &config)
{
    static char buffer[64];
    snprintf(
        buffer,
        sizeof(buffer),
        "%d（上限 %.1f kHz，%.1f Hz/bin）",
        config.decimations,
        GetEffectiveSampleRate(config) * 0.5f / 1000.0f,
        GetEffectiveSampleRate(config) / (float)config.fft_size);
    return buffer;
}

const char *peak_hold_falloff_label(float seconds)
{
    static char buffer[48];
    if (seconds <= 0.0f)
        snprintf(buffer, sizeof(buffer), "不回落");
    else if (seconds >= 60.0f)
        snprintf(buffer, sizeof(buffer), "%.1f 分钟", seconds / 60.0f);
    else
        snprintf(buffer, sizeof(buffer), "%.1f 秒", seconds);
    return buffer;
}

const char *peak_marker_label(int count)
{
    static char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d 个", count);
    return buffer;
}

const char *peak_marker_source_label(PeakMarkerSourceMode mode)
{
    return mode == PeakMarkerSourceMode::ShortHold ? "短时峰值" : "实时峰值";
}

void render_section_title(const char *title)
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.18f, 1.0f), "%s", title);
    ImGui::Separator();
}

void render_setting_label(const char *title)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
}

void render_about_section()
{
    render_section_title("关于");
    ImGui::TextWrapped("Spectrogrammer 的中文移动版 fork，当前仓库维护在 GitHub。");
    ImGui::TextWrapped("仓库：%s", kGitHubRepositoryUrl);
    if (ImGui::Button("打开 GitHub 仓库", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
        open_url_external(kGitHubRepositoryUrl);
#ifndef ANDROID
    ImGui::TextDisabled("当前平台仅显示仓库链接，不直接拉起浏览器。");
#endif
}

void update_settings_drag_scroll()
{
    ImGuiIO &io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (hovered && ImGui::IsMouseClicked(0))
    {
        gSettingsScrollTracking = true;
        gSettingsScrollDragging = false;
        gSettingsScrollStartMousePos = io.MousePos;
        gSettingsScrollStartY = ImGui::GetScrollY();
    }

    if (!ImGui::IsMouseDown(0))
    {
        gSettingsScrollTracking = false;
        gSettingsScrollDragging = false;
        return;
    }

    if (!gSettingsScrollTracking)
        return;

    const ImVec2 drag_delta(
        io.MousePos.x - gSettingsScrollStartMousePos.x,
        io.MousePos.y - gSettingsScrollStartMousePos.y);
    if (!gSettingsScrollDragging)
    {
        if (fabsf(drag_delta.y) < 8.0f || fabsf(drag_delta.y) <= fabsf(drag_delta.x))
            return;
        gSettingsScrollDragging = true;
        ImGui::ClearActiveID();
    }

    const float next_scroll = clamp(gSettingsScrollStartY - drag_delta.y, 0.0f, ImGui::GetScrollMaxY());
    if (fabsf(next_scroll - ImGui::GetScrollY()) > 0.01f)
        ImGui::SetScrollY(next_scroll);
}

void render_page_header(const char *title)
{
    ImGui::Dummy(ImVec2(0.0f, top_safe_padding()));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 16.0f));
    if (ImGui::Button("返回主界面", ImVec2(std::min(ImGui::GetContentRegionAvail().x * 0.45f, 320.0f), large_button_height())))
        gSettingsPage = SettingsPage::None;
    ImGui::PopStyleVar();
    ImGui::Spacing();
    ImGui::Text("%s", title);
    ImGui::Spacing();
    ImGui::Separator();
}

void begin_settings_page_layout(const char *title)
{
    const float side_margin = settings_side_margin();
    ImGui::SetCursorPosX(side_margin);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(24.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(18.0f, 20.0f));
    ImGui::BeginChild(
        title,
        ImVec2(
            std::max(0.0f, avail.x - side_margin),
            std::max(0.0f, avail.y)),
        false,
        ImGuiWindowFlags_NoScrollbar);
    render_page_header(title);
}

void end_settings_page_layout()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
}

void render_settings_page()
{
    begin_settings_page_layout("设置");

    render_section_title("音频");
    render_setting_label("音频源");
    set_full_width_item();
    if (ImGui::BeginCombo("##audio_source", audio_source_label(gConfig.audio_source_mode)))
    {
        const AudioSourceMode items[] = {
            AudioSourceMode::Default,
            AudioSourceMode::Generic,
            AudioSourceMode::VoiceRecognition,
            AudioSourceMode::Camcorder,
            AudioSourceMode::Unprocessed,
        };
        for (AudioSourceMode mode : items)
        {
            const bool selected = gConfig.audio_source_mode == mode;
            if (ImGui::Selectable(audio_source_label(mode), selected))
            {
                gConfig.audio_source_mode = mode;
                mark_config_dirty();
                restart_processing_session();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    render_setting_label("声道模式");
    set_full_width_item();
    if (ImGui::BeginCombo("##input_channel_mode", input_channel_label(gConfig.input_channel_mode)))
    {
        const InputChannelMode items[] = {
            InputChannelMode::Mono,
            InputChannelMode::Left,
            InputChannelMode::Right,
            InputChannelMode::StereoMixed,
            InputChannelMode::StereoDifference,
            InputChannelMode::StereoIndependent,
        };
        for (InputChannelMode mode : items)
        {
            const bool selected = gConfig.input_channel_mode == mode;
            if (ImGui::Selectable(input_channel_label(mode), selected))
            {
                const bool requires_restart = input_channel_mode_change_requires_restart(gConfig.input_channel_mode, mode);
                gConfig.input_channel_mode = mode;
                mark_config_dirty();
                if (requires_restart)
                    restart_processing_session();
                else
                    apply_display_change(false, true);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%s", runtime_input_channel_status_label());

    if (gConfig.input_channel_mode == InputChannelMode::StereoIndependent)
    {
        bool swap_stereo_order = gConfig.swap_stereo_order;
        if (ImGui::Checkbox("交换左右顺序", &swap_stereo_order))
        {
            gConfig.swap_stereo_order = swap_stereo_order;
            mark_config_dirty();
            apply_display_change(false, true);
        }
    }

    render_setting_label("输入增益");
    float input_gain_db = gConfig.input_gain_db;
    set_full_width_item();
    if (ImGui::SliderFloat("##input_gain_db", &input_gain_db, -24.0f, 24.0f, "%.1f dB"))
    {
        gConfig.input_gain_db = roundf(input_gain_db * 2.0f) * 0.5f;
        mark_config_dirty();
        apply_display_change(false, false);
    }
    ImGui::TextDisabled("负值减小，正值放大：%s", input_gain_label(gConfig.input_gain_db));

    render_setting_label("采样率");
    set_full_width_item();
    if (ImGui::BeginCombo("##sampling_rate", sampling_rate_label(gConfig.sampling_rate_mode, configured_sample_rate())))
    {
        if (ImGui::Selectable("自动（48000 Hz）", gConfig.sampling_rate_mode == SamplingRateMode::Auto))
        {
            gConfig.sampling_rate_mode = SamplingRateMode::Auto;
            gConfig.sample_rate_hz = kDefaultAndroidSampleRate;
            mark_config_dirty();
            restart_processing_session();
        }

        const int sample_rates[] = {384000, 192000, 176400, 96000, 88200, 48000, 44100, 32000, 24000, 16000, 8000};
        for (int value : sample_rates)
        {
            char label[32];
            snprintf(label, sizeof(label), "%d Hz", value);
            const bool selected = gConfig.sampling_rate_mode == SamplingRateMode::Fixed && gConfig.sample_rate_hz == value;
            if (ImGui::Selectable(label, selected))
            {
                gConfig.sampling_rate_mode = SamplingRateMode::Fixed;
                gConfig.sample_rate_hz = value;
                mark_config_dirty();
                restart_processing_session();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("高采样率取决于设备和驱动支持，不支持时可能无法启动该档位。");

    render_section_title("处理");
    render_setting_label("FFT 大小");
    set_full_width_item();
    if (ImGui::BeginCombo("##fft_size", fft_size_label(gConfig)))
    {
        const int fft_sizes[] = {8192, 4096, 2048, 1024, 512, 256, 128};
        for (int value : fft_sizes)
        {
            char label[64];
            snprintf(label, sizeof(label), "%d（%.1f Hz/bin）", value, GetEffectiveSampleRate(gConfig) / (float)value);
            const bool selected = gConfig.fft_size == value;
            if (ImGui::Selectable(label, selected))
            {
                gConfig.fft_size = value;
                mark_config_dirty();
                restart_processing_session();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    render_setting_label("抽取级数");
    set_full_width_item();
    if (ImGui::BeginCombo("##decimations", decimations_label(gConfig)))
    {
        for (int value = 0; value <= 5; value++)
        {
            char label[64];
            const float dc_resolution = (configured_sample_rate() / (float)(1 << value)) / (float)gConfig.fft_size;
            const float max_freq_khz = (configured_sample_rate() / (float)(1 << value)) * 0.5f / 1000.0f;
            snprintf(label, sizeof(label), "%d（上限 %.1f kHz，%.1f Hz/bin）", value, max_freq_khz, dc_resolution);
            const bool selected = gConfig.decimations == value;
            if (ImGui::Selectable(label, selected))
            {
                gConfig.decimations = value;
                mark_config_dirty();
                restart_processing_session();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    render_setting_label("窗函数");
    set_full_width_item();
    if (ImGui::BeginCombo("##window_function", window_function_label(gConfig.window_function)))
    {
        const WindowFunctionType items[] = {
            WindowFunctionType::Rectangular,
            WindowFunctionType::Hann,
            WindowFunctionType::Hamming,
            WindowFunctionType::BlackmanHarris,
        };
        for (WindowFunctionType type : items)
        {
            const bool selected = gConfig.window_function == type;
            if (ImGui::Selectable(window_function_label(type), selected))
            {
                gConfig.window_function = type;
                mark_config_dirty();
                restart_processing_session();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    float smoothing = gConfig.exponential_smoothing_factor;
    render_setting_label("指数平滑因子（越大越平滑）");
    set_full_width_item();
    if (ImGui::SliderFloat("##smoothing_factor", &smoothing, 0.0f, 0.95f, "%.2f"))
    {
        gConfig.exponential_smoothing_factor = smoothing;
        mark_config_dirty();
    }

    float overlay_scale = gConfig.overlay_text_scale;
    render_setting_label("图上标注字号");
    set_full_width_item();
    if (ImGui::SliderFloat("##overlay_text_scale", &overlay_scale, 0.7f, 1.8f, "%.2f 倍"))
    {
        gConfig.overlay_text_scale = overlay_scale;
        mark_config_dirty();
    }

    float overlay_alpha = gConfig.overlay_text_alpha;
    render_setting_label("图上标注透明度");
    set_full_width_item();
    if (ImGui::SliderFloat("##overlay_text_alpha", &overlay_alpha, 0.25f, 1.0f, "%.2f"))
    {
        gConfig.overlay_text_alpha = overlay_alpha;
        mark_config_dirty();
    }

    render_section_title("频谱");
    render_setting_label("频率轴刻度");
    set_full_width_item();
    if (ImGui::BeginCombo("##frequency_axis_scale", frequency_axis_label(gConfig.frequency_axis_scale)))
    {
        const FrequencyAxisScale items[] = {
            FrequencyAxisScale::Linear,
            FrequencyAxisScale::Logarithmic,
            FrequencyAxisScale::Music,
            FrequencyAxisScale::Mel,
            FrequencyAxisScale::Bark,
            FrequencyAxisScale::ERB,
        };
        for (FrequencyAxisScale scale : items)
        {
            const bool selected = gConfig.frequency_axis_scale == scale;
            if (ImGui::Selectable(frequency_axis_label(scale), selected))
            {
                gConfig.frequency_axis_scale = scale;
                mark_config_dirty();
                apply_display_change(true, true);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    bool max_hold = gConfig.max_hold_trace_enabled;
    if (ImGui::Checkbox("显示峰值保持曲线", &max_hold))
    {
        gConfig.max_hold_trace_enabled = max_hold;
        if (!max_hold)
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            clear_peak_hold_locked();
            refresh_display_state_locked(false);
        }
        mark_config_dirty();
    }

    if (gConfig.max_hold_trace_enabled)
    {
        float hold_falloff_seconds = gConfig.peak_hold_falloff_seconds;
        render_setting_label("峰值回落时长");
        set_full_width_item();
        if (ImGui::SliderFloat("##peak_hold_falloff", &hold_falloff_seconds, 0.0f, 120.0f, "%.1f 秒"))
        {
            gConfig.peak_hold_falloff_seconds = hold_falloff_seconds;
            mark_config_dirty();
        }
        ImGui::TextDisabled("当前：%s", peak_hold_falloff_label(gConfig.peak_hold_falloff_seconds));
    }

    render_setting_label("峰值标记");
    set_full_width_item();
    if (ImGui::BeginCombo("##peak_markers", peak_marker_label(gConfig.peak_marker_count)))
    {
        const int marker_counts[] = {0, 1, 3, 5};
        for (int count : marker_counts)
        {
            char label[32];
            snprintf(label, sizeof(label), "%d 个", count);
            const bool selected = gConfig.peak_marker_count == count;
            if (ImGui::Selectable(label, selected))
            {
                gConfig.peak_marker_count = count;
                mark_config_dirty();
                apply_display_change(false, false);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    render_setting_label("峰值标记来源");
    set_full_width_item();
    if (ImGui::BeginCombo("##peak_marker_source", peak_marker_source_label(gConfig.peak_marker_source_mode)))
    {
        const PeakMarkerSourceMode items[] = {
            PeakMarkerSourceMode::Live,
            PeakMarkerSourceMode::ShortHold,
        };
        for (PeakMarkerSourceMode mode : items)
        {
            const bool selected = gConfig.peak_marker_source_mode == mode;
            if (ImGui::Selectable(peak_marker_source_label(mode), selected))
            {
                gConfig.peak_marker_source_mode = mode;
                mark_config_dirty();
                apply_display_change(false, false);
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    render_section_title("瀑布图");
    bool show_spectrum = gConfig.show_spectrum;
    if (ImGui::Checkbox("显示上方频谱图", &show_spectrum))
    {
        gConfig.show_spectrum = show_spectrum;
        if (!gConfig.show_spectrum && !gConfig.show_waterfall)
            gConfig.show_waterfall = true;
        mark_config_dirty();
        apply_display_change(false, false);
    }

    bool show_waterfall = gConfig.show_waterfall;
    if (ImGui::Checkbox("显示瀑布图", &show_waterfall))
    {
        gConfig.show_waterfall = show_waterfall;
        if (!gConfig.show_spectrum && !gConfig.show_waterfall)
            gConfig.show_spectrum = true;
        mark_config_dirty();
        apply_display_change(false, false);
    }

    render_setting_label("瀑布图高度");
    set_full_width_item();
    if (ImGui::BeginCombo("##waterfall_size", waterfall_size_label(gConfig.waterfall_size_mode)))
    {
        const WaterfallSizeMode items[] = {
            WaterfallSizeMode::OneQuarter,
            WaterfallSizeMode::OneThird,
            WaterfallSizeMode::TwoFifths,
            WaterfallSizeMode::OneHalf,
            WaterfallSizeMode::ThreeFifths,
            WaterfallSizeMode::TwoThirds,
            WaterfallSizeMode::ThreeQuarters,
        };
        for (WaterfallSizeMode mode : items)
        {
            const bool selected = gConfig.waterfall_size_mode == mode;
            if (ImGui::Selectable(waterfall_size_label(mode), selected))
            {
                gConfig.waterfall_size_mode = mode;
                mark_config_dirty();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    float scroll_speed_percent = interval_ms_to_scroll_speed_percent(gConfig.desired_transform_interval_ms);
    render_setting_label("滚动速度（慢到快）");
    set_full_width_item();
    if (ImGui::SliderFloat("##transform_interval", &scroll_speed_percent, 0.0f, 100.0f, "%.0f%%"))
    {
        gConfig.desired_transform_interval_ms = scroll_speed_percent_to_interval_ms(scroll_speed_percent);
        mark_config_dirty();
        restart_processing_session();
    }
    ImGui::TextDisabled("当前约 %.0f ms/行", gConfig.desired_transform_interval_ms);

    render_section_title("运行");
    bool background_capture = gConfig.background_capture_enabled;
    if (ImGui::Checkbox("后台采集", &background_capture))
    {
        gConfig.background_capture_enabled = background_capture;
        mark_config_dirty();
    }

    bool stay_awake = gConfig.stay_awake;
    if (ImGui::Checkbox("保持亮屏", &stay_awake))
    {
        gConfig.stay_awake = stay_awake;
        mark_config_dirty();
    }

    render_about_section();
    update_settings_drag_scroll();
    end_settings_page_layout();
}

void draw_toolbar()
{
    ImGui::Dummy(ImVec2(0.0f, top_safe_padding()));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(18.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

    const float row_height = std::max(68.0f, large_button_height() * 0.76f);
    const float width = ImGui::GetContentRegionAvail().x;
    const float button_width = (width - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;

    if (ImGui::Button(gDisplayPaused ? "继续" : "暂停", ImVec2(button_width, row_height)))
        gDisplayPaused = !gDisplayPaused;

    ImGui::SameLine();
    if (ImGui::Button("清除峰值", ImVec2(button_width, row_height)))
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        clear_peak_hold_locked();
        refresh_display_state_locked(false);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!gCursorActive);
    if (ImGui::Button("清除游标", ImVec2(button_width, row_height)))
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gCursorActive = false;
        gCursorDragging = false;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("设置", ImVec2(button_width, row_height)))
        gSettingsPage = SettingsPage::Settings;
    ImGui::PopStyleVar(3);

    float axis_min = 0.0f;
    float axis_max = 0.0f;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        axis_min = gAxisFreqMin;
        axis_max = gAxisFreqMax;
    }
    char axis_min_label[32];
    char axis_max_label[32];
    char channel_status[64];
    char status[256];
    format_frequency(axis_min_label, sizeof(axis_min_label), axis_min);
    format_frequency(axis_max_label, sizeof(axis_max_label), axis_max);
    snprintf(
        channel_status,
        sizeof(channel_status),
        "%s",
        gInputChannelsFallbackActive ? "单声道（双声道回退）" : (active_input_channel_count() >= 2 ? "双声道" : "单声道"));
    snprintf(
        status,
        sizeof(status),
        "输入 %.0f Hz / %s / %s  ·  视图 %s - %s  ·  FFT %d  ·  %.1f Hz/bin",
        gInputSampleRate,
        input_channel_label(gConfig.input_channel_mode),
        channel_status,
        axis_min_label,
        axis_max_label,
        gConfig.fft_size,
        gEffectiveSampleRate / (float)gConfig.fft_size);
    ImGui::TextDisabled("%s", status);
    ImGui::SetCursorPosY(std::max(0.0f, ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void draw_hold_popup()
{
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.92f, -1));
    if (!ImGui::BeginPopupModal("对比菜单", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        gCloseHoldPopupRequested = false;
        return;
    }

    if (gCloseHoldPopupRequested)
    {
        gCloseHoldPopupRequested = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    float loaded_sample_rate = gInputSampleRate;
    uint32_t loaded_fft_size = static_cast<uint32_t>(gConfig.fft_size);
    if (HoldPicker(gWorkingDirectory.c_str(), gHoldingState == HOLDING_STATE_READY, &gSharedState.reference_hold_raw, &loaded_sample_rate, &loaded_fft_size))
    {
        gConfig.sampling_rate_mode = SamplingRateMode::Fixed;
        gConfig.sample_rate_hz = static_cast<int>(loaded_sample_rate);
        gConfig.fft_size = static_cast<int>(loaded_fft_size);
        gHoldingState = HOLDING_STATE_READY;
        mark_config_dirty();
        restart_processing_session();
    }

    if (ImGui::Button("记录当前曲线"))
        copy_current_to_reference();

    ImGui::SameLine();
    if (ImGui::Button("清除基线"))
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        clear_reference_hold_locked();
        refresh_display_state_locked(false);
    }

    ImGui::Separator();
    if (ImGui::Button("关闭"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void update_cursor_state(const ImRect &spectrumFrame, bool spectrumHovered, const ImRect *waterfallFrames, const bool *waterfallHovered, int waterfallCount)
{
    if (gPinchGesture.active)
    {
        gCursorDragging = false;
        return;
    }

    bool hovered = spectrumHovered;
    int hoveredWaterfallIndex = -1;
    for (int channel = 0; channel < waterfallCount; channel++)
    {
        if (waterfallHovered[channel])
        {
            hovered = true;
            hoveredWaterfallIndex = channel;
            break;
        }
    }
    const bool mouseDown = ImGui::IsMouseDown(0);

    if (hovered && ImGui::IsMouseClicked(0))
        gCursorDragging = true;
    else if (!mouseDown)
        gCursorDragging = false;

    if (!gCursorDragging)
        return;

    const ImRect &activeFrame = spectrumHovered || hoveredWaterfallIndex < 0 ? spectrumFrame : waterfallFrames[hoveredWaterfallIndex];
    const float x = clamp(unlerp(activeFrame.Min.x, activeFrame.Max.x, ImGui::GetIO().MousePos.x), 0.0f, 1.0f);
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        if (gScaleBufferX != nullptr)
        {
            gCursorActive = true;
            gCursorFrequencyHz = gScaleBufferX->XtoFreq(x);
            update_cursor_db_locked();
        }
    }
}

void draw_peak_markers(const ImRect &frame_bb)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    const float font_size = overlay_text_font_size();
    for (int channel = 0; channel < display_channel_count(); channel++)
    {
        const DisplayChannelState &state = display_channel_state(channel);
        const ImU32 marker_color = display_channel_live_color(channel);
        const ImU32 text_color = apply_overlay_text_alpha(marker_color);
        for (int i = 0; i < state.peak_marker_count; i++)
        {
            const PeakMarker &marker = state.peak_markers[i];
            if (!marker.active)
                continue;

            const float x = lerp(marker.normalized_x, frame_bb.Min.x, frame_bb.Max.x);
            const float y = lerp(marker.normalized_y, frame_bb.Max.y, frame_bb.Min.y);
            char label[112];
            char freq[48];
            format_frequency(freq, sizeof(freq), marker.freq_hz);
            if (display_mode_is_split())
                snprintf(label, sizeof(label), "%s %s\n%.0f dB", mapped_source_channel_title(display_source_channel(channel)), freq, marker.value_db);
            else
                snprintf(label, sizeof(label), "%s\n%.0f dB", freq, marker.value_db);
            const ImVec2 label_size = calc_overlay_text_size(label);
            ImVec2 labelPos(x + 8.0f, std::max(frame_bb.Min.y + 8.0f, y - label_size.y - (float)(i + channel * 2) * (font_size * 0.55f)));

            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, frame_bb.Min.y), ImVec2(x, frame_bb.Max.y), marker_color, 1.0f);
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(x, y), 5.0f, marker_color);
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), font_size, labelPos, text_color, label);
        }
    }
}

void draw_cursor(const ImRect &spectrumFrame, const ImRect *waterfallFrames, int waterfallCount)
{
    std::lock_guard<std::mutex> lock(gStateMutex);
    if (!gCursorActive || gScaleBufferX == nullptr)
        return;

    char freqLabel[48];
    char dbLabels[kMaxInputChannels][48];
    format_frequency(freqLabel, sizeof(freqLabel), gCursorFrequencyHz);
    for (int channel = 0; channel < display_channel_count(); channel++)
    {
        const char *channel_label = display_channel_title(channel);
        if (channel_label != nullptr && channel_label[0] != '\0')
            snprintf(dbLabels[channel], sizeof(dbLabels[channel]), "%s %.1f dB", channel_label, gCursorDb[channel]);
        else
            snprintf(dbLabels[channel], sizeof(dbLabels[channel]), "%.1f dB", gCursorDb[channel]);
    }

    const float normalized_x = gScaleBufferX->FreqToX(gCursorFrequencyHz);
    if (normalized_x < 0.0f || normalized_x > 1.0f)
        return;

    ImRect anchorFrame = spectrumFrame;
    if (anchorFrame.GetWidth() <= 0.0f || anchorFrame.GetHeight() <= 0.0f)
    {
        for (int channel = 0; channel < waterfallCount; channel++)
        {
            if (waterfallFrames[channel].GetWidth() > 0.0f && waterfallFrames[channel].GetHeight() > 0.0f)
            {
                anchorFrame = waterfallFrames[channel];
                break;
            }
        }
    }
    if (anchorFrame.GetWidth() <= 0.0f || anchorFrame.GetHeight() <= 0.0f)
        return;

    const float anchor_x = lerp(normalized_x, anchorFrame.Min.x, anchorFrame.Max.x);
    const ImU32 cursorColor = IM_COL32(70, 255, 180, 255);
    const float font_size = overlay_text_font_size();
    const ImVec2 freqSize = calc_overlay_text_size(freqLabel);
    float boxWidth = freqSize.x + 16.0f;
    float boxHeight = freqSize.y + 12.0f;
    ImVec2 dbSizes[kMaxInputChannels];
    for (int channel = 0; channel < display_channel_count(); channel++)
    {
        dbSizes[channel] = calc_overlay_text_size(dbLabels[channel]);
        boxWidth = std::max(boxWidth, dbSizes[channel].x + 16.0f);
        boxHeight += dbSizes[channel].y + 4.0f;
    }
    boxHeight += 6.0f;
    float boxX = anchor_x + 10.0f;
    if (boxX + boxWidth > anchorFrame.Max.x - 4.0f)
        boxX = anchor_x - boxWidth - 10.0f;
    boxX = std::max(anchorFrame.Min.x + 4.0f, std::min(boxX, anchorFrame.Max.x - boxWidth - 4.0f));
    const float boxY = anchorFrame.Min.y + 8.0f;

    if (spectrumFrame.GetWidth() > 0.0f && spectrumFrame.GetHeight() > 0.0f)
    {
        const float spectrum_x = lerp(normalized_x, spectrumFrame.Min.x, spectrumFrame.Max.x);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(spectrum_x, spectrumFrame.Min.y), ImVec2(spectrum_x, spectrumFrame.Max.y), cursorColor, 1.5f);
    }
    for (int channel = 0; channel < waterfallCount; channel++)
    {
        if (waterfallFrames[channel].GetWidth() <= 0.0f || waterfallFrames[channel].GetHeight() <= 0.0f)
            continue;
        const float waterfall_x = lerp(normalized_x, waterfallFrames[channel].Min.x, waterfallFrames[channel].Max.x);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(waterfall_x, waterfallFrames[channel].Min.y), ImVec2(waterfall_x, waterfallFrames[channel].Max.y), cursorColor, 1.5f);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(boxX, boxY),
        ImVec2(boxX + boxWidth, boxY + boxHeight),
        IM_COL32(10, 18, 26, 150),
        6.0f);
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(boxX, boxY),
        ImVec2(boxX + boxWidth, boxY + boxHeight),
        IM_COL32(70, 255, 180, 110),
        6.0f);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), font_size, ImVec2(boxX + 8.0f, boxY + 5.0f), apply_overlay_text_alpha(cursorColor), freqLabel);
    float textY = boxY + 9.0f + freqSize.y;
    for (int channel = 0; channel < display_channel_count(); channel++)
    {
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            font_size,
            ImVec2(boxX + 8.0f, textY),
            apply_overlay_text_alpha(display_channel_live_color(channel)),
            dbLabels[channel]);
        textY += dbSizes[channel].y + 4.0f;
    }
}

void draw_spectrum(const ImRect &frame_bb)
{
    BufferIODouble liveLines[kMaxInputChannels];
    BufferIODouble peakLines[kMaxInputChannels];
    BufferIODouble referenceLine;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        for (int channel = 0; channel < display_channel_count(); channel++)
        {
            liveLines[channel].copy(&display_channel_state(channel).live_line);
            peakLines[channel].copy(&display_channel_state(channel).peak_hold_line);
        }
        referenceLine.copy(&gSharedState.reference_hold_line);
    }

    for (int channel = 0; channel < display_channel_count(); channel++)
    {
        if (liveLines[channel].GetSize() > 0)
            draw_lines(frame_bb, liveLines[channel].GetData(), liveLines[channel].GetSize() / 2, display_channel_live_color(channel), 0, 1);
        if (gConfig.max_hold_trace_enabled && peakLines[channel].GetSize() > 0)
            draw_lines(frame_bb, peakLines[channel].GetData(), peakLines[channel].GetSize() / 2, display_channel_peak_color(channel), 0, 1);
    }
    if (referenceLine.GetSize() > 0)
        draw_lines(frame_bb, referenceLine.GetData(), referenceLine.GetSize() / 2, IM_COL32(128, 64, 220, 220), 0, 1);

    if (gScaleBufferX != nullptr)
        draw_frequency_scale(frame_bb, gScaleBufferX, gConfig.frequency_axis_scale);
    draw_scale_y(frame_bb, gAxisYMin, gAxisYMax);
    draw_peak_markers(frame_bb);

    if (display_mode_is_split())
    {
        float legend_x = frame_bb.Min.x + 10.0f;
        const float font_size = overlay_text_font_size();
        for (int channel = 0; channel < display_channel_count(); channel++)
        {
            const char *label = display_channel_title(channel);
            if (label == nullptr || label[0] == '\0')
                continue;
            ImGui::GetWindowDrawList()->AddText(
                ImGui::GetFont(),
                font_size,
                ImVec2(legend_x, frame_bb.Min.y + 8.0f),
                apply_overlay_text_alpha(display_channel_live_color(channel)),
                label);
            legend_x += calc_overlay_text_size(label).x + 20.0f;
        }
    }
}

void render_main_screen()
{
    draw_toolbar();

    clear_frequency_gesture_frame();
    const float total_height = ImGui::GetContentRegionAvail().y;
    const bool show_spectrum = spectrum_visible();
    const bool show_waterfall = waterfall_visible();
    const float plot_width = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    float waterfall_height = 0.0f;
    float spectrum_height = 0.0f;
    if (show_spectrum && show_waterfall)
    {
        waterfall_height = std::max(180.0f, total_height * GetWaterfallFraction(gConfig));
        spectrum_height = std::max(kSpectrumMinimumHeight, total_height - waterfall_height - 12.0f);
    }
    else if (show_spectrum)
    {
        spectrum_height = std::max(kSpectrumMinimumHeight, total_height);
    }
    else if (show_waterfall)
    {
        waterfall_height = total_height;
    }
    const int waterfallPanels = show_waterfall ? display_channel_count() : 0;
    const float waterfallGap = 0.0f;
    const float waterfallPanelHeight = waterfallPanels > 0 ? std::max(80.0f, (waterfall_height - waterfallGap * (waterfallPanels - 1)) / (float)waterfallPanels) : 0.0f;

    ImRect spectrumFrame = ImRect();
    ImRect waterfallFrames[kMaxInputChannels];
    bool spectrumHovered = false;
    bool waterfallHovered[kMaxInputChannels] = {false, false};

    if (show_spectrum && block_add("spectrum_frame", ImVec2(plot_width, spectrum_height), &spectrumFrame, &spectrumHovered))
    {
        const ImGuiStyle &style = GImGui->Style;
        ImGui::RenderFrame(spectrumFrame.Min, spectrumFrame.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
        const int width = static_cast<int>(spectrumFrame.GetWidth());
        if (width != gFrequencyPlotWidth)
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gFrequencyPlotWidth = width;
            rebuild_scale_if_ready_locked();
            refresh_display_state_locked(false);
        }
    }

    if (show_spectrum && show_waterfall)
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

    for (int channel = 0; channel < waterfallPanels; channel++)
    {
        char frame_id[32];
        snprintf(frame_id, sizeof(frame_id), "waterfall_frame_%d", channel);
        if (!block_add(frame_id, ImVec2(plot_width, waterfallPanelHeight), &waterfallFrames[channel], &waterfallHovered[channel]))
            continue;

        const int width = static_cast<int>(waterfallFrames[channel].GetWidth());
        const int height = static_cast<int>(waterfallFrames[channel].GetHeight());
        if (width != gWaterfallWidth[channel] || height != gWaterfallHeight[channel] || !gWaterfallInitialized[channel])
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gWaterfallWidth[channel] = width;
            gWaterfallHeight[channel] = height;
            Init_waterfall(channel, width, height);
            rebuild_scale_if_ready_locked();
            refresh_display_state_locked(false);
            gWaterfallInitialized[channel] = true;
        }

        Draw_waterfall(channel, waterfallFrames[channel]);
        Draw_vertical_scale(waterfallFrames[channel], gWaterfallSecondsPerRow);
        if (display_mode_is_split())
        {
            const char *label = display_channel_title(channel);
            if (label == nullptr || label[0] == '\0')
                continue;
            ImGui::GetWindowDrawList()->AddText(
                ImGui::GetFont(),
                overlay_text_font_size(),
                ImVec2(waterfallFrames[channel].Min.x + 10.0f, waterfallFrames[channel].Min.y + 8.0f),
                apply_overlay_text_alpha(display_channel_live_color(channel)),
                label);
        }

        if (channel + 1 < waterfallPanels && waterfallGap > 0.0f)
            ImGui::Dummy(ImVec2(0.0f, waterfallGap));
    }

    if (show_spectrum && spectrumFrame.GetWidth() > 0.0f && spectrumFrame.GetHeight() > 0.0f)
    {
        gFrequencyGestureFrame = spectrumFrame;
        if (show_waterfall && waterfallPanels > 0 && waterfallFrames[waterfallPanels - 1].GetWidth() > 0.0f && waterfallFrames[waterfallPanels - 1].GetHeight() > 0.0f)
            gFrequencyGestureFrame.Max.y = waterfallFrames[waterfallPanels - 1].Max.y;
        gFrequencyGestureFrameValid = true;
    }
    else if (show_waterfall && waterfallPanels > 0 && waterfallFrames[0].GetWidth() > 0.0f && waterfallFrames[0].GetHeight() > 0.0f)
    {
        gFrequencyGestureFrame = waterfallFrames[0];
        gFrequencyGestureFrame.Max.y = waterfallFrames[waterfallPanels - 1].Max.y;
        gFrequencyGestureFrameValid = true;
    }

    update_cursor_state(spectrumFrame, spectrumHovered, waterfallFrames, waterfallHovered, waterfallPanels);
    if (show_spectrum)
        draw_spectrum(spectrumFrame);
    draw_cursor(spectrumFrame, waterfallFrames, waterfallPanels);
}
}

void Spectrogrammer_Init(void *window)
{
#ifdef ANDROID
    gAndroidApp = static_cast<android_app *>(window);
#endif
    load_config_if_needed(window);

    if (!gSessionInitialized)
    {
        start_processing_session();
        gSessionInitialized = (gProcessors[0] != nullptr);
    }
}

void Spectrogrammer_ReleaseGraphics()
{
    Shutdown_waterfall();
    for (int channel = 0; channel < kMaxInputChannels; channel++)
        gWaterfallInitialized[channel] = false;
}

void Spectrogrammer_Shutdown()
{
    stop_processing_session();
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        delete gScaleBufferX;
        gScaleBufferX = nullptr;
        clear_peak_hold_locked();
        clear_reference_hold_locked();
        clear_peak_markers_locked();
        for (int channel = 0; channel < kMaxInputChannels; channel++)
        {
            channel_state(channel).live_power_raw.Resize(0);
            channel_state(channel).live_line.Resize(0);
        }
        clear_display_channels_locked();
    }

    Reset_waterfall_storage();
    Shutdown_waterfall();
    clear_frequency_gesture_frame();
    gPinchGesture = PinchGestureState{};
    save_config_if_needed();
    gSessionInitialized = false;
}

bool Spectrogrammer_MainLoopStep()
{
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
    ImGui::Begin("主窗口", nullptr, window_flags);

    if (gSettingsPage == SettingsPage::Settings)
        render_settings_page();
    else
        render_main_screen();

    ImGui::End();
    ImGui::PopStyleVar();
    save_config_if_needed();
    return true;
}

bool Spectrogrammer_HandleBackPressed()
{
    if (gSettingsPage != SettingsPage::None)
    {
        gSettingsPage = SettingsPage::None;
        gSettingsScrollTracking = false;
        gSettingsScrollDragging = false;
        return true;
    }

    return false;
}

bool Spectrogrammer_HandleTouchGesture(int action, int pointerCount, const float *xs, const float *ys)
{
    constexpr int kActionUp = 1;
    constexpr int kActionMove = 2;
    constexpr int kActionCancel = 3;
    constexpr int kActionPointerDown = 5;
    constexpr int kActionPointerUp = 6;

    if (action == kActionUp || action == kActionCancel || action == kActionPointerUp)
    {
        const bool was_active = gPinchGesture.active;
        gPinchGesture = PinchGestureState{};
        return was_active;
    }

    if (gSettingsPage != SettingsPage::None || !gFrequencyGestureFrameValid || pointerCount < 2 || xs == nullptr || ys == nullptr)
        return false;

    const float normalized_x0 = clamp(unlerp(gFrequencyGestureFrame.Min.x, gFrequencyGestureFrame.Max.x, xs[0]), 0.0f, 1.0f);
    const float normalized_x1 = clamp(unlerp(gFrequencyGestureFrame.Min.x, gFrequencyGestureFrame.Max.x, xs[1]), 0.0f, 1.0f);
    const float left_x = std::min(normalized_x0, normalized_x1);
    const float right_x = std::max(normalized_x0, normalized_x1);
    const float midpoint_x = (xs[0] + xs[1]) * 0.5f;
    const float midpoint_y = (ys[0] + ys[1]) * 0.5f;
    const bool inside_frame =
        midpoint_x >= gFrequencyGestureFrame.Min.x && midpoint_x <= gFrequencyGestureFrame.Max.x &&
        midpoint_y >= gFrequencyGestureFrame.Min.y && midpoint_y <= gFrequencyGestureFrame.Max.y;

    if (!gPinchGesture.active)
    {
        if (action != kActionPointerDown && action != kActionMove)
            return false;
        if (!inside_frame)
            return false;
        gPinchGesture.active = true;
        gPinchGesture.previous_left_x = left_x;
        gPinchGesture.previous_right_x = right_x;
        gCursorDragging = false;
        return true;
    }

    if (action == kActionMove || action == kActionPointerDown)
    {
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            update_frequency_view_from_pinch_locked(
                gPinchGesture.previous_left_x,
                gPinchGesture.previous_right_x,
                left_x,
                right_x);
        }
        gPinchGesture.previous_left_x = left_x;
        gPinchGesture.previous_right_x = right_x;
        gCursorDragging = false;
        return true;
    }

    return false;
}

bool Spectrogrammer_ShouldStayAwake()
{
    return gConfig.stay_awake;
}

bool Spectrogrammer_ShouldRunInBackground()
{
    return gConfig.background_capture_enabled;
}

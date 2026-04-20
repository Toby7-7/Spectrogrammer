// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

enum class AudioSourceMode
{
    Default = 0,
    Generic = 1,
    VoiceRecognition = 2,
    Camcorder = 3,
    Unprocessed = 4,
};

enum class InputChannelMode
{
    Mono = 0,
    Left = 1,
    Right = 2,
    StereoMixed = 3,
    StereoIndependent = 4,
};

enum class SamplingRateMode
{
    Auto = 0,
    Fixed = 1,
};

enum class WindowFunctionType
{
    Rectangular = 0,
    Hann = 1,
    Hamming = 2,
    BlackmanHarris = 3,
};

enum class FrequencyAxisScale
{
    Logarithmic = 0,
    Linear = 1,
    Music = 2,
    Mel = 3,
    Bark = 4,
    ERB = 5,
};

enum class WaterfallSizeMode
{
    OneThird = 0,
    OneHalf = 1,
    TwoThirds = 2,
    OneQuarter = 3,
    TwoFifths = 4,
    ThreeFifths = 5,
    ThreeQuarters = 6,
};

enum class TraceMode
{
    Live = 0,
    LiveAndPeakHold = 1,
    PeakHoldOnly = 2,
};

enum class PeakMarkerSourceMode
{
    Live = 0,
    ShortHold = 1,
};

struct AppConfig
{
    int version;
    AudioSourceMode audio_source_mode;
    InputChannelMode input_channel_mode;
    SamplingRateMode sampling_rate_mode;
    int sample_rate_hz;
    int fft_size;
    int decimations;
    WindowFunctionType window_function;
    float desired_transform_interval_ms;
    float exponential_smoothing_factor;
    float overlay_text_scale;
    float overlay_text_alpha;
    FrequencyAxisScale frequency_axis_scale;
    WaterfallSizeMode waterfall_size_mode;
    bool swap_stereo_order;
    bool max_hold_trace_enabled;
    float peak_hold_falloff_seconds;
    int peak_marker_count;
    PeakMarkerSourceMode peak_marker_source_mode;
    bool stay_awake;
    TraceMode trace_mode;
    bool background_capture_enabled;
    bool show_spectrum;
    bool show_waterfall;
};

AppConfig MakeDefaultAppConfig();
bool LoadAppConfig(const char *path, AppConfig *config);
bool SaveAppConfig(const char *path, const AppConfig &config);

int GetDecimationFactor(const AppConfig &config);
float GetEffectiveSampleRate(const AppConfig &config);
float GetWaterfallFraction(const AppConfig &config);

#endif

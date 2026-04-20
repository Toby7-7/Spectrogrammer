// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0

#include "AppConfig.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace
{
constexpr int kConfigVersion = 8;

void migrate_config(AppConfig *config)
{
    if (config == nullptr)
        return;

    if (config->version < 2)
    {
        // Version 1 accidentally defaulted to heavy decimation and a very small FFT,
        // which hid most of the audible band on first launch.
        if (config->decimations == 3)
            config->decimations = 0;
        if (config->fft_size == 1024)
            config->fft_size = 4096;
        if (config->peak_hold_falloff_seconds <= 0.0f)
            config->peak_hold_falloff_seconds = 4.0f;
        config->version = 2;
    }

    if (config->version < 3)
    {
        if (config->audio_source_mode == AudioSourceMode::Default)
            config->audio_source_mode = AudioSourceMode::Unprocessed;
        config->version = 3;
    }

    if (config->version < 4)
    {
        if (config->sample_rate_hz > 192000)
            config->sample_rate_hz = 192000;
        config->peak_marker_source_mode = PeakMarkerSourceMode::Live;
        config->version = 4;
    }

    if (config->version < 5)
    {
        config->show_waterfall = true;
        config->version = 5;
    }

    if (config->version < 6)
    {
        if (config->audio_source_mode == AudioSourceMode::Unprocessed)
            config->audio_source_mode = AudioSourceMode::Default;
        if (fabsf(config->exponential_smoothing_factor - 0.30f) < 1e-3f)
            config->exponential_smoothing_factor = 0.10f;
        if (config->peak_marker_source_mode == PeakMarkerSourceMode::Live)
            config->peak_marker_source_mode = PeakMarkerSourceMode::ShortHold;
        config->version = 6;
    }

    if (config->version < 7)
    {
        config->input_channel_mode = InputChannelMode::StereoIndependent;
        config->version = 7;
    }

    if (config->version < 8)
    {
        if (config->input_channel_mode == InputChannelMode::Left)
            config->input_channel_mode = InputChannelMode::StereoIndependent;
        config->swap_stereo_order = false;
        config->show_spectrum = true;
        config->version = kConfigVersion;
    }
}

bool parse_bool(const char *value)
{
    return atoi(value) != 0;
}

bool parse_line(const char *line, char *key, size_t key_size, char *value, size_t value_size)
{
    const char *equals = strchr(line, '=');
    if (equals == nullptr)
        return false;

    size_t key_len = static_cast<size_t>(equals - line);
    size_t value_len = strlen(equals + 1);

    while (value_len > 0 && (equals[1 + value_len - 1] == '\n' || equals[1 + value_len - 1] == '\r'))
        value_len--;

    if (key_len == 0 || key_len >= key_size || value_len >= value_size)
        return false;

    memcpy(key, line, key_len);
    key[key_len] = '\0';
    memcpy(value, equals + 1, value_len);
    value[value_len] = '\0';
    return true;
}
}

AppConfig MakeDefaultAppConfig()
{
    AppConfig config = {};
    config.version = kConfigVersion;
    config.audio_source_mode = AudioSourceMode::Default;
    config.input_channel_mode = InputChannelMode::StereoIndependent;
    config.sampling_rate_mode = SamplingRateMode::Auto;
    config.sample_rate_hz = 48000;
    config.fft_size = 4096;
    config.decimations = 0;
    config.window_function = WindowFunctionType::BlackmanHarris;
    config.desired_transform_interval_ms = 20.0f;
    config.exponential_smoothing_factor = 0.10f;
    config.frequency_axis_scale = FrequencyAxisScale::Logarithmic;
    config.waterfall_size_mode = WaterfallSizeMode::TwoThirds;
    config.swap_stereo_order = false;
    config.max_hold_trace_enabled = true;
    config.peak_hold_falloff_seconds = 4.0f;
    config.peak_marker_count = 3;
    config.peak_marker_source_mode = PeakMarkerSourceMode::ShortHold;
    config.stay_awake = false;
    config.trace_mode = TraceMode::LiveAndPeakHold;
    config.background_capture_enabled = true;
    config.show_spectrum = true;
    config.show_waterfall = true;
    return config;
}

bool LoadAppConfig(const char *path, AppConfig *config)
{
    if (path == nullptr || config == nullptr)
        return false;

    FILE *file = fopen(path, "rb");
    if (file == nullptr)
        return false;

    AppConfig loaded = MakeDefaultAppConfig();
    char line[256];
    while (fgets(line, sizeof(line), file) != nullptr)
    {
        char key[128];
        char value[128];
        if (!parse_line(line, key, sizeof(key), value, sizeof(value)))
            continue;

        if (strcmp(key, "version") == 0)
            loaded.version = atoi(value);
        else if (strcmp(key, "audio_source_mode") == 0)
            loaded.audio_source_mode = static_cast<AudioSourceMode>(atoi(value));
        else if (strcmp(key, "input_channel_mode") == 0)
            loaded.input_channel_mode = static_cast<InputChannelMode>(atoi(value));
        else if (strcmp(key, "sampling_rate_mode") == 0)
            loaded.sampling_rate_mode = static_cast<SamplingRateMode>(atoi(value));
        else if (strcmp(key, "sample_rate_hz") == 0)
            loaded.sample_rate_hz = atoi(value);
        else if (strcmp(key, "fft_size") == 0)
            loaded.fft_size = atoi(value);
        else if (strcmp(key, "decimations") == 0)
            loaded.decimations = atoi(value);
        else if (strcmp(key, "window_function") == 0)
            loaded.window_function = static_cast<WindowFunctionType>(atoi(value));
        else if (strcmp(key, "desired_transform_interval_ms") == 0)
            loaded.desired_transform_interval_ms = strtof(value, nullptr);
        else if (strcmp(key, "exponential_smoothing_factor") == 0)
            loaded.exponential_smoothing_factor = strtof(value, nullptr);
        else if (strcmp(key, "frequency_axis_scale") == 0)
            loaded.frequency_axis_scale = static_cast<FrequencyAxisScale>(atoi(value));
        else if (strcmp(key, "waterfall_size_mode") == 0)
            loaded.waterfall_size_mode = static_cast<WaterfallSizeMode>(atoi(value));
        else if (strcmp(key, "swap_stereo_order") == 0)
            loaded.swap_stereo_order = parse_bool(value);
        else if (strcmp(key, "max_hold_trace_enabled") == 0)
            loaded.max_hold_trace_enabled = parse_bool(value);
        else if (strcmp(key, "peak_hold_falloff_seconds") == 0)
            loaded.peak_hold_falloff_seconds = strtof(value, nullptr);
        else if (strcmp(key, "peak_marker_count") == 0)
            loaded.peak_marker_count = atoi(value);
        else if (strcmp(key, "peak_marker_source_mode") == 0)
            loaded.peak_marker_source_mode = static_cast<PeakMarkerSourceMode>(atoi(value));
        else if (strcmp(key, "stay_awake") == 0)
            loaded.stay_awake = parse_bool(value);
        else if (strcmp(key, "trace_mode") == 0)
            loaded.trace_mode = static_cast<TraceMode>(atoi(value));
        else if (strcmp(key, "background_capture_enabled") == 0)
            loaded.background_capture_enabled = parse_bool(value);
        else if (strcmp(key, "show_spectrum") == 0)
            loaded.show_spectrum = parse_bool(value);
        else if (strcmp(key, "show_waterfall") == 0)
            loaded.show_waterfall = parse_bool(value);
    }

    fclose(file);

    if (loaded.version <= 0 || loaded.version > kConfigVersion)
        return false;

    migrate_config(&loaded);

    if (loaded.sample_rate_hz <= 0)
        loaded.sample_rate_hz = 48000;
    if ((int)loaded.input_channel_mode < (int)InputChannelMode::Mono ||
        (int)loaded.input_channel_mode > (int)InputChannelMode::StereoIndependent)
        loaded.input_channel_mode = InputChannelMode::StereoIndependent;
    if (loaded.sample_rate_hz > 192000)
        loaded.sample_rate_hz = 192000;
    if (loaded.fft_size < 128)
        loaded.fft_size = 1024;
    if (loaded.decimations < 0)
        loaded.decimations = 0;
    if (loaded.decimations > 5)
        loaded.decimations = 5;
    if ((int)loaded.waterfall_size_mode < (int)WaterfallSizeMode::OneThird ||
        (int)loaded.waterfall_size_mode > (int)WaterfallSizeMode::ThreeQuarters)
        loaded.waterfall_size_mode = WaterfallSizeMode::TwoThirds;
    if (loaded.desired_transform_interval_ms < 2.0f)
        loaded.desired_transform_interval_ms = 2.0f;
    if (loaded.desired_transform_interval_ms > 250.0f)
        loaded.desired_transform_interval_ms = 250.0f;
    if (loaded.exponential_smoothing_factor < 0.0f)
        loaded.exponential_smoothing_factor = 0.0f;
    if (loaded.exponential_smoothing_factor > 0.99f)
        loaded.exponential_smoothing_factor = 0.99f;
    if (loaded.peak_hold_falloff_seconds < 0.0f)
        loaded.peak_hold_falloff_seconds = 0.0f;
    if (loaded.peak_hold_falloff_seconds > 120.0f)
        loaded.peak_hold_falloff_seconds = 120.0f;
    if (loaded.peak_marker_count < 0)
        loaded.peak_marker_count = 0;
    if (loaded.peak_marker_count > 5)
        loaded.peak_marker_count = 5;
    if ((int)loaded.peak_marker_source_mode < (int)PeakMarkerSourceMode::Live ||
        (int)loaded.peak_marker_source_mode > (int)PeakMarkerSourceMode::ShortHold)
        loaded.peak_marker_source_mode = PeakMarkerSourceMode::Live;
    if (!loaded.show_spectrum && !loaded.show_waterfall)
        loaded.show_spectrum = true;
    *config = loaded;
    return true;
}

bool SaveAppConfig(const char *path, const AppConfig &config)
{
    if (path == nullptr)
        return false;

    FILE *file = fopen(path, "wb");
    if (file == nullptr)
        return false;

    fprintf(file, "version=%d\n", kConfigVersion);
    fprintf(file, "audio_source_mode=%d\n", static_cast<int>(config.audio_source_mode));
    fprintf(file, "input_channel_mode=%d\n", static_cast<int>(config.input_channel_mode));
    fprintf(file, "sampling_rate_mode=%d\n", static_cast<int>(config.sampling_rate_mode));
    fprintf(file, "sample_rate_hz=%d\n", config.sample_rate_hz);
    fprintf(file, "fft_size=%d\n", config.fft_size);
    fprintf(file, "decimations=%d\n", config.decimations);
    fprintf(file, "window_function=%d\n", static_cast<int>(config.window_function));
    fprintf(file, "desired_transform_interval_ms=%.6f\n", config.desired_transform_interval_ms);
    fprintf(file, "exponential_smoothing_factor=%.6f\n", config.exponential_smoothing_factor);
    fprintf(file, "frequency_axis_scale=%d\n", static_cast<int>(config.frequency_axis_scale));
    fprintf(file, "waterfall_size_mode=%d\n", static_cast<int>(config.waterfall_size_mode));
    fprintf(file, "swap_stereo_order=%d\n", config.swap_stereo_order ? 1 : 0);
    fprintf(file, "max_hold_trace_enabled=%d\n", config.max_hold_trace_enabled ? 1 : 0);
    fprintf(file, "peak_hold_falloff_seconds=%.6f\n", config.peak_hold_falloff_seconds);
    fprintf(file, "peak_marker_count=%d\n", config.peak_marker_count);
    fprintf(file, "peak_marker_source_mode=%d\n", static_cast<int>(config.peak_marker_source_mode));
    fprintf(file, "stay_awake=%d\n", config.stay_awake ? 1 : 0);
    fprintf(file, "trace_mode=%d\n", static_cast<int>(config.trace_mode));
    fprintf(file, "background_capture_enabled=%d\n", config.background_capture_enabled ? 1 : 0);
    fprintf(file, "show_spectrum=%d\n", config.show_spectrum ? 1 : 0);
    fprintf(file, "show_waterfall=%d\n", config.show_waterfall ? 1 : 0);

    fclose(file);
    return true;
}

int GetDecimationFactor(const AppConfig &config)
{
    return 1 << config.decimations;
}

float GetEffectiveSampleRate(const AppConfig &config)
{
    return static_cast<float>(config.sample_rate_hz) / static_cast<float>(GetDecimationFactor(config));
}

float GetWaterfallFraction(const AppConfig &config)
{
    switch (config.waterfall_size_mode)
    {
    case WaterfallSizeMode::OneQuarter:
        return 0.25f;
    case WaterfallSizeMode::OneThird:
        return 1.0f / 3.0f;
    case WaterfallSizeMode::TwoFifths:
        return 0.4f;
    case WaterfallSizeMode::OneHalf:
        return 0.5f;
    case WaterfallSizeMode::ThreeFifths:
        return 0.6f;
    case WaterfallSizeMode::TwoThirds:
        return 2.0f / 3.0f;
    case WaterfallSizeMode::ThreeQuarters:
        return 0.75f;
    default:
        return 2.0f / 3.0f;
    }
}

#include "ScaleUI.h"

#include <cfloat>
#include <cmath>
#include <stdio.h>

namespace
{
void format_axis_frequency_label(char *buffer, size_t buffer_size, float freq)
{
    if (freq >= 1000.0f)
    {
        const float khz = freq / 1000.0f;
        if (fabsf(khz - roundf(khz)) < 0.05f)
            snprintf(buffer, buffer_size, "%.0fk", khz);
        else
            snprintf(buffer, buffer_size, "%.1fk", khz);
    }
    else
    {
        snprintf(buffer, buffer_size, "%.0f", freq);
    }
}

void draw_tick_label(ImGuiWindow *window, float x, float text_y, const char *label, float *last_label_right)
{
    const float textWidth = ImGui::CalcTextSize(label).x;
    const float left = x - textWidth * 0.5f;
    const float right = x + textWidth * 0.5f;
    if (left <= *last_label_right + 12.0f)
        return;

    window->DrawList->AddText(ImVec2(left, text_y), ImGui::GetColorU32(ImGuiCol_Text), label);
    *last_label_right = right;
}

float choose_linear_major_step(float min_freq, float max_freq)
{
    const float target_major_count = 5.0f;
    const float raw_step = fmaxf(1.0f, (max_freq - min_freq) / target_major_count);
    const float magnitude = powf(10.0f, floorf(log10f(raw_step)));
    const float normalized = raw_step / magnitude;

    if (normalized <= 1.0f)
        return magnitude;
    if (normalized <= 2.0f)
        return 2.0f * magnitude;
    if (normalized <= 5.0f)
        return 5.0f * magnitude;
    return 10.0f * magnitude;
}

void draw_linear_scale(ImRect frame_bb, ScaleBufferBase *pScaleBufferX)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float text_y = frame_bb.Max.y + 6.0f;
    const float min_freq = pScaleBufferX->XtoFreq(0.0f);
    const float max_freq = pScaleBufferX->XtoFreq(1.0f);
    const float major_step = choose_linear_major_step(min_freq, max_freq);
    const float minor_step = major_step / 5.0f;
    float last_label_right = -FLT_MAX;

    for (float freq = floorf(min_freq / minor_step) * minor_step; freq <= max_freq + minor_step; freq += minor_step)
    {
        if (freq < min_freq)
            continue;

        const float x = lerp(pScaleBufferX->FreqToX(freq), frame_bb.Min.x, frame_bb.Max.x);
        if (x < frame_bb.Min.x || x > frame_bb.Max.x)
            continue;

        const float major_index = freq / major_step;
        const bool major = fabsf(major_index - roundf(major_index)) < 0.02f;
        if (major)
        {
            char str[32];
            format_axis_frequency_label(str, sizeof(str), freq);
            draw_tick_label(window, x, text_y, str, &last_label_right);
        }

        window->DrawList->AddLine(
            ImVec2(x, frame_bb.Min.y),
            ImVec2(x, frame_bb.Max.y),
            major ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }
}

void draw_logarithmic_scale(ImRect frame_bb, ScaleBufferBase *pScaleBufferX)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float text_y = frame_bb.Max.y + 6.0f;
    const float min_freq = fmaxf(1.0f, pScaleBufferX->XtoFreq(0.0f));
    const float max_freq = fmaxf(min_freq, pScaleBufferX->XtoFreq(1.0f));
    const int start_decade = static_cast<int>(floorf(log10f(min_freq)));
    const int end_decade = static_cast<int>(ceilf(log10f(max_freq)));
    float last_label_right = -FLT_MAX;

    for (int decade = start_decade; decade <= end_decade; decade++)
    {
        const float base = powf(10.0f, static_cast<float>(decade));
        for (int i = 1; i < 10; i++)
        {
            const float freq = static_cast<float>(i) * base;
            if (freq < min_freq || freq > max_freq)
                continue;

            const float x = lerp(pScaleBufferX->FreqToX(freq), frame_bb.Min.x, frame_bb.Max.x);
            if (x < frame_bb.Min.x || x > frame_bb.Max.x)
                continue;

            const bool major = i == 1;
            const bool emphasized = major || i == 2 || i == 5;
            if (emphasized)
            {
                char str[32];
                format_axis_frequency_label(str, sizeof(str), freq);
                draw_tick_label(window, x, text_y, str, &last_label_right);
            }

            window->DrawList->AddLine(
                ImVec2(x, frame_bb.Min.y),
                ImVec2(x, frame_bb.Max.y),
                major ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        }
    }
}

void draw_music_scale(ImRect frame_bb, ScaleBufferBase *pScaleBufferX)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float text_y = frame_bb.Max.y + 6.0f;
    const float min_freq = fmaxf(16.0f, pScaleBufferX->XtoFreq(0.0f));
    const float max_freq = fmaxf(min_freq, pScaleBufferX->XtoFreq(1.0f));
    const float base_freq = 31.25f;
    const int start_octave = static_cast<int>(floorf(log2f(min_freq / base_freq))) - 1;
    const int end_octave = static_cast<int>(ceilf(log2f(max_freq / base_freq))) + 1;
    float last_label_right = -FLT_MAX;

    for (int octave = start_octave; octave <= end_octave; octave++)
    {
        const float octave_base = base_freq * powf(2.0f, (float)octave);
        for (int division = 0; division < 3; division++)
        {
            const float freq = octave_base * powf(2.0f, division / 3.0f);
            if (freq < min_freq || freq > max_freq)
                continue;

            const float x = lerp(pScaleBufferX->FreqToX(freq), frame_bb.Min.x, frame_bb.Max.x);
            if (x < frame_bb.Min.x || x > frame_bb.Max.x)
                continue;

            const bool major = division == 0;
            if (major)
            {
                char str[32];
                format_axis_frequency_label(str, sizeof(str), freq);
                draw_tick_label(window, x, text_y, str, &last_label_right);
            }

            window->DrawList->AddLine(
                ImVec2(x, frame_bb.Min.y),
                ImVec2(x, frame_bb.Max.y),
                major ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        }
    }
}

void draw_perceptual_scale(ImRect frame_bb, ScaleBufferBase *pScaleBufferX, FrequencyAxisScale curve)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float text_y = frame_bb.Max.y + 6.0f;
    const float min_freq = fmaxf(1.0f, pScaleBufferX->XtoFreq(0.0f));
    const float max_freq = fmaxf(min_freq, pScaleBufferX->XtoFreq(1.0f));
    const float min_scale = frequency_to_scale_value(curve, min_freq);
    const float max_scale = frequency_to_scale_value(curve, max_freq);
    const int major_count = 8;
    const int minor_per_major = 4;
    float last_label_right = -FLT_MAX;

    for (int step = 0; step <= major_count * minor_per_major; step++)
    {
        const bool major = (step % minor_per_major) == 0;
        const float t = step / (float)(major_count * minor_per_major);
        const float scale_value = lerp(t, min_scale, max_scale);
        const float freq = scale_value_to_frequency(curve, scale_value);
        if (freq < min_freq || freq > max_freq)
            continue;

        const float x = lerp(pScaleBufferX->FreqToX(freq), frame_bb.Min.x, frame_bb.Max.x);
        if (x < frame_bb.Min.x || x > frame_bb.Max.x)
            continue;

        if (major)
        {
            char str[32];
            format_axis_frequency_label(str, sizeof(str), freq);
            draw_tick_label(window, x, text_y, str, &last_label_right);
        }

        window->DrawList->AddLine(
            ImVec2(x, frame_bb.Min.y),
            ImVec2(x, frame_bb.Max.y),
            major ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }
}
}

void draw_frequency_scale(ImRect frame_bb, ScaleBufferBase *pScaleBufferX, FrequencyAxisScale curve)
{
    if (pScaleBufferX == nullptr)
        return;

    switch (curve)
    {
    case FrequencyAxisScale::Linear:
        draw_linear_scale(frame_bb, pScaleBufferX);
        break;
    case FrequencyAxisScale::Music:
        draw_music_scale(frame_bb, pScaleBufferX);
        break;
    case FrequencyAxisScale::Mel:
    case FrequencyAxisScale::Bark:
    case FrequencyAxisScale::ERB:
        draw_perceptual_scale(frame_bb, pScaleBufferX, curve);
        break;
    case FrequencyAxisScale::Logarithmic:
    default:
        draw_logarithmic_scale(frame_bb, pScaleBufferX);
        break;
    }
}

void draw_scale_y(ImRect frame_bb, float min, float max)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImGui::PushClipRect(frame_bb.Min, frame_bb.Max, true);

    for (int dec = 0; dec > -130; dec -= 25)
    {
        float t = unlerp(min, max, dec);
        float y = lerp(t, frame_bb.Max.y, frame_bb.Min.y);

        if (y < frame_bb.Min.y)
            continue;
        if (y > frame_bb.Max.y)
            break;

        char str[32];
        sprintf(str, "%i", (int)dec);
        window->DrawList->AddText(
            ImVec2(frame_bb.Min.x, y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            str
        );

        window->DrawList->AddLine(
            ImVec2(frame_bb.Min.x, y),
            ImVec2(frame_bb.Max.x, y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled));
    }

    ImGui::PopClipRect();
}

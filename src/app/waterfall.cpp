#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdlib.h>

#include <cstring>
#include <vector>

#include "colormaps.h"
#include "waterfall.h"

namespace
{
constexpr int kWaterfallChannelCapacity = 2;

void calc_scale(float seconds_per_row, float desired_distance, float *notch_distance, float *scale)
{
    *notch_distance = desired_distance;
    *scale = 1;

    float min_err = 10000;
    float scale_list[] = {1.0f / 20.0f, 1.0f / 10.0f, 1.0f / 2.0f, 1.0f, 3.0f, 4.0f, 10.0f, 30.0f, 60.0f, 120.0f, 300.0f, 600.0f, 1200.0f, 3600.0f};
    for (int i = 0; i < 14; i++)
    {
        float nd = scale_list[i] / seconds_per_row;
        float err = fabs(nd - desired_distance);
        if (err < min_err)
            min_err = err;
        if (err > min_err)
            break;
        *notch_distance = nd;
        *scale = scale_list[i];
    }
}

int get_time_magnitude(float total_time)
{
    if (total_time > 60 * 60)
        return 3;
    if (total_time > 60)
        return 2;
    if (total_time > 20)
        return 1;
    return 0;
}

struct WaterfallState
{
    int texture_width = -1;
    int texture_height = -1;
    GLuint image_texture = 0xffffffff;
    std::vector<uint16_t> image_storage;
    std::vector<uint16_t> row_scratch;
    bool texture_dirty = false;
};

WaterfallState gWaterfalls[kWaterfallChannelCapacity];

WaterfallState *get_waterfall(int channel)
{
    if (channel < 0 || channel >= kWaterfallChannelCapacity)
        return nullptr;
    return &gWaterfalls[channel];
}

void ensure_texture(WaterfallState *state)
{
    if (state == nullptr || state->texture_width <= 0 || state->texture_height <= 0)
        return;

    if (state->image_texture == 0xffffffff)
    {
        glGenTextures(1, &state->image_texture);
        glBindTexture(GL_TEXTURE_2D, state->image_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, state->texture_width, state->texture_height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, state->image_storage.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        state->texture_dirty = false;
        return;
    }

    if (state->texture_dirty)
    {
        glBindTexture(GL_TEXTURE_2D, state->image_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, state->texture_width, state->texture_height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, state->image_storage.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        state->texture_dirty = false;
    }
}
}

void Init_waterfall(int channel, int16_t width, int16_t height)
{
    WaterfallState *state = get_waterfall(channel);
    if (state == nullptr || width <= 0 || height <= 0)
        return;

    const int old_width = state->texture_width;
    const int old_height = state->texture_height;
    const std::vector<uint16_t> old_storage = state->image_storage;
    const bool resized = state->texture_width != width || state->texture_height != height;
    state->texture_width = width;
    state->texture_height = height;

    if (resized || state->image_storage.size() != static_cast<size_t>(state->texture_width * state->texture_height))
    {
        state->image_storage.assign(static_cast<size_t>(state->texture_width) * static_cast<size_t>(state->texture_height), 0);

        if (resized && old_width > 0 && old_height > 0 && !old_storage.empty())
        {
            const int copy_width = old_width < state->texture_width ? old_width : state->texture_width;
            const int copy_height = old_height < state->texture_height ? old_height : state->texture_height;

            for (int y = 0; y < copy_height; y++)
            {
                memcpy(
                    &state->image_storage[static_cast<size_t>(y) * static_cast<size_t>(state->texture_width)],
                    &old_storage[static_cast<size_t>(y) * static_cast<size_t>(old_width)],
                    sizeof(uint16_t) * static_cast<size_t>(copy_width));
            }
        }

        state->texture_dirty = true;
    }

    if (resized && state->image_texture != 0xffffffff)
    {
        glDeleteTextures(1, &state->image_texture);
        state->image_texture = 0xffffffff;
    }

    ensure_texture(state);
}

void Draw_update(int channel, float *pData, uint32_t size)
{
    WaterfallState *state = get_waterfall(channel);
    if (state == nullptr || state->texture_width <= 0 || state->texture_height <= 0 || pData == nullptr || size == 0)
        return;

    state->row_scratch.assign(static_cast<size_t>(state->texture_width), 0);
    for (int i = 0; i < state->texture_width; i++)
    {
        const uint32_t source_index = static_cast<uint32_t>((static_cast<uint64_t>(i) * size) / static_cast<uint32_t>(state->texture_width));
        const uint32_t clamped_index = source_index < size ? source_index : size - 1;
        state->row_scratch[static_cast<size_t>(i)] = GetColorMap(pData[clamped_index] * 255);
    }

    if (state->texture_height > 1)
    {
        memmove(
            &state->image_storage[static_cast<size_t>(state->texture_width)],
            &state->image_storage[0],
            sizeof(uint16_t) * static_cast<size_t>(state->texture_width) * static_cast<size_t>(state->texture_height - 1));
    }
    memcpy(&state->image_storage[0], state->row_scratch.data(), sizeof(uint16_t) * static_cast<size_t>(state->texture_width));
    state->texture_dirty = true;
}

void Draw_waterfall(int channel, ImRect frame_bb)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    WaterfallState *state = get_waterfall(channel);
    ensure_texture(state);
    if (state == nullptr || state->image_texture == 0xffffffff)
        return;
    window->DrawList->AddImage((ImTextureID)state->image_texture, frame_bb.Min, frame_bb.Max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
}

void Draw_vertical_scale(ImRect frame_bb, float seconds_per_row)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImU32 col = IM_COL32(200, 200, 200, 200);

    float scale;
    float notch_distance;
    float desired_distance = 25;
    calc_scale(seconds_per_row, desired_distance, &notch_distance, &scale);

    float total_time = seconds_per_row * frame_bb.GetHeight();
    int time_magnitude = get_time_magnitude(total_time);

    for (int i = 0;; i++)
    {
        float y = floor((float)i * notch_distance);
        if (y >= frame_bb.GetHeight())
            break;

        bool long_notch = (i % 5) == 0;

        window->DrawList->AddLine(
            ImVec2(frame_bb.Min.x, frame_bb.Min.y + y),
            ImVec2(frame_bb.Min.x + (long_notch ? 50 : 25), frame_bb.Min.y + y),
            col);

        if (long_notch)
        {
            char str[255];
            int t = static_cast<int>(floor(i * scale));
            switch (time_magnitude)
            {
            case 0:
                sprintf(str, "%0.2fs", i * scale);
                break;
            case 1:
                sprintf(str, "%02is", t % 60);
                break;
            case 2:
                sprintf(str, "%im%02is", t / 60, t % 60);
                break;
            case 3:
            default:
                sprintf(str, "%ih%02im", t / (60 * 60), (t % (60 * 60)) / 60);
                break;
            }

            window->DrawList->AddText(ImVec2(frame_bb.Min.x + 50, frame_bb.Min.y + y), col, str);
        }
    }
}

void Shutdown_waterfall()
{
    glFlush();
    for (int channel = 0; channel < kWaterfallChannelCapacity; channel++)
    {
        WaterfallState &state = gWaterfalls[channel];
        if (state.image_texture != 0xffffffff)
        {
            glDeleteTextures(1, &state.image_texture);
            state.image_texture = 0xffffffff;
        }
    }
}

void Reset_waterfall_storage(int channel)
{
    if (channel < 0)
    {
        for (int i = 0; i < kWaterfallChannelCapacity; i++)
            Reset_waterfall_storage(i);
        return;
    }

    WaterfallState *state = get_waterfall(channel);
    if (state != nullptr && !state->image_storage.empty())
    {
        std::fill(state->image_storage.begin(), state->image_storage.end(), 0);
        state->texture_dirty = true;
    }
}

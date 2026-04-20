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

static int texture_width = -1;
static int texture_height = -1;
static GLuint image_texture = 0xffffffff;
static std::vector<uint16_t> image_storage;
static std::vector<uint16_t> row_scratch;
static bool texture_dirty = false;

void ensure_texture()
{
    if (texture_width <= 0 || texture_height <= 0)
        return;

    if (image_texture == 0xffffffff)
    {
        glGenTextures(1, &image_texture);
        glBindTexture(GL_TEXTURE_2D, image_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texture_width, texture_height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_storage.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        texture_dirty = false;
        return;
    }

    if (texture_dirty)
    {
        glBindTexture(GL_TEXTURE_2D, image_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture_width, texture_height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, image_storage.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        texture_dirty = false;
    }
}
}

void Init_waterfall(int16_t width, int16_t height)
{
    if (width <= 0 || height <= 0)
        return;

    const int old_width = texture_width;
    const int old_height = texture_height;
    const std::vector<uint16_t> old_storage = image_storage;
    const bool resized = texture_width != width || texture_height != height;
    texture_width = width;
    texture_height = height;

    if (resized || image_storage.size() != static_cast<size_t>(texture_width * texture_height))
    {
        image_storage.assign(static_cast<size_t>(texture_width) * static_cast<size_t>(texture_height), 0);

        if (resized && old_width > 0 && old_height > 0 && !old_storage.empty())
        {
            const int copy_width = old_width < texture_width ? old_width : texture_width;
            const int copy_height = old_height < texture_height ? old_height : texture_height;

            for (int y = 0; y < copy_height; y++)
            {
                memcpy(
                    &image_storage[static_cast<size_t>(y) * static_cast<size_t>(texture_width)],
                    &old_storage[static_cast<size_t>(y) * static_cast<size_t>(old_width)],
                    sizeof(uint16_t) * static_cast<size_t>(copy_width));
            }
        }

        texture_dirty = true;
    }

    if (resized && image_texture != 0xffffffff)
    {
        glDeleteTextures(1, &image_texture);
        image_texture = 0xffffffff;
    }

    ensure_texture();
}

void Draw_update(float *pData, uint32_t size)
{
    if (texture_width <= 0 || texture_height <= 0 || pData == nullptr || size == 0)
        return;

    row_scratch.assign(static_cast<size_t>(texture_width), 0);
    for (int i = 0; i < texture_width; i++)
    {
        const uint32_t source_index = static_cast<uint32_t>((static_cast<uint64_t>(i) * size) / static_cast<uint32_t>(texture_width));
        const uint32_t clamped_index = source_index < size ? source_index : size - 1;
        row_scratch[static_cast<size_t>(i)] = GetColorMap(pData[clamped_index] * 255);
    }

    if (texture_height > 1)
    {
        memmove(
            &image_storage[static_cast<size_t>(texture_width)],
            &image_storage[0],
            sizeof(uint16_t) * static_cast<size_t>(texture_width) * static_cast<size_t>(texture_height - 1));
    }
    memcpy(&image_storage[0], row_scratch.data(), sizeof(uint16_t) * static_cast<size_t>(texture_width));
    texture_dirty = true;
}

void Draw_waterfall(ImRect frame_bb)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ensure_texture();
    if (image_texture == 0xffffffff)
        return;
    window->DrawList->AddImage((ImTextureID)image_texture, frame_bb.Min, frame_bb.Max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
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

    float total_time = seconds_per_row * static_cast<float>(texture_height);
    int time_magnitude = get_time_magnitude(total_time);

    for (int i = 0;; i++)
    {
        float y = floor((float)i * notch_distance);
        if (y >= texture_height)
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
    if (image_texture != 0xffffffff)
    {
        glDeleteTextures(1, &image_texture);
        image_texture = 0xffffffff;
    }
}

void Reset_waterfall_storage()
{
    if (!image_storage.empty())
    {
        std::fill(image_storage.begin(), image_storage.end(), 0);
        texture_dirty = true;
    }
}

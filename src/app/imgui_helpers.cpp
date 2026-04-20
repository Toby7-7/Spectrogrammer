#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_helpers.h"
#include <stdlib.h>
#include <vector>

bool block_add(const char *label, const ImVec2& size_arg, ImRect *pFrame_bb, bool *pHovered)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImGuiID id = window->GetID(label);
    ImVec2 size = ImGui::CalcItemSize(size_arg, 0.0f, 0.0f);
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    *pFrame_bb = bb;
    ImGui::ButtonBehavior(bb, id, pHovered, NULL);

    return true;
}


void get_max_min(float *pData, int stride, int values_count, float *scale_max, float *scale_min)
{
    if (*scale_min == FLT_MAX || *scale_max == FLT_MAX)
    {
        float v_min = FLT_MAX;
        float v_max = -FLT_MAX;
        for (int i = 0; i < values_count; i++)
        {
            const float v = pData[i*stride];
            if (v != v) // Ignore NaN values
                continue;
            v_min = ImMin(v_min, v);
            v_max = ImMax(v_max, v);
        }
        if (*scale_min == FLT_MAX)
            *scale_min = v_min;
        if (*scale_max == FLT_MAX)
            *scale_max = v_max; 
    }
}

void draw_lines_fit(ImRect frame_bb, float *pData, int values_count)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImGui::PushClipRect(frame_bb.Min, frame_bb.Max, true);

    float scale_max_x = FLT_MAX, scale_min_x = FLT_MAX;
    float scale_max_y = FLT_MAX, scale_min_y = FLT_MAX;
    get_max_min(&pData[0], 2, values_count, &scale_max_x, &scale_min_x);
    get_max_min(&pData[1], 2, values_count, &scale_max_y, &scale_min_y);

    const float inv_scale_y = (scale_min_y == scale_max_y) ? 0.0f : (1.0f / (scale_max_y - scale_min_y));
    const float inv_scale_x = (scale_min_x == scale_max_x) ? 0.0f : (1.0f / (scale_max_x - scale_min_x));
    
    ImU32 col = IM_COL32(200, 200, 200, 200);
    
    ImVec2 tp0 = ImVec2( 
        0.0f, 
        1.0f - ImSaturate((pData[2*0+1] - scale_min_y) * inv_scale_y) 
    );
    ImVec2 pos0 = ImLerp(frame_bb.Min, frame_bb.Max, tp0);
    for (int i = 1; i < values_count; i++)
    {
        const ImVec2 tp1 = ImVec2( 
            0.0f + ImSaturate((pData[2*i+0] - scale_min_x) * inv_scale_x) , 
            1.0f - ImSaturate((pData[2*i+1] - scale_min_y) * inv_scale_y) 
        );
        ImVec2 pos1 = ImLerp(frame_bb.Min, frame_bb.Max, tp1);
        window->DrawList->AddLine(pos0, pos1, col);
        pos0 = pos1;
        if (tp1.x>1.0f)
            break;
    }    

    ImGui::PopClipRect();
}

static float lerp( float min, float max, float t )
{
    return min*(1.0-t) + max*t;
}

void draw_lines(ImRect frame_bb, float *pData, int values_count, ImU32 col, float scale_min_y, float scale_max_y)
{
    if (pData==NULL || values_count==0)
        return;
        
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImGui::PushClipRect(frame_bb.Min, frame_bb.Max, true);

    get_max_min(&pData[1], 2, values_count, &scale_max_y, &scale_min_y);

    const float inv_scale_y = (scale_min_y == scale_max_y) ? 0.0f : (1.0f / (scale_max_y - scale_min_y));

    std::vector<ImVec2> polyline_points;
    polyline_points.reserve(static_cast<size_t>(values_count));

    int current_pixel_x = -1;
    for (int i = 0; i < values_count; i++)
    {
        const ImVec2 pos = ImVec2(
            lerp(frame_bb.Min.x, frame_bb.Max.x, pData[2 * i + 0]),
            lerp(frame_bb.Max.y, frame_bb.Min.y, ImSaturate((pData[2 * i + 1] - scale_min_y) * inv_scale_y)));

        if (pos.x > frame_bb.Max.x)
            break;

        const int pixel_x = static_cast<int>(pos.x);
        if (!polyline_points.empty() && pixel_x == current_pixel_x)
        {
            // Collapse points that land on the same screen column to keep the spectrum
            // visually identical while avoiding ImGui's 16-bit index overflow on 8192 FFT.
            if (pos.y < polyline_points.back().y)
                polyline_points.back() = pos;
            continue;
        }

        polyline_points.push_back(pos);
        current_pixel_x = pixel_x;
    }

    if (polyline_points.size() >= 2)
        window->DrawList->AddPolyline(polyline_points.data(), static_cast<int>(polyline_points.size()), col, 0, 1.0f);
    else if (polyline_points.size() == 1)
        window->DrawList->AddCircleFilled(polyline_points[0], 1.5f, col);

    ImGui::PopClipRect();
}

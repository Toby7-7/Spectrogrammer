#pragma once

#include <cfloat>

#include "imgui.h"

bool block_add(const char *label, const ImVec2& size_arg, ImRect *pFrame_bb, bool *pHovered);
void draw_lines(ImRect frame_bb, float *pData, int values_count, ImU32 col, float scale_min_y = FLT_MAX, float scale_max_y = FLT_MAX);
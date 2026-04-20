#pragma once

#include <stdint.h>

#include "imgui.h"

void Init_waterfall(int16_t width, int16_t height);
void Draw_waterfall(ImRect frame_bb);
void Shutdown_waterfall();
void Reset_waterfall_storage();
void Draw_update(float *pData, uint32_t size);
void Draw_vertical_scale(ImRect frame_bb, float seconds_per_row);

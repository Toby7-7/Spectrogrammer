#pragma once

#include <stdint.h>

#include "imgui.h"

void Init_waterfall(int channel, int16_t width, int16_t height);
void Draw_waterfall(int channel, ImRect frame_bb);
void Shutdown_waterfall();
void Reset_waterfall_storage(int channel = -1);
void Draw_update(int channel, float *pData, uint32_t size);
void Draw_vertical_scale(ImRect frame_bb, float seconds_per_row);

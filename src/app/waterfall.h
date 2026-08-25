#pragma once

#include <stdint.h>

#include <vector>

#include "imgui.h"

void Init_waterfall(int channel, int16_t width, int16_t height);
void Draw_waterfall(int channel, ImRect frame_bb);
void Shutdown_waterfall();
void Reset_waterfall_storage(int channel = -1);
// Called by the processing thread; safe against the UI thread.
void Draw_update(int channel, float *pData, uint32_t size);
void Draw_vertical_scale(ImRect frame_bb, float seconds_per_row);

// Export accessors (UI thread): flushes a pending upload, then exposes the
// latest complete RGB565 image (row 0 = newest, on top).
void Waterfall_applyPendingUpload(int channel);
const std::vector<uint16_t> *Waterfall_imageData(int channel);
int Waterfall_imageWidth(int channel);
int Waterfall_imageHeight(int channel);

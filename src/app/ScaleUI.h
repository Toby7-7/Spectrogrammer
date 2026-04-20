#include "imgui.h"
#include "imgui_internal.h"
#include "ScaleBufferX.h"
#include "ScaleBufferY.h"

void draw_frequency_scale(ImRect frame_bb, ScaleBufferBase *pScaleBufferX, FrequencyAxisScale curve);
void draw_scale_y(ImRect frame_bb, float min, float max);


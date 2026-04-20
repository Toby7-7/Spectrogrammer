
#pragma once

#include "audio_common.h"

bool Audio_init(unsigned int sampleRate, int framesPerBuf, int recordingPreset);
void Audio_deinit();
float Audio_getSampleRate();
void Audio_getBufferQueues(AudioQueue **pFreeQ, AudioQueue **pRecQ);
bool Audio_startPlay();




#pragma once

#include "audio_common.h"

bool Audio_init(unsigned int sampleRate, int framesPerBuf, int recordingPreset, int inputChannels);
void Audio_deinit();
float Audio_getSampleRate();
int Audio_getInputChannelCount();
void Audio_getBufferQueues(AudioQueue **pFreeQ, AudioQueue **pRecQ);
bool Audio_startPlay();


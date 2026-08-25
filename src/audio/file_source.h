// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0
//
// FileAudio: plays back a local audio file (WAV) through the exact same
// sample_buf / AudioQueue / ChunkerProcessor pipeline used for live capture,
// so spectrum, waterfall, peak hold, markers, cursor and zoom behave
// identically to a live session.
//
// The feeder thread paces playback in real time (1x). Callers must only touch
// FileAudio_* from the UI thread while the processing thread is stopped
// (same contract as the live Audio_* API).

#ifndef FILE_AUDIO_H
#define FILE_AUDIO_H

#include <sys/types.h>
#include "buf_manager.h"

// Loads `path`, creates the queues/buffers and (optionally) positions the
// playback at `startFrame`. Returns false on error; see FileAudio_errorText().
bool FileAudio_init(const char *path, int framesPerBuf, long long startFrame = 0);

void FileAudio_getBufferQueues(AudioQueue **pFreeQ, AudioQueue **pRecQ);
int FileAudio_getInputChannelCount();
float FileAudio_getSampleRate();

// Starts (or resumes) the realtime feeder.
bool FileAudio_startPlay();
// Pauses the feeder; already-queued audio is drained by the consumer.
void FileAudio_pausePlay();
bool FileAudio_isPlaying();

// True once the last frame of the file has been pushed to the rec queue.
bool FileAudio_atEnd();
// 0..1 playback progress (absolute position within the file).
double FileAudio_getProgress();
long long FileAudio_positionFrames();
long long FileAudio_totalFrames();

const char *FileAudio_fileName();
const char *FileAudio_errorText();
bool FileAudio_active();

// Stops the feeder and releases everything. Safe to call when not active.
void FileAudio_deinit();

#endif // FILE_AUDIO_H

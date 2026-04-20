#ifdef SDL_DRIVER

#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#define LOGW printf
#include "buf_manager.h"

#define BUF_COUNT 64
static uint32_t bufCount_ = BUF_COUNT;
static sample_buf *bufs_;
static AudioQueue *freeQueue_;
static AudioQueue *recQueue_;
static SDL_AudioDeviceID audioDevice;
static int32_t buffer_frames = 1024;
static bool recording = false;
static pthread_t capture_thread;
static int input_channels_ = 1;

bool Audio_init(unsigned int sampleRate, int framesPerBuf, int recordingPreset, int inputChannels) {
    (void)recordingPreset;
    SDL_Init(SDL_INIT_AUDIO);

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = sampleRate;
    desired.format = AUDIO_S16LSB;
    desired.channels = inputChannels <= 1 ? 1 : 2;
    desired.samples = framesPerBuf;
    desired.callback = NULL;  // We’ll pull manually

    audioDevice = SDL_OpenAudioDevice(NULL, 1, &desired, &obtained, 0);
    if (audioDevice == 0) {
        fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    input_channels_ = obtained.channels <= 1 ? 1 : 2;
    buffer_frames = obtained.samples;
    uint32_t bufSize = buffer_frames * input_channels_ * 2;
    bufs_ = allocateSampleBufs(bufCount_, bufSize);
    assert(bufs_);

    freeQueue_ = new AudioQueue(bufCount_);
    recQueue_ = new AudioQueue(bufCount_);
    for (uint32_t i = 0; i < bufCount_; i++) {
        freeQueue_->push(&bufs_[i]);
    }

    SDL_PauseAudioDevice(audioDevice, 0);  // Start recording
    return true;
}

void Audio_getBufferQueues(AudioQueue **pFreeQ, AudioQueue **pRecQ) {
    *pFreeQ = freeQueue_;
    *pRecQ = recQueue_;
}

int Audio_getInputChannelCount() {
    return input_channels_;
}

static void* capture_thread_fn(void *v) {
    while (recording) {
        sample_buf *buf;
        if (freeQueue_->front(&buf)) {
            freeQueue_->pop();

            int bytesRead = SDL_DequeueAudio(audioDevice, buf->buf_, buffer_frames * input_channels_ * 2);
            if (bytesRead <= 0) continue;

            buf->size_ = bytesRead;
            recQueue_->push(buf);
        }
        SDL_Delay(10);  // Prevent busy loop
    }
    return NULL;
}

bool Audio_startPlay() {
    recording = true;
    pthread_create(&capture_thread, NULL, capture_thread_fn, NULL);
    return true;
}

void Audio_deinit() {
    recording = false;
    pthread_join(capture_thread, NULL);

    SDL_CloseAudioDevice(audioDevice);
    SDL_Quit();

    delete recQueue_;
    delete freeQueue_;
    releaseSampleBufs(bufs_, bufCount_);
    fprintf(stdout, "audio interface closed\n");
}

#endif // SDL_DRIVER

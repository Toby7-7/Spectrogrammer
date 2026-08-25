/*
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "audio_recorder.h"
#include "audio_common.h"
#include <sys/types.h>
#include <cassert>
#include <cstring>

struct EchoAudioEngine {
  SLmilliHertz fastPathSampleRate_;
  uint32_t fastPathFramesPerBuf_;
  uint16_t sampleChannels_;
  uint16_t bitsPerSample_;

  SLObjectItf slEngineObj_;
  SLEngineItf slEngineItf_;

  AudioRecorder *recorder_;
  AudioQueue *freeBufQueue_;  // Owner of the queue
  AudioQueue *recBufQueue_;   // Owner of the queue

  sample_buf *bufs_;
  uint32_t bufCount_;
  uint32_t frameCount_;
  int recordingPreset_;
};
static EchoAudioEngine engine;

bool EngineService(void *ctx, uint32_t msg, void *data);
bool Audio_createAudioRecorder();

// Releases everything Audio_init allocated.  Safe to call at any time, and
// idempotent.
static void Audio_releaseAll() {
  if (engine.recorder_ != nullptr) {
    delete engine.recorder_;
    engine.recorder_ = nullptr;
  }
  if (engine.recBufQueue_ != nullptr) {
    delete engine.recBufQueue_;
    engine.recBufQueue_ = nullptr;
  }
  if (engine.freeBufQueue_ != nullptr) {
    delete engine.freeBufQueue_;
    engine.freeBufQueue_ = nullptr;
  }
  if (engine.bufs_ != nullptr) {
    releaseSampleBufs(engine.bufs_, engine.bufCount_);
    engine.bufs_ = nullptr;
    engine.bufCount_ = 0;
  }
  if (engine.slEngineObj_ != nullptr) {
    (*engine.slEngineObj_)->Destroy(engine.slEngineObj_);
    engine.slEngineObj_ = nullptr;
    engine.slEngineItf_ = nullptr;
  }
}

bool Audio_init(unsigned int sampleRate, int framesPerBuf, int recordingPreset, int inputChannels) {
  // Guard against double init leaking the previous engine.
  Audio_releaseAll();
  memset(&engine, 0, sizeof(engine));

  engine.fastPathSampleRate_ = static_cast<SLmilliHertz>(sampleRate);
  engine.fastPathFramesPerBuf_ = static_cast<uint32_t>(framesPerBuf);
  engine.sampleChannels_ = static_cast<uint16_t>(inputChannels <= 1 ? 1 : 2);
  engine.bitsPerSample_ = SL_PCMSAMPLEFORMAT_FIXED_16;
  engine.recordingPreset_ = recordingPreset;

  SLresult result = slCreateEngine(&engine.slEngineObj_, 0, NULL, 0, NULL, NULL);
  if (result != SL_RESULT_SUCCESS) {
    LOGE("slCreateEngine failed: 0x%08x", result);
    return false;
  }

  result = (*engine.slEngineObj_)->Realize(engine.slEngineObj_, SL_BOOLEAN_FALSE);
  if (result != SL_RESULT_SUCCESS) {
    LOGE("Realize(engine) failed: 0x%08x", result);
    Audio_releaseAll();
    return false;
  }

  result = (*engine.slEngineObj_)->GetInterface(engine.slEngineObj_, SL_IID_ENGINE, &engine.slEngineItf_);
  if (result != SL_RESULT_SUCCESS) {
    LOGE("GetInterface(SL_IID_ENGINE) failed: 0x%08x", result);
    Audio_releaseAll();
    return false;
  }

  // compute the RECOMMENDED fast audio buffer size:
  //   the lower latency required
  //     *) the smaller the buffer should be (adjust it here) AND
  //     *) the less buffering should be before starting player AFTER
  //        receiving the recorder buffer
  uint32_t bufSize = engine.fastPathFramesPerBuf_ * engine.sampleChannels_ * engine.bitsPerSample_;
  bufSize = (bufSize + 7) >> 3;  // bits --> byte
  engine.bufCount_ = BUF_COUNT;
  engine.bufs_ = allocateSampleBufs(engine.bufCount_, bufSize);
  if (engine.bufs_ == nullptr) {
    LOGE("allocateSampleBufs failed (%u bufs x %u bytes)", engine.bufCount_, bufSize);
    Audio_releaseAll();
    return false;
  }

  engine.freeBufQueue_ = new AudioQueue(engine.bufCount_);
  engine.recBufQueue_ = new AudioQueue(engine.bufCount_);
  if (engine.freeBufQueue_ == nullptr || engine.recBufQueue_ == nullptr) {
    LOGE("AudioQueue allocation failed");
    Audio_releaseAll();
    return false;
  }
  for (uint32_t i = 0; i < engine.bufCount_; i++) {
    engine.freeBufQueue_->push(&engine.bufs_[i]);
  }

  if (!Audio_createAudioRecorder()) {
    LOGE("Audio_createAudioRecorder failed");
    Audio_releaseAll();
    return false;
  }

  LOGI("Audio_init ok: rate=%u frames=%u channels=%u preset=%d",
       (unsigned)sampleRate, (unsigned)framesPerBuf,
       (unsigned)engine.sampleChannels_, recordingPreset);
  return true;
}

void Audio_getBufferQueues(AudioQueue **pFreeQ, AudioQueue **pRecQ)
{
  *pFreeQ = engine.freeBufQueue_;
  *pRecQ = engine.recBufQueue_;
}

int Audio_getInputChannelCount()
{
    return static_cast<int>(engine.sampleChannels_);
}

bool Audio_createAudioRecorder()
{
  SampleFormat sampleFormat;
  memset(&sampleFormat, 0, sizeof(sampleFormat));
  sampleFormat.pcmFormat_ = static_cast<uint16_t>(engine.bitsPerSample_);

  // SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
  sampleFormat.channels_ = engine.sampleChannels_;
  sampleFormat.sampleRate_ = engine.fastPathSampleRate_;
  sampleFormat.framesPerBuf_ = engine.fastPathFramesPerBuf_;
  engine.recorder_ = new AudioRecorder(&sampleFormat, engine.slEngineItf_, static_cast<SLuint32>(engine.recordingPreset_));
  if (engine.recorder_ == nullptr || !engine.recorder_->IsValid()) {
    LOGE("AudioRecorder invalid");
    delete engine.recorder_;
    engine.recorder_ = nullptr;
    return false;
  }
  engine.recorder_->SetBufQueues(engine.freeBufQueue_, engine.recBufQueue_);
  engine.recorder_->RegisterCallback(EngineService, (void *)&engine);
  return true;
}

bool Audio_startPlay() {
  if (engine.recorder_ == nullptr || !engine.recorder_->IsValid())
    return false;
  engine.frameCount_ = 0;
  return engine.recorder_->Start() == SL_BOOLEAN_TRUE;
}

uint32_t dbgEngineGetBufCount() {
  uint32_t count = 0;
  count += engine.recorder_ ? (uint32_t)engine.recorder_->dbgGetDevBufCount() : 0;
  count += engine.freeBufQueue_ ? engine.freeBufQueue_->size() : 0;
  count += engine.recBufQueue_ ? engine.recBufQueue_->size() : 0;

  LOGI("Buf distributions: RecDev=%u FreeQ=%u RecQ=%u",
       engine.recorder_ ? (uint32_t)engine.recorder_->dbgGetDevBufCount() : 0,
       engine.freeBufQueue_ ? engine.freeBufQueue_->size() : 0,
       engine.recBufQueue_ ? engine.recBufQueue_->size() : 0);
  if (count != engine.bufCount_) {
    LOGE("====Lost Bufs among the queue(supposed = %d, found = %d)", BUF_COUNT, count);
  }
  return count;
}

/*
 * simple message passing for player/recorder to communicate with engine
 */
bool EngineService(void *ctx, uint32_t msg, void *data) {
  assert(ctx == &engine);
  switch (msg) {
    case ENGINE_SERVICE_MSG_RETRIEVE_DUMP_BUFS: {
      *(static_cast<uint32_t *>(data)) = dbgEngineGetBufCount();
      break;
    }
    case ENGINE_SERVICE_MSG_RECORDED_AUDIO_AVAILABLE: {
      sample_buf *buf = static_cast<sample_buf *>(data);
      assert(engine.fastPathFramesPerBuf_ == buf->size_ / engine.sampleChannels_ / (engine.bitsPerSample_ / 8));
      break;
    }
    default:
      LOGE("EngineService: unknown message %u", msg);
      return false;
  }

  return true;
}

void Audio_deinit()
{
    Audio_releaseAll();
}

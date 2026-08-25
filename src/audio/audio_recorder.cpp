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

#include <cstring>
#include <cstdlib>
#include <chrono>
#include "audio_recorder.h"
#include "audio_common.h"
#include <android/log.h>

void ConvertToSLSampleFormat(SLAndroidDataFormat_PCM_EX* pFormat, SampleFormat* pSampleInfo_)
{
    assert(pFormat);
    memset(pFormat, 0, sizeof(*pFormat));

    pFormat->formatType = SL_DATAFORMAT_PCM;
    // Only support 2 channels
    // For channelMask, refer to wilhelm/src/android/channels.c for details
    if (pSampleInfo_->channels_ <= 1)
    {
        pFormat->numChannels = 1;
        pFormat->channelMask = SL_SPEAKER_FRONT_LEFT;
    }
    else
    {
        pFormat->numChannels = 2;
        pFormat->channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    }
    pFormat->sampleRate = pSampleInfo_->sampleRate_ * 1000;

    pFormat->endianness = SL_BYTEORDER_LITTLEENDIAN;
    pFormat->bitsPerSample = pSampleInfo_->pcmFormat_;
    pFormat->containerSize = pSampleInfo_->pcmFormat_;

    /*
     * fixup for android extended representations...
     */
    pFormat->representation = pSampleInfo_->representation_;
    switch (pFormat->representation)
    {
    case SL_ANDROID_PCM_REPRESENTATION_UNSIGNED_INT:
        pFormat->bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_8;
        pFormat->containerSize = SL_PCMSAMPLEFORMAT_FIXED_8;
        pFormat->formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
        break;
    case SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT:
        pFormat->bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;  // supports 16, 24, and 32
        pFormat->containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
        pFormat->formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
        break;
    case SL_ANDROID_PCM_REPRESENTATION_FLOAT:
        pFormat->bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_32;
        pFormat->containerSize = SL_PCMSAMPLEFORMAT_FIXED_32;
        pFormat->formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
        break;
    case 0:
        break;
    default:
        assert(0);
    }
}

/*
 * bqRecorderCallback(): called for every full device buffer;
 *                       move it to the app queue and notify the handler.
 *                       Buffer re-arming happens in the pump thread.
 */
void bqRecorderCallback(SLAndroidSimpleBufferQueueItf bq, void* rec)
{
    (static_cast<AudioRecorder*>(rec))->ProcessSLCallback(bq);
}

void AudioRecorder::ProcessSLCallback(SLAndroidSimpleBufferQueueItf bq)
{
    assert(bq == recBufQueueItf_);
#ifdef ENABLE_LOG
    recLog_->logTime();
#endif

    sample_buf* dataBuf = nullptr;
    if (!devShadowQueue_->front(&dataBuf))
    {
        // Device delivered a buffer we never tracked - ignore instead of
        // dereferencing a null pointer (release builds have no asserts).
        LOGE("ProcessSLCallback: empty shadow queue");
        return;
    }
    devShadowQueue_->pop();
    dataBuf->size_ = dataBuf->cap_;  // device only calls us when it is really full
    recQueue_->push(dataBuf);

    if (callback_ != nullptr)
        callback_(ctx_, ENGINE_SERVICE_MSG_RECORDED_AUDIO_AVAILABLE, dataBuf);

    // No Enqueue here: the pump thread refills the device.
}

AudioRecorder::AudioRecorder(SampleFormat* sampleFormat, SLEngineItf slEngine, SLuint32 recordingPreset)
    : freeQueue_(nullptr),
      recQueue_(nullptr),
      devShadowQueue_(nullptr),
      callback_(nullptr),
      ctx_(nullptr)
{
    SLresult result;
    sampleInfo_ = *sampleFormat;
    SLAndroidDataFormat_PCM_EX format_pcm;
    ConvertToSLSampleFormat(&format_pcm, &sampleInfo_);

    // configure audio source
    SLDataLocator_IODevice loc_dev = {SL_DATALOCATOR_IODEVICE,
                                      SL_IODEVICE_AUDIOINPUT,
                                      SL_DEFAULTDEVICEID_AUDIOINPUT, nullptr};
    SLDataSource audioSrc = {&loc_dev, nullptr};

    // configure audio sink
    SLDataLocator_AndroidSimpleBufferQueue loc_bq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, DEVICE_SHADOW_BUFFER_QUEUE_LEN};

    SLDataSink audioSnk = {&loc_bq, &format_pcm};

    // create audio recorder
    // (requires the RECORD_AUDIO permission)
    const SLInterfaceID id[2] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                 SL_IID_ANDROIDCONFIGURATION};
    const SLboolean req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*slEngine)->CreateAudioRecorder(slEngine, &recObjectItf_, &audioSrc, &audioSnk, sizeof(id) / sizeof(id[0]), id, req);
    if (result != SL_RESULT_SUCCESS)
    {
        LOGE("CreateAudioRecorder failed: 0x%08x", result);
        recObjectItf_ = nullptr;
        return;
    }

    // Configure the voice recognition preset which has no signal processing for lower latency.
    SLAndroidConfigurationItf inputConfig;
    result = (*recObjectItf_)->GetInterface(recObjectItf_, SL_IID_ANDROIDCONFIGURATION, &inputConfig);
    if (SL_RESULT_SUCCESS == result)
    {
        SLuint32 presetValue = recordingPreset;
        (*inputConfig)->SetConfiguration(inputConfig, SL_ANDROID_KEY_RECORDING_PRESET, &presetValue, sizeof(SLuint32));
    }

    result = (*recObjectItf_)->Realize(recObjectItf_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS)
    {
        LOGE("Realize(recorder) failed: 0x%08x", result);
        return;
    }
    result = (*recObjectItf_)->GetInterface(recObjectItf_, SL_IID_RECORD, &recItf_);
    if (result != SL_RESULT_SUCCESS)
    {
        LOGE("GetInterface(SL_IID_RECORD) failed: 0x%08x", result);
        return;
    }

    result = (*recObjectItf_)->GetInterface(recObjectItf_, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &recBufQueueItf_);
    if (result != SL_RESULT_SUCCESS)
    {
        LOGE("GetInterface(SL_IID_ANDROIDSIMPLEBUFFERQUEUE) failed: 0x%08x", result);
        return;
    }

    result = (*recBufQueueItf_)->RegisterCallback(recBufQueueItf_, bqRecorderCallback, this);
    if (result != SL_RESULT_SUCCESS)
    {
        LOGE("RegisterCallback failed: 0x%08x", result);
        return;
    }

    devShadowQueue_ = new AudioQueue(DEVICE_SHADOW_BUFFER_QUEUE_LEN);
    if (!devShadowQueue_)
        return;
#ifdef ENABLE_LOG
    std::string name = "rec";
    recLog_ = new AndroidLog(name);
#endif
}

bool AudioRecorder::IsValid() const
{
    return recObjectItf_ != nullptr && recItf_ != nullptr && recBufQueueItf_ != nullptr && devShadowQueue_ != nullptr;
}

void AudioRecorder::pumpLoop()
{
    while (m_pumpRunning.load())
    {
        size_t shadowUsed = devShadowQueue_->size();

        // Refill the device with free application buffers.
        while (shadowUsed < DEVICE_SHADOW_BUFFER_QUEUE_LEN)
        {
            sample_buf* buf = nullptr;
            if (!freeQueue_->front(&buf))
                break;
            freeQueue_->pop();
            if (!devShadowQueue_->push(buf))
            {
                // Should not happen; put the buffer back.
                freeQueue_->push(buf);
                break;
            }
            const SLresult result = (*recBufQueueItf_)->Enqueue(recBufQueueItf_, buf->buf_, buf->cap_);
            if (result != SL_RESULT_SUCCESS)
            {
                LOGE("pump: Enqueue failed: 0x%08x", result);
                devShadowQueue_->pop();
                freeQueue_->push(buf);
                break;
            }
            shadowUsed++;
        }

        // Drive the device toward the desired record state.
        SLuint32 state = SL_RECORDSTATE_STOPPED;
        if ((*recItf_)->GetRecordState(recItf_, &state) == SL_RESULT_SUCCESS)
        {
            if (m_recordingDesired.load() && state != SL_RECORDSTATE_RECORDING)
            {
                // Wait until at least one buffer is enqueued so the device
                // never runs empty.
                if (shadowUsed > 0)
                    (*recItf_)->SetRecordState(recItf_, SL_RECORDSTATE_RECORDING);
            }
            else if (!m_recordingDesired.load() && state != SL_RECORDSTATE_STOPPED)
            {
                (*recItf_)->SetRecordState(recItf_, SL_RECORDSTATE_STOPPED);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(shadowUsed < DEVICE_SHADOW_BUFFER_QUEUE_LEN ? 2 : 10));
    }
}

void AudioRecorder::startPump()
{
    if (m_pumpRunning.exchange(true))
        return;
    m_pumpThread = std::thread(&AudioRecorder::pumpLoop, this);
}

void AudioRecorder::stopPump()
{
    m_pumpRunning.store(false);
    if (m_pumpThread.joinable())
        m_pumpThread.join();
}

SLboolean AudioRecorder::Start()
{
    if (!IsValid() || !freeQueue_ || !recQueue_ || !devShadowQueue_)
    {
        LOGE("Start: invalid state (freeQueue=%p recQueue=%p shadow=%p)", (void*)freeQueue_, (void*)recQueue_, (void*)devShadowQueue_);
        return SL_BOOLEAN_FALSE;
    }
    audioBufCount = 0;

    // Stop any pump from a previous Start first.
    m_recordingDesired.store(false);
    stopPump();

    SLresult result;
    // in case already recording, stop recording and clear buffer queue
    result = (*recItf_)->SetRecordState(recItf_, SL_RECORDSTATE_STOPPED);
    if (result != SL_RESULT_SUCCESS)
        return SL_BOOLEAN_FALSE;
    result = (*recBufQueueItf_)->Clear(recBufQueueItf_);
    if (result != SL_RESULT_SUCCESS)
        return SL_BOOLEAN_FALSE;

    // Kick-start the device with a batch of buffers before the pump takes over.
    for (int i = 0; i < RECORD_DEVICE_KICKSTART_BUF_COUNT; i++)
    {
        sample_buf* buf = nullptr;
        if (!freeQueue_->front(&buf))
            break;
        freeQueue_->pop();
        assert(buf->buf_ && buf->cap_ && !buf->size_);

        result = (*recBufQueueItf_)->Enqueue(recBufQueueItf_, buf->buf_, buf->cap_);
        if (result != SL_RESULT_SUCCESS)
        {
            LOGE("Start: Enqueue failed: 0x%08x", result);
            devShadowQueue_->pop();
            freeQueue_->push(buf);
            return SL_BOOLEAN_FALSE;
        }
        devShadowQueue_->push(buf);
    }

    m_recordingDesired.store(true);
    startPump();

    result = (*recItf_)->SetRecordState(recItf_, SL_RECORDSTATE_RECORDING);
    if (result != SL_RESULT_SUCCESS)
    {
        LOGE("Start: SetRecordState(RECORDING) failed: 0x%08x", result);
        m_recordingDesired.store(false);
        stopPump();
        return SL_BOOLEAN_FALSE;
    }

    return SL_BOOLEAN_TRUE;
}

SLboolean AudioRecorder::Pause()
{
    if (!IsValid())
        return SL_BOOLEAN_FALSE;

    SLuint32 curState;
    SLresult result = (*recItf_)->GetRecordState(recItf_, &curState);
    if (result != SL_RESULT_SUCCESS)
        return SL_BOOLEAN_FALSE;

    if (curState == SL_RECORDSTATE_RECORDING)
    {
        SLresult result = (*recItf_)->SetRecordState(recItf_, SL_RECORDSTATE_PAUSED);
        if (result != SL_RESULT_SUCCESS)
            return SL_BOOLEAN_FALSE;
        return SL_BOOLEAN_TRUE;
    }
    else if (curState == SL_RECORDSTATE_PAUSED)
    {
        SLresult result = (*recItf_)->SetRecordState(recItf_, SL_RECORDSTATE_RECORDING);
        if (result != SL_RESULT_SUCCESS)
            return SL_BOOLEAN_FALSE;
        return SL_BOOLEAN_TRUE;
    }

    return SL_BOOLEAN_FALSE;
}

SLboolean AudioRecorder::Stop()
{
    if (!IsValid())
        return SL_BOOLEAN_FALSE;

    // Stop the pump first so no Enqueue/SetRecordState can race with the
    // device drain below.
    m_recordingDesired.store(false);
    stopPump();

    SLresult result;

    SLuint32 curState;
    result = (*recItf_)->GetRecordState(recItf_, &curState);
    if (result != SL_RESULT_SUCCESS)
        return SL_BOOLEAN_FALSE;
    if (curState == SL_RECORDSTATE_STOPPED)
    {
        return SL_BOOLEAN_TRUE;
    }
    result = (*recItf_)->SetRecordState(recItf_, SL_RECORDSTATE_STOPPED);
    if (result != SL_RESULT_SUCCESS)
        return SL_BOOLEAN_FALSE;
    result = (*recBufQueueItf_)->Clear(recBufQueueItf_);
    if (result != SL_RESULT_SUCCESS)
        return SL_BOOLEAN_FALSE;

#ifdef ENABLE_LOG
    recLog_->flush();
#endif

    return SL_BOOLEAN_TRUE;
}

AudioRecorder::~AudioRecorder()
{
    Stop();

    if (recObjectItf_ != nullptr)
    {
        (*recObjectItf_)->Destroy(recObjectItf_);
        recObjectItf_ = nullptr;
    }
    recItf_ = nullptr;
    recBufQueueItf_ = nullptr;

    if (devShadowQueue_)
    {
        sample_buf* buf = nullptr;
        while (devShadowQueue_->front(&buf))
        {
            devShadowQueue_->pop();
            if (freeQueue_ != nullptr)
                freeQueue_->push(buf);
        }
        delete (devShadowQueue_);
        devShadowQueue_ = nullptr;
    }

#ifdef ENABLE_LOG
    if (recLog_)
    {
        delete recLog_;
        recLog_ = nullptr;
    }
#endif
}

void AudioRecorder::SetBufQueues(AudioQueue* freeQ, AudioQueue* recQ)
{
    assert(freeQ && recQ);
    freeQueue_ = freeQ;
    recQueue_ = recQ;
}

void AudioRecorder::RegisterCallback(ENGINE_CALLBACK cb, void* ctx)
{
    callback_ = cb;
    ctx_ = ctx;
}

int32_t AudioRecorder::dbgGetDevBufCount()
{
    return devShadowQueue_ ? (int32_t)devShadowQueue_->size() : 0;
}

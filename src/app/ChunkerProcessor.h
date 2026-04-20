#pragma once

#include "Processor.h"
#define LOGW(...)
#include "buf_manager.h"

#define QUEUE_SIZE 32

class ChunkerProcessor
{
    bool m_started = false;
    int m_offsetFrames = 0;
    int m_srcOffsetFrames = 0;
    int m_destOffsetFrames = 0;
    int m_bufferIndex = 0;
    int m_inputChannels = 1;

    AudioQueue *m_pRecQueue = NULL;
    AudioQueue *m_pFreeQueue = NULL;

    bool PrepareBuffer(Processor **pSpectra, int spectrumCount);
    AU_FORMAT *GetSampleData(sample_buf *b0)
    {
        return (AU_FORMAT *)b0->buf_;
    }

public:
    void begin();
    void end();
    void SetQueues(AudioQueue *pRecQueue, AudioQueue *pFreeQueue, int inputChannels);
    bool releaseUsedAudioChunks();
    void releaseAllAudioChunks();
    bool Process(Processor **pSpectra, int spectrumCount, int hopSamples);
};

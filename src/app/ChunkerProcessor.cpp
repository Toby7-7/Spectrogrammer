#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "ChunkerProcessor.h"
#include "auformat.h"

void ChunkerProcessor::begin()
{
    assert(m_started == false);
    m_offsetFrames = 0;
    m_started = true;
    m_srcOffsetFrames = 0;
    m_destOffsetFrames = 0;
    m_bufferIndex = 0;
}

void ChunkerProcessor::end()
{
    assert(m_started == true);
    m_started = false;
}

bool ChunkerProcessor::releaseUsedAudioChunks()
{
    assert(m_pRecQueue != NULL);

    sample_buf *front = nullptr;
    while (m_pRecQueue->front(&front))
    {
        assert(front != NULL);

        int frontSize = AU_LEN(front->cap_) / m_inputChannels;

        if (m_offsetFrames < frontSize)
            return true;

        m_pRecQueue->pop();
        m_pFreeQueue->push(front);

        m_offsetFrames -= frontSize;
    }

    return false;
}

void ChunkerProcessor::SetQueues(AudioQueue *pRecQueue, AudioQueue *pFreeQueue, int inputChannels)
{
    assert(pRecQueue != NULL);
    assert(pFreeQueue != NULL);

    m_pRecQueue = pRecQueue;
    m_pFreeQueue = pFreeQueue;
    m_inputChannels = inputChannels <= 1 ? 1 : inputChannels;
}

void ChunkerProcessor::releaseAllAudioChunks()
{
    sample_buf *front = nullptr;
    while (m_pRecQueue != nullptr && m_pRecQueue->front(&front))
    {
        m_pRecQueue->pop();
        m_pFreeQueue->push(front);
    }
    m_offsetFrames = 0;
    m_srcOffsetFrames = 0;
    m_destOffsetFrames = 0;
    m_bufferIndex = 0;
}

bool ChunkerProcessor::PrepareBuffer(Processor **pSpectra, int spectrumCount)
{
    assert(pSpectra != NULL);
    assert(spectrumCount > 0);
    assert(pSpectra[0] != NULL);

    const int dataToWrite = pSpectra[0]->getProcessedLength();

    if (m_bufferIndex == 0)
    {
        if (releaseUsedAudioChunks() == false)
            return false;

        m_srcOffsetFrames = m_offsetFrames;
    }

    sample_buf *buf = nullptr;
    while (m_pRecQueue->peek(&buf, m_bufferIndex))
    {
        const int bufSizeFrames = AU_LEN(buf->cap_) / m_inputChannels;
        const int srcBufLeft = bufSizeFrames - m_srcOffsetFrames;
        const int destLeft = dataToWrite - m_destOffsetFrames;

        const int toWrite = std::min(destLeft, srcBufLeft);

        AU_FORMAT *ptrB0 = GetSampleData(buf) + (m_srcOffsetFrames * m_inputChannels);
        for (int channel = 0; channel < spectrumCount; channel++)
        {
            assert(pSpectra[channel] != NULL);
            pSpectra[channel]->convertShortToFFT(ptrB0 + channel, m_destOffsetFrames, toWrite, m_inputChannels);
        }

        m_destOffsetFrames += toWrite;
        m_srcOffsetFrames += toWrite;

        if (m_srcOffsetFrames == bufSizeFrames)
        {
            m_srcOffsetFrames = 0;
            m_bufferIndex++;
        }

        if (m_destOffsetFrames == dataToWrite)
        {
            m_destOffsetFrames = 0;
            m_bufferIndex = 0;
            return true;
        }
    }

    return false;
}

bool ChunkerProcessor::Process(Processor **pSpectra, int spectrumCount, int hopSamples)
{
    if (PrepareBuffer(pSpectra, spectrumCount))
    {
        m_offsetFrames += hopSamples;
        return true;
    }

    return false;
}

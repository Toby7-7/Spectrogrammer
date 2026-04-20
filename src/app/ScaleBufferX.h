#ifndef SCALEBUFFERX_H
#define SCALEBUFFERX_H

#include <algorithm>
#include <cmath>

#include "Processor.h"
#include "scale.h"
#include "BufferIO.h"
#include "ScaleBufferBase.h"

class ScaleBufferXCurve : public ScaleBufferBase
{
    Scale scaleBintoFreq;
    BufferIOInt m_binOffsets;
    FrequencyAxisScale m_curve;

public:
    explicit ScaleBufferXCurve(FrequencyAxisScale curve) : m_curve(curve) {}

    void setOutputWidth(int outputWidth, float minFreq, float maxFreq)
    {
        m_binOffsets.Resize(outputWidth);
        m_binOffsets.clear();
        scaleBintoFreq.init(m_curve, minFreq, maxFreq);
    }

    float XtoFreq(float x) const
    {
        return scaleBintoFreq.forward(x);
    }

    float FreqToX(float freq) const
    {
        return scaleBintoFreq.backward(freq);
    }

    void PreBuild(Processor *pProc)
    {
        int *pBins = m_binOffsets.GetData();
        for (int i = 0; i < m_binOffsets.GetSize(); i++)
        {
            float t = (float)i / (float)(m_binOffsets.GetSize() - 1.0f);
            float freq = XtoFreq(t);
            int bin = (int)floorf(pProc->freq2Bin(freq));
            pBins[i] = std::max(0, std::min(bin, pProc->getBinCount() - 1));
        }
    }

    void Build(BufferIODouble *pInput, BufferIODouble *pOutput)
    {
        pOutput->Resize(m_binOffsets.GetSize());

        float *pInputData = pInput->GetData();
        float *pOutputData = pOutput->GetData();
        int *pBins = m_binOffsets.GetData();

        pOutput->clear();

        for (int x = 0; x < m_binOffsets.GetSize(); x++)
        {
            int bin = std::max(0, std::min(pBins[x], pInput->GetSize() - 1));
            pOutputData[x] = std::max(pInputData[bin], pOutputData[x]);
        }
    }
};

#endif

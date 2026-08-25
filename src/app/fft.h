#ifndef MYFFT_H
#define MYFFT_H

#include <cassert>
#include <cmath>
#include <vector>
#include "Processor.h"
#include "kiss_fftr.h"
#include "auformat.h"

float hanning(int i, int window_size);
float hamming(int i, int window_size);
float blackman_harris(int i, int window_size);
float apply_window(WindowFunctionType windowFunction, int i, int window_size);

class myFFT : public Processor
{
    kiss_fft_cpx *m_out_cpx = nullptr;

    kiss_fftr_cfg m_cfg;

    BufferIODouble m_pInput_samples;
    BufferIODouble m_pOutput_bins;

    float m_fftScaling;
    float m_sampleRate;
    float m_freq_resolution;
    int m_decimationFactor = 1;
    WindowFunctionType m_windowFunction = WindowFunctionType::Hann;

    // Windowed-sinc anti-aliasing low-pass used before decimation (D > 1).
    // Without it, simply dropping samples aliases the band above the
    // decimated Nyquist frequency into the spectrum.
    std::vector<float> m_firCoeffs;
    std::vector<float> m_firWindow;
    int m_firRawSeen = 0;

    float m_lastPeakPower = 0.0f;
    float m_lastTotalPower = 0.0f;

    void buildFirFilter()
    {
        m_firCoeffs.clear();
        m_firWindow.clear();
        m_firRawSeen = 0;
        if (m_decimationFactor <= 1)
            return;

        const int taps = 31;
        // Cutoff slightly below the decimated Nyquist frequency, expressed
        // as a fraction of the raw sample rate.
        const float fc = 0.45f / (float)m_decimationFactor;
        const int m = (taps - 1) / 2;
        float norm = 0.0f;
        m_firCoeffs.resize(taps);
        for (int k = 0; k < taps; k++)
        {
            const float x = (float)(k - m);
            float sinc;
            if (x == 0.0f)
                sinc = 1.0f;
            else
            {
                const float arg = (float)M_PI * 2.0f * fc * x;
                sinc = sinf(arg) / arg;
            }
            const float hamming = 0.54f - 0.46f * cosf((2.0f * (float)M_PI * (float)k) / (float)(taps - 1));
            m_firCoeffs[k] = 2.0f * fc * sinc * hamming;
            norm += m_firCoeffs[k];
        }
        if (norm > 1e-9f)
            for (int k = 0; k < taps; k++)
                m_firCoeffs[k] /= norm;

        m_firWindow.assign(taps, 0.0f);
    }

    void init(int nfft)
    {
        m_cfg = kiss_fftr_alloc(nfft, false, 0, 0);
        m_pInput_samples.Resize(nfft);
        m_pOutput_bins.Resize(nfft/2+1);

        for (int i = 0; i < m_pInput_samples.GetSize(); i++)
            m_pInput_samples.GetData()[i]=0;
        for (int i = 0; i < m_pOutput_bins.GetSize(); i++)
            m_pOutput_bins.GetData()[i]=0;

        m_out_cpx = (kiss_fft_cpx*) malloc(sizeof(kiss_fft_cpx) * (nfft/2+1));

        m_fftScaling = 0;
        for(int i=0;i<m_pInput_samples.GetSize();i++)
            m_fftScaling += apply_window(m_windowFunction, i, m_pInput_samples.GetSize());
    }

    void deinit()
    {
        kiss_fftr_free(m_cfg);
        free(m_out_cpx);
        m_out_cpx = nullptr;
    }

public:
    ~myFFT()
    {
        deinit();
    }

    virtual const char *GetName() const {  return "FFT"; };

    void init(int nfft, float sampleRate, int decimationFactor, WindowFunctionType windowFunction)
    {
        m_decimationFactor = decimationFactor < 1 ? 1 : decimationFactor;
        m_windowFunction = windowFunction;
        init(nfft);
        m_sampleRate = sampleRate;
        m_freq_resolution = (sampleRate / (float)m_decimationFactor) / nfft;
        buildFirFilter();
    }

    int getBinCount() const { return m_pOutput_bins.GetSize(); }

    int getProcessedLength() const { return m_pInput_samples.GetSize() * m_decimationFactor; }

    void writeInputSample(float sample, int raw_index)
    {
        if (m_decimationFactor <= 1)
        {
            const int ii = raw_index;
            if (ii >= m_pInput_samples.GetSize())
                return;

            float *m_in_samples = m_pInput_samples.GetData();
            m_in_samples[ii] = sample * apply_window(m_windowFunction, ii, m_pInput_samples.GetSize());
            return;
        }

        const int taps = (int)m_firCoeffs.size();
        if (taps == 0)
            return;

        // Sliding window of the most recent `taps` raw samples.
        m_firWindow[raw_index % taps] = sample;
        if (m_firRawSeen < taps)
        {
            // Filter still warming up: collect the full window first.
            m_firRawSeen++;
            return;
        }

        if ((raw_index % m_decimationFactor) != 0)
            return;

        float acc = 0.0f;
        const int base = raw_index % taps;
        for (int k = 0; k < taps; k++)
        {
            const int idx = (base - k) % taps;
            acc += m_firCoeffs[k] * m_firWindow[idx < 0 ? idx + taps : idx];
        }

        const int ii = raw_index / m_decimationFactor;
        if (ii >= m_pInput_samples.GetSize())
            return;

        float *m_in_samples = m_pInput_samples.GetData();
        m_in_samples[ii] = acc * apply_window(m_windowFunction, ii, m_pInput_samples.GetSize());
    }

    void convertShortToFFT(const AU_FORMAT *input, int offsetDest, int length, int inputStride)
    {
        for (int i = 0; i < length; i++)
        {
            const int raw_index = i + offsetDest;
            writeInputSample(static_cast<float>(Uint16ToFloat(&input[i * inputStride])), raw_index);
        }
    }

    void convertFloatToFFT(const float *input, int offsetDest, int length)
    {
        for (int i = 0; i < length; i++)
        {
            const int raw_index = i + offsetDest;
            writeInputSample(input[i], raw_index);
        }
    }

    void computePower(float decay)
    {
        kiss_fftr( m_cfg , m_pInput_samples.GetData() , m_out_cpx );

        float totalPower = 0;
        float peakPower = 0;

        float *m_rout = m_pOutput_bins.GetData();
        for (int i = 0; i < getBinCount(); i++)
        {
            float power = sqrt(m_out_cpx[i].r * m_out_cpx[i].r + m_out_cpx[i].i * m_out_cpx[i].i);
            power *= (2 / m_fftScaling);
            m_rout[i] = m_rout[i] *decay + power*(1.0f-decay);

            totalPower += power;
            if (power > peakPower)
                peakPower = power;
        }
        m_lastTotalPower = totalPower;
        m_lastPeakPower = peakPower;
    }

    // Peak/total bin power of the last computePower() call, in full-scale
    // units (a full-scale sine peaks at ~1.0). Used for the level meter.
    float getLastPeakPower() const { return m_lastPeakPower; }
    float getLastTotalPower() const { return m_lastTotalPower; }

    float bin2Freq(int bin) const
    {
        return (float)(bin) * m_freq_resolution;
    }

    float freq2Bin(float freq) const
    {
        return ((float)freq / m_freq_resolution);
    }

    BufferIODouble *getBufferIO()
    {
        return &m_pOutput_bins;
    }
};

#endif

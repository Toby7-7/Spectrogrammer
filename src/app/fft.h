#ifndef MYFFT_H
#define MYFFT_H

#include <cassert>
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

    void init(int nfft)
    { 
        m_cfg = kiss_fftr_alloc(nfft, false, 0, 0);
        m_pInput_samples.Resize(nfft);
        m_pOutput_bins.Resize(nfft/2+1);
        
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
    }

public:
    ~myFFT()
    {
        deinit();
    }

    virtual const char *GetName() const {  return "FFT"; };

    void init(int nfft, float sampleRate, int decimationFactor, WindowFunctionType windowFunction)
    {
        m_decimationFactor = decimationFactor;
        m_windowFunction = windowFunction;
        init(nfft);
        m_sampleRate = sampleRate;
        m_freq_resolution = (sampleRate / (float)m_decimationFactor) / nfft;
    }

    int getBinCount() const { return m_pOutput_bins.GetSize(); }

    int getProcessedLength() const { return m_pInput_samples.GetSize() * m_decimationFactor; }

    void writeInputSample(float sample, int raw_index)
    {
        if ((raw_index % m_decimationFactor) != 0)
            return;

        const int ii = raw_index / m_decimationFactor;
        if (ii >= m_pInput_samples.GetSize())
            return;

        float *m_in_samples = m_pInput_samples.GetData();
        sample *= apply_window(m_windowFunction, ii, m_pInput_samples.GetSize());
        m_in_samples[ii] = sample;
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

        float *m_rout = m_pOutput_bins.GetData();
        for (int i = 0; i < getBinCount(); i++)
        {
            float power = sqrt(m_out_cpx[i].r * m_out_cpx[i].r + m_out_cpx[i].i * m_out_cpx[i].i);
            power *= (2 / m_fftScaling);
            m_rout[i] = m_rout[i] *decay + power*(1.0f-decay);


            totalPower += power;
        }
    }

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

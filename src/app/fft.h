#ifndef MYFFT_H
#define MYFFT_H

#include <cassert>
#include <cmath>
#include <vector>
#include <algorithm>
#include "Processor.h"
#include "kiss_fftr.h"
#include "auformat.h"

float hanning(int i, int window_size);
float hamming(int i, int window_size);
float blackman_harris(int i, int window_size);
float apply_window(WindowFunctionType windowFunction, int i, int window_size);

class myFFT : public Processor
{
    // Single-rate FFT structures (m_decimationFactor <= 1)
    kiss_fftr_cfg m_cfg = nullptr;
    kiss_fft_cpx *m_out_cpx = nullptr;
    BufferIODouble m_pInput_samples;

    // Dual-rate FFT structures (m_decimationFactor > 1)
    kiss_fftr_cfg m_cfg_decim = nullptr;
    kiss_fftr_cfg m_cfg_full = nullptr;
    kiss_fft_cpx *m_out_cpx_decim = nullptr;
    kiss_fft_cpx *m_out_cpx_full = nullptr;
    BufferIODouble m_pInput_decim;
    BufferIODouble m_pInput_full;
    BufferIODouble m_decim_power;
    BufferIODouble m_full_power;

    BufferIODouble m_pOutput_bins;

    int m_nfft = 0;
    float m_fftScaling = 0.0f;
    float m_invFftScaling = 0.0f;
    float m_sampleRate = 48000.0f;
    float m_freq_resolution = 0.0f;
    int m_decimationFactor = 1;
    WindowFunctionType m_windowFunction = WindowFunctionType::Hann;

    // Precomputed analysis window lookup table and anti-aliasing FIR filter.
    std::vector<float> m_window;
    std::vector<float> m_firCoeffs;
    std::vector<float> m_firWindow;

    float m_lastPeakPower = 0.0f;
    float m_lastTotalPower = 0.0f;

    void buildFirFilter()
    {
        m_firCoeffs.clear();
        m_firWindow.clear();
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

    void initBuffers(int nfft)
    {
        m_nfft = nfft;
        m_window.resize(nfft);
        m_fftScaling = 0.0f;
        for (int i = 0; i < nfft; i++)
        {
            const float w = apply_window(m_windowFunction, i, nfft);
            m_window[i] = w;
            m_fftScaling += w;
        }
        m_invFftScaling = (m_fftScaling > 0.0f) ? (2.0f / m_fftScaling) : 0.0f;

        if (m_decimationFactor <= 1)
        {
            m_cfg = kiss_fftr_alloc(nfft, false, 0, 0);
            m_out_cpx = (kiss_fft_cpx *)malloc(sizeof(kiss_fft_cpx) * (nfft / 2 + 1));
            m_pInput_samples.Resize(nfft);
            m_pInput_samples.clear();
            m_pOutput_bins.Resize(nfft / 2 + 1);
            m_pOutput_bins.clear();
        }
        else
        {
            m_cfg_decim = kiss_fftr_alloc(nfft, false, 0, 0);
            m_cfg_full = kiss_fftr_alloc(nfft, false, 0, 0);
            m_out_cpx_decim = (kiss_fft_cpx *)malloc(sizeof(kiss_fft_cpx) * (nfft / 2 + 1));
            m_out_cpx_full = (kiss_fft_cpx *)malloc(sizeof(kiss_fft_cpx) * (nfft / 2 + 1));
            m_pInput_decim.Resize(nfft);
            m_pInput_decim.clear();
            m_pInput_full.Resize(nfft);
            m_pInput_full.clear();
            m_decim_power.Resize(nfft / 2 + 1);
            m_decim_power.clear();
            m_full_power.Resize(nfft / 2 + 1);
            m_full_power.clear();

            const int totalBins = (m_decimationFactor * nfft) / 2 + 1;
            m_pOutput_bins.Resize(totalBins);
            m_pOutput_bins.clear();
        }
    }

    void deinit()
    {
        if (m_cfg)
        {
            kiss_fftr_free(m_cfg);
            m_cfg = nullptr;
        }
        if (m_cfg_decim)
        {
            kiss_fftr_free(m_cfg_decim);
            m_cfg_decim = nullptr;
        }
        if (m_cfg_full)
        {
            kiss_fftr_free(m_cfg_full);
            m_cfg_full = nullptr;
        }
        if (m_out_cpx)
        {
            free(m_out_cpx);
            m_out_cpx = nullptr;
        }
        if (m_out_cpx_decim)
        {
            free(m_out_cpx_decim);
            m_out_cpx_decim = nullptr;
        }
        if (m_out_cpx_full)
        {
            free(m_out_cpx_full);
            m_out_cpx_full = nullptr;
        }
    }

public:
    ~myFFT()
    {
        deinit();
    }

    virtual const char *GetName() const { return "DualRateFFT"; }

    void init(int nfft, float sampleRate, int decimationFactor, WindowFunctionType windowFunction)
    {
        deinit();
        m_decimationFactor = decimationFactor < 1 ? 1 : decimationFactor;
        m_windowFunction = windowFunction;
        m_sampleRate = sampleRate;
        initBuffers(nfft);
        m_freq_resolution = (sampleRate / (float)m_decimationFactor) / (float)nfft;
        buildFirFilter();
    }

    int getBinCount() const { return m_pOutput_bins.GetSize(); }

    int getProcessedLength() const { return m_nfft * m_decimationFactor; }

    void writeInputSample(float sample, int raw_index)
    {
        if (raw_index < 0)
            return;

        if (m_decimationFactor <= 1)
        {
            if (raw_index >= m_nfft)
                return;
            float *m_in_samples = m_pInput_samples.GetData();
            m_in_samples[raw_index] = sample * m_window[raw_index];
            return;
        }

        // 1. Full-rate path: temporally centered in the N*D raw chunk
        const int full_start = (m_nfft * m_decimationFactor - m_nfft) / 2;
        if (raw_index >= full_start && raw_index < full_start + m_nfft)
        {
            const int k = raw_index - full_start;
            m_pInput_full.GetData()[k] = sample * m_window[k];
        }

        // 2. Decimated path: 31-tap FIR low-pass filter + D downsampling
        const int taps = (int)m_firCoeffs.size();
        if (taps == 0)
            return;

        if (raw_index == 0)
            std::fill(m_firWindow.begin(), m_firWindow.end(), 0.0f);

        m_firWindow[raw_index % taps] = sample;

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
        if (ii >= m_nfft)
            return;

        float *m_in_samples = m_pInput_decim.GetData();
        m_in_samples[ii] = acc * m_window[ii];
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
        const float mix = 1.0f - decay;

        if (m_decimationFactor <= 1)
        {
            kiss_fftr(m_cfg, m_pInput_samples.GetData(), m_out_cpx);
            float totalPower = 0.0f;
            float peakPower = 0.0f;
            float *m_rout = m_pOutput_bins.GetData();
            const int binCount = getBinCount();
            for (int i = 0; i < binCount; i++)
            {
                float power = sqrtf(m_out_cpx[i].r * m_out_cpx[i].r + m_out_cpx[i].i * m_out_cpx[i].i) * m_invFftScaling;
                m_rout[i] = m_rout[i] * decay + power * mix;
                totalPower += power;
                if (power > peakPower)
                    peakPower = power;
            }
            m_lastTotalPower = totalPower;
            m_lastPeakPower = peakPower;
            return;
        }

        // Multi-rate dual FFT execution:
        // A. Decimated low-rate FFT
        kiss_fftr(m_cfg_decim, m_pInput_decim.GetData(), m_out_cpx_decim);
        float *p_decim = m_decim_power.GetData();
        const int half_n = m_nfft / 2;
        for (int i = 0; i <= half_n; i++)
        {
            float p = sqrtf(m_out_cpx_decim[i].r * m_out_cpx_decim[i].r + m_out_cpx_decim[i].i * m_out_cpx_decim[i].i) * m_invFftScaling;
            p_decim[i] = p_decim[i] * decay + p * mix;
        }

        // B. Full-rate high-rate FFT
        kiss_fftr(m_cfg_full, m_pInput_full.GetData(), m_out_cpx_full);
        float *p_full = m_full_power.GetData();
        for (int i = 0; i <= half_n; i++)
        {
            float p = sqrtf(m_out_cpx_full[i].r * m_out_cpx_full[i].r + m_out_cpx_full[i].i * m_out_cpx_full[i].i) * m_invFftScaling;
            p_full[i] = p_full[i] * decay + p * mix;
        }

        // C. Stitch into m_pOutput_bins (size: M = (D * N)/2 + 1)
        const float f_nyq2 = m_sampleRate / (2.0f * (float)m_decimationFactor);
        const float f_a = 0.70f * f_nyq2;
        const float f_b = 0.90f * f_nyq2;
        const float inv_trans = (f_b > f_a) ? (1.0f / (f_b - f_a)) : 0.0f;
        const float full_df = m_sampleRate / (float)m_nfft;

        float *m_rout = m_pOutput_bins.GetData();
        const int totalBins = getBinCount();
        float totalPower = 0.0f;
        float peakPower = 0.0f;

        for (int i = 0; i < totalBins; i++)
        {
            const float freq = static_cast<float>(i) * m_freq_resolution;
            float val = 0.0f;

            if (freq < f_a)
            {
                val = (i <= half_n) ? p_decim[i] : 0.0f;
            }
            else
            {
                const float k_f = full_df > 0.0f ? (freq / full_df) : 0.0f;
                const int k0 = std::max(0, std::min(static_cast<int>(floorf(k_f)), half_n));
                const int k1 = std::min(k0 + 1, half_n);
                const float frac = std::max(0.0f, std::min(k_f - static_cast<float>(k0), 1.0f));
                const float full_val = p_full[k0] * (1.0f - frac) + p_full[k1] * frac;

                if (freq > f_b)
                {
                    val = full_val;
                }
                else
                {
                    const float alpha = 0.5f * (1.0f + cosf((float)M_PI * (freq - f_a) * inv_trans));
                    const float decim_val = (i <= half_n) ? p_decim[i] : full_val;
                    val = alpha * decim_val + (1.0f - alpha) * full_val;
                }
            }

            m_rout[i] = val;
            totalPower += val;
            if (val > peakPower)
                peakPower = val;
        }

        m_lastTotalPower = totalPower;
        m_lastPeakPower = peakPower;
    }

    float getLastPeakPower() const { return m_lastPeakPower; }
    float getLastTotalPower() const { return m_lastTotalPower; }

    float bin2Freq(int bin) const
    {
        return (float)bin * m_freq_resolution;
    }

    float freq2Bin(float freq) const
    {
        return m_freq_resolution > 0.0f ? (freq / m_freq_resolution) : 0.0f;
    }

    BufferIODouble *getBufferIO()
    {
        return &m_pOutput_bins;
    }
};

#endif

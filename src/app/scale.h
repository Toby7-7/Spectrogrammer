#ifndef SCALE_H
#define SCALE_H
#include "math.h"
#include "AppConfig.h"

float lerp( float t, float min, float max);
float unlerp( float min, float max, float x);
float clamp(float v, float min, float max);

inline bool frequency_scale_is_linear(FrequencyAxisScale curve)
{
    return curve == FrequencyAxisScale::Linear;
}

inline float frequency_scale_min_frequency(FrequencyAxisScale curve, float freq)
{
    return frequency_scale_is_linear(curve) ? fmaxf(0.0f, freq) : fmaxf(1.0f, freq);
}

inline float frequency_to_scale_value(FrequencyAxisScale curve, float freq)
{
    freq = frequency_scale_min_frequency(curve, freq);
    switch (curve)
    {
    case FrequencyAxisScale::Linear:
        return freq;
    case FrequencyAxisScale::Music:
        return log2f(freq);
    case FrequencyAxisScale::Mel:
        return 2595.0f * log10f(1.0f + freq / 700.0f);
    case FrequencyAxisScale::Bark:
        return 6.0f * asinhf(freq / 600.0f);
    case FrequencyAxisScale::ERB:
        return 21.4f * log10f(1.0f + 0.00437f * freq);
    case FrequencyAxisScale::Logarithmic:
    default:
        return log10f(freq);
    }
}

inline float scale_value_to_frequency(FrequencyAxisScale curve, float value)
{
    switch (curve)
    {
    case FrequencyAxisScale::Linear:
        return value;
    case FrequencyAxisScale::Music:
        return powf(2.0f, value);
    case FrequencyAxisScale::Mel:
        return 700.0f * (powf(10.0f, value / 2595.0f) - 1.0f);
    case FrequencyAxisScale::Bark:
        return 600.0f * sinhf(value / 6.0f);
    case FrequencyAxisScale::ERB:
        return (powf(10.0f, value / 21.4f) - 1.0f) / 0.00437f;
    case FrequencyAxisScale::Logarithmic:
    default:
        return powf(10.0f, value);
    }
}

class Scale
{
    FrequencyAxisScale m_curve = FrequencyAxisScale::Logarithmic;
    float m_minScale = 0.0f;
    float m_scaleSpan = 1.0f;
    float m_invScaleSpan = 1.0f;

public:
    void init(FrequencyAxisScale curve, float minFreq_, float maxFreq_)
    {
        m_curve = curve;
        const float minFreq = frequency_scale_min_frequency(curve, minFreq_);
        const float maxFreq = fmaxf(minFreq + 1e-3f, frequency_scale_min_frequency(curve, maxFreq_));

        m_minScale = frequency_to_scale_value(curve, minFreq);
        const float maxScale = frequency_to_scale_value(curve, maxFreq);
        m_scaleSpan = fmaxf(1e-6f, maxScale - m_minScale);
        m_invScaleSpan = 1.0f / m_scaleSpan;
    }

    float forward(float x) const
    {
        return scale_value_to_frequency(m_curve, m_minScale + x * m_scaleSpan);
    }

    float backward(float x) const
    {
        const float scaleValue = frequency_to_scale_value(m_curve, x);
        return (scaleValue - m_minScale) * m_invScaleSpan;
    }
};

#endif

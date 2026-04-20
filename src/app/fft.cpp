#include "fft.h"

float hanning(int i, int window_size)
{
    return .50f - .50f * cos((2.0f * M_PI * (float)i) / (float)(window_size - 1));
}

float hamming(int i, int window_size)
{
    return .54f - .46f * cos((2.0f * M_PI * (float)i) / (float)(window_size - 1));
}

float blackman_harris(int i, int window_size)
{
    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;
    const float x = (2.0f * M_PI * (float)i) / (float)(window_size - 1);
    return a0 - a1 * cos(x) + a2 * cos(2.0f * x) - a3 * cos(3.0f * x);
}

float apply_window(WindowFunctionType windowFunction, int i, int window_size)
{
    switch (windowFunction)
    {
    case WindowFunctionType::Rectangular:
        return 1.0f;
    case WindowFunctionType::Hann:
        return hanning(i, window_size);
    case WindowFunctionType::Hamming:
        return hamming(i, window_size);
    case WindowFunctionType::BlackmanHarris:
    default:
        return blackman_harris(i, window_size);
    }
}

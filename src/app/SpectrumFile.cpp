#include <stdio.h>
#include <stdlib.h>
#include "SpectrumFile.h"

namespace
{
constexpr uint32_t kSpectrumMagicVersion = 1;
// Sanity cap: 4M floats = 16 MB.  A legitimate trace is a few thousand bins.
constexpr uint32_t kMaxSpectrumSamples = 1u << 22;

bool read_exact(FILE *f, void *dst, size_t count)
{
    return fread(dst, 1, count, f) == count;
}
}

bool LoadSpectrum(const char *filename, BufferIODouble *pBuffer, float *sample_rate, uint32_t *fft_size)
{
    if (pBuffer == nullptr || sample_rate == nullptr || fft_size == nullptr)
        return false;

    FILE *f = fopen(filename, "rb");
    if (f == NULL)
        return false;

    uint32_t version = 0;
    float rate = 0.0f;
    uint32_t fft = 0;
    uint32_t size = 0;
    bool ok = read_exact(f, &version, sizeof(uint32_t)) && version == kSpectrumMagicVersion &&
              read_exact(f, &rate, sizeof(float)) &&
              read_exact(f, &fft, sizeof(uint32_t)) &&
              read_exact(f, &size, sizeof(uint32_t));
    if (ok)
    {
        // Reject corrupt/truncated headers before allocating anything.
        // A trace stored for FFT size N holds exactly N/2+1 bins.
        ok = fft > 0 && size == fft / 2 + 1 && size <= kMaxSpectrumSamples && rate > 0.0f;
    }
    if (ok)
    {
        pBuffer->Resize(size);
        ok = read_exact(f, pBuffer->GetData(), static_cast<size_t>(size) * sizeof(float));
    }

    if (!ok)
    {
        // Leave the buffer in a known-good (empty) state.
        pBuffer->Resize(0);
    }

    fclose(f);
    if (!ok)
        return false;

    *sample_rate = rate;
    *fft_size = fft;
    return true;
}

bool SaveSpectrum(const char *filename, BufferIODouble *pBuffer, float sample_rate, uint32_t fft_size)
{
    if (pBuffer == nullptr)
        return false;

    FILE *f = fopen(filename, "wb");
    if (f == NULL)
        return false;

    const uint32_t version = kSpectrumMagicVersion;
    const uint32_t size = pBuffer->GetSize();
    bool ok = fwrite(&version, 1, sizeof(uint32_t), f) == sizeof(uint32_t) &&
              fwrite(&sample_rate, 1, sizeof(float), f) == sizeof(float) &&
              fwrite(&fft_size, 1, sizeof(uint32_t), f) == sizeof(uint32_t) &&
              fwrite(&size, 1, sizeof(uint32_t), f) == sizeof(uint32_t);
    if (ok && size > 0)
        ok = fwrite(pBuffer->GetData(), sizeof(float), size, f) == size;
    ok = ok && fflush(f) == 0;
    const int close_result = fclose(f);
    return ok && close_result == 0;
}

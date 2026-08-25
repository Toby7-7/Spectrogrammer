// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0

#ifndef LOGW
#define LOGW(...)
#endif

#include "file_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr uint32_t kFileBufCount = 64;

struct WavFormat
{
    uint32_t sampleRate = 0;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    uint16_t formatTag = 1; // 1 = PCM, 3 = IEEE float
    uint32_t dataOffset = 0;
    uint32_t dataBytes = 0;
};

struct FileSourceState
{
    bool active = false;
    bool atEnd = false;
    bool feederAlive = false;
    std::atomic<bool> stopFeeder{false};
    std::atomic<bool> pauseFlag{false};
    long long startFrame = 0;
    long long producedFrames = 0;
    std::thread feeder;
    FILE *file = nullptr;
    WavFormat fmt;
    AudioQueue *freeQueue = nullptr;
    AudioQueue *recQueue = nullptr;
    sample_buf *bufs = nullptr;
    uint32_t framesPerBuf = 1024;
    std::string error;
    std::string fileName;
};

FileSourceState gSrc;

bool readU16(FILE *f, uint16_t *out)
{
    uint8_t b[2];
    return fread(b, 1, 2, f) == 2 && (*out = (uint16_t)(b[0] | (b[1] << 8)));
}

bool readU32(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
        return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

bool parseWavHeader(FILE *f, WavFormat *out, std::string *error)
{
    uint8_t head[12];
    if (fread(head, 1, 12, f) != 12)
    {
        *error = "not a RIFF file";
        return false;
    }
    if (memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0)
    {
        *error = "missing RIFF/WAVE header";
        return false;
    }

    bool haveFormat = false;
    bool haveData = false;
    while (!haveData)
    {
        uint8_t id[4];
        uint32_t size = 0;
        if (fread(id, 1, 4, f) != 4)
        {
            *error = "truncated chunk list";
            return false;
        }
        if (!readU32(f, &size))
        {
            *error = "truncated chunk size";
            return false;
        }

        if (memcmp(id, "fmt ", 4) == 0)
        {
            if (size < 16)
            {
                *error = "bad fmt chunk";
                return false;
            }
            if (!readU16(f, &out->formatTag) || !readU16(f, &out->channels) || !readU32(f, &out->sampleRate))
            {
                *error = "bad fmt chunk";
                return false;
            }
            uint32_t byteRate = 0;
            uint16_t blockAlign = 0;
            uint16_t bits = 0;
            if (!readU32(f, &byteRate) || !readU16(f, &blockAlign) || !readU16(f, &bits))
            {
                *error = "bad fmt chunk";
                return false;
            }
            out->bitsPerSample = bits;
            haveFormat = true;
            if (size > 16)
                fseek(f, (long)(size - 16), SEEK_CUR);
        }
        else if (memcmp(id, "data", 4) == 0)
        {
            out->dataOffset = (uint32_t)ftell(f);
            out->dataBytes = size;
            haveData = true;
        }
        else
        {
            if (fseek(f, (long)size, SEEK_CUR) != 0)
            {
                *error = "seek failed";
                return false;
            }
        }
        // RIFF chunks are word aligned
        if (size & 1u)
            fseek(f, 1, SEEK_CUR);
    }

    if (!haveFormat)
    {
        *error = "missing fmt chunk";
        return false;
    }
    if (out->channels < 1 || out->channels > 2)
    {
        *error = "only mono/stereo files are supported";
        return false;
    }
    if (out->sampleRate < 3000 || out->sampleRate > 384000)
    {
        *error = "unsupported sample rate";
        return false;
    }
    const bool isPcm = out->formatTag == 1;
    const bool isFloat = out->formatTag == 3;
    if (!isPcm && !isFloat)
    {
        *error = "unsupported WAV format (need PCM or IEEE float)";
        return false;
    }
    const bool validBits = isPcm ? (out->bitsPerSample == 16 || out->bitsPerSample == 24 || out->bitsPerSample == 32)
                                 : (out->bitsPerSample == 32);
    if (!validBits)
    {
        *error = "unsupported bit depth";
        return false;
    }
    return true;
}

int16_t decodeSample(const uint8_t *p, const WavFormat &fmt)
{
    if (fmt.formatTag == 3)
    {
        uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        float value;
        memcpy(&value, &bits, sizeof(float));
        if (value > 1.0f)
            value = 1.0f;
        if (value < -1.0f)
            value = -1.0f;
        return (int16_t)lroundf(value * 32767.0f);
    }
    switch (fmt.bitsPerSample)
    {
    case 16:
        return (int16_t)(int16_t)(p[0] | (p[1] << 8));
    case 24:
    {
        const int32_t v = (int32_t)(p[0] | (p[1] << 8) | ((int8_t)p[2] << 24));
        return (int16_t)(v >> 8);
    }
    case 32:
    {
        const int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
        return (int16_t)(v >> 16);
    }
    default:
        return 0;
    }
}

// Decodes `takeFrames` frames into buf (int16 interleaved), zero-filling the
// remainder of the buffer. Returns the number of real frames decoded.
long long decodeIntoBuffer(sample_buf *buf, long long takeFrames)
{
    const WavFormat &fmt = gSrc.fmt;
    const int channels = fmt.channels;
    const int bytesPerSample = fmt.bitsPerSample / 8;
    const long long capacityFrames = (long long)(buf->cap_ / (2 * channels));
    const long long totalBytes = takeFrames * channels * bytesPerSample;

    int16_t *out = (int16_t *)buf->buf_;
    long long decoded = 0;
    if (gSrc.file != nullptr && totalBytes > 0)
    {
        uint8_t *raw = (uint8_t *)std::malloc((size_t)totalBytes);
        if (raw != nullptr)
        {
            if (fread(raw, 1, (size_t)totalBytes, gSrc.file) == (size_t)totalBytes)
            {
                for (long long i = 0; i < takeFrames; i++)
                    for (int c = 0; c < channels; c++)
                        out[(i * channels + c)] = decodeSample(raw + (i * channels + c) * bytesPerSample, fmt);
                decoded = takeFrames;
            }
            std::free(raw);
        }
    }
    // zero-fill the rest (also used for the final partial buffer)
    const size_t totalSamples = (size_t)capacityFrames * channels;
    for (size_t i = (size_t)decoded * channels; i < totalSamples; i++)
        out[i] = 0;
    buf->size_ = buf->cap_;
    return decoded;
}

void feederLoop()
{
    using namespace std::chrono;
    const WavFormat &fmt = gSrc.fmt;
    const long long totalFrames = (long long)(fmt.dataBytes / (fmt.channels * (fmt.bitsPerSample / 8)));
    const long long remainingAtStart = totalFrames - gSrc.startFrame;

    time_point<steady_clock> next = steady_clock::now();
    const auto step = nanoseconds(static_cast<int64_t>(
        ((unsigned long long)gSrc.framesPerBuf * 1000000000ULL) / (unsigned long long)std::max(1u, fmt.sampleRate)));

    gSrc.producedFrames = 0;
    while (!gSrc.stopFeeder.load())
    {
        if (gSrc.pauseFlag.load())
        {
            std::this_thread::sleep_for(milliseconds(5));
            continue;
        }
        if (gSrc.producedFrames >= remainingAtStart)
        {
            gSrc.atEnd = true;
            // keep the thread alive until deinit so the consumer can drain
            std::this_thread::sleep_for(milliseconds(10));
            continue;
        }

        sample_buf *buf = nullptr;
        if (!gSrc.freeQueue->front(&buf))
        {
            std::this_thread::sleep_for(milliseconds(2));
            continue;
        }
        gSrc.freeQueue->pop();

        const long long take = std::min<long long>(gSrc.framesPerBuf, remainingAtStart - gSrc.producedFrames);
        decodeIntoBuffer(buf, take);
        gSrc.recQueue->push(buf);
        gSrc.producedFrames += take;

        next += step;
        std::this_thread::sleep_until(next);
    }
}

void cleanupLocked()
{
    gSrc.stopFeeder.store(true);
    if (gSrc.feederAlive)
    {
        if (gSrc.feeder.joinable())
            gSrc.feeder.join();
        gSrc.feederAlive = false;
    }
    gSrc.stopFeeder.store(false);
    gSrc.pauseFlag.store(false);
    if (gSrc.file != nullptr)
    {
        std::fclose(gSrc.file);
        gSrc.file = nullptr;
    }
    if (gSrc.recQueue != nullptr)
    {
        delete gSrc.recQueue;
        gSrc.recQueue = nullptr;
    }
    if (gSrc.freeQueue != nullptr)
    {
        delete gSrc.freeQueue;
        gSrc.freeQueue = nullptr;
    }
    if (gSrc.bufs != nullptr)
    {
        uint32_t buf_count = kFileBufCount;
        releaseSampleBufs(gSrc.bufs, buf_count);
        gSrc.bufs = nullptr;
    }
    gSrc.active = false;
    gSrc.atEnd = false;
    gSrc.fmt = WavFormat();
}
} // namespace

bool FileAudio_init(const char *path, int framesPerBuf, long long startFrame)
{
    // idempotent: release any previous file session first
    cleanupLocked();

    gSrc.error.clear();
    if (path == nullptr || path[0] == '\0')
    {
        gSrc.error = "no file path";
        return false;
    }

    FILE *f = std::fopen(path, "rb");
    if (f == nullptr)
    {
        gSrc.error = std::string("cannot open file: ") + path;
        return false;
    }

    WavFormat fmt;
    if (!parseWavHeader(f, &fmt, &gSrc.error))
    {
        std::fclose(f);
        return false;
    }

    const long long totalFrames = (long long)(fmt.dataBytes / (fmt.channels * (fmt.bitsPerSample / 8)));
    if (totalFrames < 64)
    {
        gSrc.error = "file is too short";
        std::fclose(f);
        return false;
    }
    if (startFrame < 0)
        startFrame = 0;
    if (startFrame > totalFrames)
        startFrame = totalFrames;

    const uint32_t bufBytes = (uint32_t)framesPerBuf * fmt.channels * 2;
    gSrc.bufs = allocateSampleBufs(kFileBufCount, bufBytes);
    if (gSrc.bufs == nullptr)
    {
        gSrc.error = "out of memory";
        std::fclose(f);
        return false;
    }
    gSrc.freeQueue = new AudioQueue(kFileBufCount);
    gSrc.recQueue = new AudioQueue(kFileBufCount);
    for (uint32_t i = 0; i < kFileBufCount; i++)
        gSrc.freeQueue->push(&gSrc.bufs[i]);

    gSrc.file = f;
    gSrc.fmt = fmt;
    gSrc.framesPerBuf = (uint32_t)framesPerBuf;
    gSrc.startFrame = startFrame;
    gSrc.atEnd = false;
    gSrc.producedFrames = 0;
    gSrc.active = true;

    std::string name(path);
    const size_t slash = name.find_last_of('/');
    gSrc.fileName = (slash == std::string::npos) ? name : name.substr(slash + 1);
    return true;
}

void FileAudio_getBufferQueues(AudioQueue **pFreeQ, AudioQueue **pRecQ)
{
    *pFreeQ = gSrc.freeQueue;
    *pRecQ = gSrc.recQueue;
}

int FileAudio_getInputChannelCount()
{
    return gSrc.active ? (int)gSrc.fmt.channels : 1;
}

float FileAudio_getSampleRate()
{
    return gSrc.active ? (float)gSrc.fmt.sampleRate : 0.0f;
}

bool FileAudio_startPlay()
{
    if (!gSrc.active || gSrc.feederAlive)
        return gSrc.feederAlive;
    if (gSrc.startFrame >= (long long)(gSrc.fmt.dataBytes / (gSrc.fmt.channels * (gSrc.fmt.bitsPerSample / 8))))
    {
        gSrc.atEnd = true;
        return true;
    }
    gSrc.atEnd = false;
    gSrc.pauseFlag.store(false);
    gSrc.stopFeeder.store(false);
    gSrc.feeder = std::thread(feederLoop);
    gSrc.feederAlive = true;
    return true;
}

void FileAudio_pausePlay()
{
    gSrc.pauseFlag.store(true);
}

bool FileAudio_isPlaying()
{
    return gSrc.feederAlive && !gSrc.pauseFlag.load() && !gSrc.atEnd;
}

bool FileAudio_atEnd()
{
    return gSrc.atEnd;
}

long long FileAudio_positionFrames()
{
    const long long total = (long long)(gSrc.fmt.dataBytes / (gSrc.fmt.channels * (gSrc.fmt.bitsPerSample / 8)));
    const long long pos = gSrc.startFrame + gSrc.producedFrames;
    if (pos < 0)
        return 0;
    if (pos > total)
        return total;
    return pos;
}

double FileAudio_getProgress()
{
    const long long total = (long long)(gSrc.fmt.dataBytes / (gSrc.fmt.channels * (gSrc.fmt.bitsPerSample / 8)));
    if (total <= 0)
        return 1.0;
    const double p = (double)FileAudio_positionFrames() / (double)total;
    if (p < 0.0)
        return 0.0;
    if (p > 1.0)
        return 1.0;
    return p;
}

long long FileAudio_totalFrames()
{
    return (long long)(gSrc.fmt.dataBytes / (gSrc.fmt.channels * (gSrc.fmt.bitsPerSample / 8)));
}

const char *FileAudio_fileName()
{
    return gSrc.fileName.c_str();
}

const char *FileAudio_errorText()
{
    return gSrc.error.c_str();
}

bool FileAudio_active()
{
    return gSrc.active;
}

void FileAudio_deinit()
{
    if (!gSrc.active && !gSrc.feederAlive && gSrc.file == nullptr)
        return;
    cleanupLocked();
    gSrc.error.clear();
    gSrc.fileName.clear();
}

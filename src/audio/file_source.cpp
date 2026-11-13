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

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ANDROID
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#endif

namespace
{
constexpr uint32_t kFileBufCount = 64;

enum class FileDecoderType
{
    None,
    Wav,
    MediaCodec
};

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
    
    FileDecoderType decoderType = FileDecoderType::None;

    // WAV direct decoder
    FILE *file = nullptr;
    WavFormat fmt;

#ifdef ANDROID
    // NDK MediaCodec universal decoder
    int mediaFd = -1;
    AMediaExtractor *extractor = nullptr;
    AMediaCodec *codec = nullptr;
    bool mediaEof = false;
    std::vector<int16_t> pcmFifo;
    int32_t mediaSampleRate = 44100;
    int32_t mediaChannels = 2;
    int64_t mediaDurationUs = 0;
#endif

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
    if (gSrc.decoderType == FileDecoderType::Wav)
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
#ifdef ANDROID
    else if (gSrc.decoderType == FileDecoderType::MediaCodec && gSrc.codec != nullptr && gSrc.extractor != nullptr)
    {
        const int channels = gSrc.mediaChannels;
        const long long capacityFrames = (long long)(buf->cap_ / (2 * channels));
        int16_t *out = (int16_t *)buf->buf_;
        long long decoded = 0;

        const size_t wantedSamples = (size_t)(takeFrames * channels);
        while (gSrc.pcmFifo.size() < wantedSamples && !gSrc.mediaEof && !gSrc.stopFeeder.load())
        {
            // 1. Feed input to MediaCodec
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(gSrc.codec, 1000);
            if (inIdx >= 0)
            {
                size_t inSize = 0;
                uint8_t *inBuf = AMediaCodec_getInputBuffer(gSrc.codec, (size_t)inIdx, &inSize);
                if (inBuf != nullptr)
                {
                    ssize_t sampleSize = AMediaExtractor_readSampleData(gSrc.extractor, inBuf, inSize);
                    if (sampleSize < 0)
                    {
                        AMediaCodec_queueInputBuffer(gSrc.codec, (size_t)inIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    }
                    else
                    {
                        int64_t pts = AMediaExtractor_getSampleTime(gSrc.extractor);
                        AMediaCodec_queueInputBuffer(gSrc.codec, (size_t)inIdx, 0, (size_t)sampleSize, (uint64_t)pts, 0);
                        AMediaExtractor_advance(gSrc.extractor);
                    }
                }
            }

            // 2. Retrieve decoded output from MediaCodec
            AMediaCodecBufferInfo info;
            ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(gSrc.codec, &info, 1000);
            if (outIdx >= 0)
            {
                if (info.size > 0)
                {
                    size_t outSize = 0;
                    uint8_t *outBuf = AMediaCodec_getOutputBuffer(gSrc.codec, (size_t)outIdx, &outSize);
                    if (outBuf != nullptr)
                    {
                        const int16_t *pcm = (const int16_t *)(outBuf + info.offset);
                        size_t sampleCount = (size_t)info.size / sizeof(int16_t);
                        gSrc.pcmFifo.insert(gSrc.pcmFifo.end(), pcm, pcm + sampleCount);
                    }
                }
                AMediaCodec_releaseOutputBuffer(gSrc.codec, (size_t)outIdx, false);
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)
                {
                    gSrc.mediaEof = true;
                }
            }
            else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
            {
                AMediaFormat *newFmt = AMediaCodec_getOutputFormat(gSrc.codec);
                if (newFmt != nullptr)
                {
                    AMediaFormat_getInt32(newFmt, "channel-count", &gSrc.mediaChannels);
                    AMediaFormat_getInt32(newFmt, "sample-rate", &gSrc.mediaSampleRate);
                    AMediaFormat_delete(newFmt);
                }
            }
            else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER)
            {
                std::this_thread::yield();
            }
        }

        size_t availableSamples = std::min(gSrc.pcmFifo.size(), wantedSamples);
        if (availableSamples > 0)
        {
            std::memcpy(out, gSrc.pcmFifo.data(), availableSamples * sizeof(int16_t));
            gSrc.pcmFifo.erase(gSrc.pcmFifo.begin(), gSrc.pcmFifo.begin() + availableSamples);
            decoded = (long long)(availableSamples / channels);
        }

        // zero-fill remainder
        const size_t totalSamples = (size_t)capacityFrames * channels;
        for (size_t i = (size_t)decoded * channels; i < totalSamples; i++)
            out[i] = 0;
        buf->size_ = buf->cap_;
        return decoded;
    }
#endif

    buf->size_ = buf->cap_;
    std::memset(buf->buf_, 0, buf->cap_);
    return 0;
}

void feederLoop()
{
    using namespace std::chrono;
    const uint32_t sampleRate = (uint32_t)FileAudio_getSampleRate();
    const long long totalFrames = FileAudio_totalFrames();
    const long long remainingAtStart = totalFrames - gSrc.startFrame;

    time_point<steady_clock> next = steady_clock::now();
    const auto step = nanoseconds(static_cast<int64_t>(
        ((unsigned long long)gSrc.framesPerBuf * 1000000000ULL) / (unsigned long long)std::max(1u, sampleRate)));

    gSrc.producedFrames = 0;
    while (!gSrc.stopFeeder.load())
    {
        if (gSrc.pauseFlag.load())
        {
            std::this_thread::sleep_for(milliseconds(5));
            continue;
        }
        if (gSrc.producedFrames >= remainingAtStart || gSrc.atEnd)
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
        long long decoded = decodeIntoBuffer(buf, take);
        gSrc.recQueue->push(buf);
        gSrc.producedFrames += take;

        if (decoded < take && decoded == 0)
        {
            gSrc.atEnd = true;
        }

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
#ifdef ANDROID
    if (gSrc.codec != nullptr)
    {
        AMediaCodec_stop(gSrc.codec);
        AMediaCodec_delete(gSrc.codec);
        gSrc.codec = nullptr;
    }
    if (gSrc.extractor != nullptr)
    {
        AMediaExtractor_delete(gSrc.extractor);
        gSrc.extractor = nullptr;
    }
    if (gSrc.mediaFd >= 0)
    {
        close(gSrc.mediaFd);
        gSrc.mediaFd = -1;
    }
    gSrc.pcmFifo.clear();
    gSrc.mediaEof = false;
#endif
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
    gSrc.decoderType = FileDecoderType::None;
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

    std::string name(path);
    const size_t slash = name.find_last_of('/');
    gSrc.fileName = (slash == std::string::npos) ? name : name.substr(slash + 1);

    // 1. Try standard WAV parser first
    FILE *f = std::fopen(path, "rb");
    if (f != nullptr)
    {
        WavFormat fmt;
        if (parseWavHeader(f, &fmt, &gSrc.error))
        {
            const long long totalFrames = (long long)(fmt.dataBytes / (fmt.channels * (fmt.bitsPerSample / 8)));
            if (totalFrames >= 64)
            {
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

                // Seek to start position
                const long long startByteOffset = fmt.dataOffset + startFrame * fmt.channels * (fmt.bitsPerSample / 8);
                fseek(f, (long)startByteOffset, SEEK_SET);

                gSrc.file = f;
                gSrc.fmt = fmt;
                gSrc.framesPerBuf = (uint32_t)framesPerBuf;
                gSrc.startFrame = startFrame;
                gSrc.atEnd = false;
                gSrc.producedFrames = 0;
                gSrc.decoderType = FileDecoderType::Wav;
                gSrc.active = true;
                return true;
            }
        }
        std::fclose(f);
        f = nullptr;
    }

#ifdef ANDROID
    // 2. Fall back to Android NDK MediaExtractor + MediaCodec (MP3, AAC, FLAC, OGG, M4A, etc.)
    int fd = open(path, O_RDONLY);
    if (fd >= 0)
    {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size > 0)
        {
            AMediaExtractor *extractor = AMediaExtractor_new();
            if (extractor != nullptr)
            {
                media_status_t err = AMediaExtractor_setDataSourceFd(extractor, fd, 0, st.st_size);
                if (err == AMEDIA_OK)
                {
                    size_t numTracks = AMediaExtractor_getTrackCount(extractor);
                    int audioTrack = -1;
                    AMediaFormat *trackFmt = nullptr;
                    const char *mime = nullptr;

                    for (size_t i = 0; i < numTracks; i++)
                    {
                        trackFmt = AMediaExtractor_getTrackFormat(extractor, i);
                        if (trackFmt != nullptr)
                        {
                            if (AMediaFormat_getString(trackFmt, "mime", &mime) && mime != nullptr)
                            {
                                if (strncmp(mime, "audio/", 6) == 0)
                                {
                                    audioTrack = (int)i;
                                    break;
                                }
                            }
                            AMediaFormat_delete(trackFmt);
                            trackFmt = nullptr;
                        }
                    }

                    if (audioTrack >= 0 && trackFmt != nullptr)
                    {
                        AMediaExtractor_selectTrack(extractor, (size_t)audioTrack);

                        int32_t sampleRate = 44100;
                        int32_t channels = 2;
                        int64_t durationUs = 0;
                        AMediaFormat_getInt32(trackFmt, "sample-rate", &sampleRate);
                        AMediaFormat_getInt32(trackFmt, "channel-count", &channels);
                        AMediaFormat_getInt64(trackFmt, "durationUs", &durationUs);

                        if (channels > 2)
                            channels = 2;
                        if (sampleRate <= 0)
                            sampleRate = 44100;

                        AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
                        if (codec != nullptr)
                        {
                            media_status_t cfgErr = AMediaCodec_configure(codec, trackFmt, nullptr, nullptr, 0);
                            media_status_t startErr = AMediaCodec_start(codec);
                            AMediaFormat_delete(trackFmt);

                            if (cfgErr == AMEDIA_OK && startErr == AMEDIA_OK)
                            {
                                const long long totalFrames = (long long)((double)durationUs / 1000000.0 * (double)sampleRate);
                                if (startFrame < 0)
                                    startFrame = 0;
                                if (startFrame > totalFrames && totalFrames > 0)
                                    startFrame = totalFrames;

                                if (startFrame > 0 && sampleRate > 0)
                                {
                                    int64_t targetUs = (int64_t)((double)startFrame / (double)sampleRate * 1000000.0);
                                    AMediaExtractor_seekTo(extractor, targetUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                                }

                                const uint32_t bufBytes = (uint32_t)framesPerBuf * (uint32_t)channels * 2;
                                gSrc.bufs = allocateSampleBufs(kFileBufCount, bufBytes);
                                if (gSrc.bufs == nullptr)
                                {
                                    AMediaCodec_stop(codec);
                                    AMediaCodec_delete(codec);
                                    AMediaExtractor_delete(extractor);
                                    close(fd);
                                    gSrc.error = "out of memory";
                                    return false;
                                }

                                gSrc.freeQueue = new AudioQueue(kFileBufCount);
                                gSrc.recQueue = new AudioQueue(kFileBufCount);
                                for (uint32_t i = 0; i < kFileBufCount; i++)
                                    gSrc.freeQueue->push(&gSrc.bufs[i]);

                                gSrc.extractor = extractor;
                                gSrc.codec = codec;
                                gSrc.mediaFd = fd;
                                gSrc.mediaSampleRate = sampleRate;
                                gSrc.mediaChannels = channels;
                                gSrc.mediaDurationUs = durationUs;
                                gSrc.mediaEof = false;
                                gSrc.pcmFifo.clear();

                                gSrc.framesPerBuf = (uint32_t)framesPerBuf;
                                gSrc.startFrame = startFrame;
                                gSrc.atEnd = false;
                                gSrc.producedFrames = 0;
                                gSrc.decoderType = FileDecoderType::MediaCodec;
                                gSrc.active = true;
                                return true;
                            }
                            else
                            {
                                AMediaCodec_delete(codec);
                            }
                        }
                        else
                        {
                            AMediaFormat_delete(trackFmt);
                        }
                    }
                }
                AMediaExtractor_delete(extractor);
            }
        }
        close(fd);
    }
#endif

    gSrc.error = "unsupported audio format or damaged file";
    return false;
}

void FileAudio_getBufferQueues(AudioQueue **pFreeQ, AudioQueue **pRecQ)
{
    *pFreeQ = gSrc.freeQueue;
    *pRecQ = gSrc.recQueue;
}

int FileAudio_getInputChannelCount()
{
    if (!gSrc.active)
        return 1;
    if (gSrc.decoderType == FileDecoderType::Wav)
        return (int)gSrc.fmt.channels;
#ifdef ANDROID
    if (gSrc.decoderType == FileDecoderType::MediaCodec)
        return (int)gSrc.mediaChannels;
#endif
    return 1;
}

float FileAudio_getSampleRate()
{
    if (!gSrc.active)
        return 0.0f;
    if (gSrc.decoderType == FileDecoderType::Wav)
        return (float)gSrc.fmt.sampleRate;
#ifdef ANDROID
    if (gSrc.decoderType == FileDecoderType::MediaCodec)
        return (float)gSrc.mediaSampleRate;
#endif
    return 0.0f;
}

bool FileAudio_startPlay()
{
    if (!gSrc.active || gSrc.feederAlive)
        return gSrc.feederAlive;
    if (gSrc.startFrame >= FileAudio_totalFrames() && FileAudio_totalFrames() > 0)
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
    const long long total = FileAudio_totalFrames();
    const long long pos = gSrc.startFrame + gSrc.producedFrames;
    if (pos < 0)
        return 0;
    if (pos > total && total > 0)
        return total;
    return pos;
}

double FileAudio_getProgress()
{
    const long long total = FileAudio_totalFrames();
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
    if (gSrc.decoderType == FileDecoderType::Wav)
    {
        const int bytesPerSample = gSrc.fmt.bitsPerSample / 8;
        if (gSrc.fmt.channels > 0 && bytesPerSample > 0)
            return (long long)(gSrc.fmt.dataBytes / (gSrc.fmt.channels * bytesPerSample));
        return 0;
    }
#ifdef ANDROID
    if (gSrc.decoderType == FileDecoderType::MediaCodec)
    {
        return (long long)((double)gSrc.mediaDurationUs / 1000000.0 * (double)gSrc.mediaSampleRate);
    }
#endif
    return 0;
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

// Host-side unit tests for Spectrogrammer (Linux / macOS dev machines).
// Build & run with:  make test
//
// Covers: AppConfig save/load + v11->v12 migration + validation, color map
// selection/clamping, PNG writer (structure, CRCs, pixel round-trip), the
// FIR anti-alias decimator + level accessors in fft.h, the WAV file feeder
// (FileAudio_*) end to end, and the WAV file picker helper.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#ifndef LOGW
#define LOGW(...)
#endif

#include <zlib.h>

#include "AppConfig.h"
#include "colormaps.h"
#include "FilePicker.h"
#include "image_export.h"
#include "file_source.h"
#include "fft.h"
#include "auformat.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(bool cond, const char *msg)
{
    g_checks++;
    if (cond)
        printf("  PASS  %s\n", msg);
    else
    {
        printf("  FAIL  %s\n", msg);
        g_failures++;
    }
}

static void section(const char *name)
{
    printf("== %s ==\n", name);
}

static std::string tmp_path(const char *name)
{
    char tmpl[] = "/tmp/spectro_test_XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd < 0)
        return "";
    close(fd);
    unlink(tmpl);
    return std::string(tmpl) + "_" + name;
}

// ---------------------------------------------------------------- AppConfig

static void test_appconfig_roundtrip()
{
    section("AppConfig round-trip");
    const std::string path = tmp_path("rt.ini");

    AppConfig cfg = MakeDefaultAppConfig();
    cfg.fft_size = 4096;
    cfg.sample_rate_hz = 96000;
    cfg.window_function = WindowFunctionType::BlackmanHarris;
    cfg.input_channel_mode = InputChannelMode::StereoIndependent;
    cfg.color_map = 1;
    cfg.waterfall_freeze = true;
    cfg.average_frames = 32;
    cfg.alarm_enabled = true;
    cfg.alarm_threshold_db = -55.0f;
    cfg.alarm_freq_min_hz = 100.0f;
    cfg.alarm_freq_max_hz = 8000.0f;
    cfg.alarm_vibrate = true;
    cfg.baseline_diff_enabled = true;
    cfg.desired_transform_interval_ms = 33.0f;

    check(SaveAppConfig(path.c_str(), cfg), "save succeeds");

    AppConfig loaded = MakeDefaultAppConfig();
    check(LoadAppConfig(path.c_str(), &loaded), "load succeeds");
    check(loaded.fft_size == 4096, "fft_size preserved");
    check(loaded.sample_rate_hz == 96000, "sample_rate_hz preserved");
    check(loaded.window_function == WindowFunctionType::BlackmanHarris, "window preserved");
    check(loaded.input_channel_mode == InputChannelMode::StereoIndependent, "channel mode preserved");
    check(loaded.color_map == 1, "color_map preserved");
    check(loaded.waterfall_freeze, "waterfall_freeze preserved");
    check(loaded.average_frames == 32, "average_frames preserved");
    check(loaded.alarm_enabled, "alarm_enabled preserved");
    check(fabsf(loaded.alarm_threshold_db + 55.0f) < 0.01f, "alarm threshold preserved");
    check(loaded.alarm_vibrate, "alarm_vibrate preserved");
    check(loaded.baseline_diff_enabled, "baseline_diff preserved");
    unlink(path.c_str());
}

static void test_appconfig_migration_v11()
{
    section("AppConfig v11 -> v12 migration");
    const std::string path = tmp_path("v11.ini");

    FILE *f = fopen(path.c_str(), "w");
    if (f == nullptr)
        return;
    fprintf(f, "app_config_version=11\n");
    fprintf(f, "fft_size=2048\n");
    fprintf(f, "sample_rate_hz=44100\n");
    fprintf(f, "window_function=1\n");
    fprintf(f, "max_hold_trace_enabled=1\n");
    fprintf(f, "show_waterfall=1\n");
    fclose(f);

    AppConfig cfg = MakeDefaultAppConfig();
    check(LoadAppConfig(path.c_str(), &cfg), "legacy file loads");
    check(cfg.fft_size == 2048, "old fft_size kept");
    check(cfg.sample_rate_hz == 44100, "old sample rate kept");
    check(cfg.max_hold_trace_enabled, "old max_hold kept");
    check(cfg.color_map == 0, "new color_map defaults");
    check(cfg.average_frames == 0, "new average_frames defaults");
    check(!cfg.alarm_enabled, "new alarm defaults off");
    check(!cfg.waterfall_freeze, "new freeze defaults off");
    check(!cfg.baseline_diff_enabled, "new baseline_diff defaults off");
    unlink(path.c_str());
}

static void test_appconfig_validation()
{
    section("AppConfig validation / clamping");
    const std::string path = tmp_path("bad.ini");

    FILE *f = fopen(path.c_str(), "w");
    if (f == nullptr)
        return;
    fprintf(f, "app_config_version=12\n");
    fprintf(f, "fft_size=99999\n");
    fprintf(f, "window_function=42\n");
    fprintf(f, "color_map=9\n");
    fprintf(f, "average_frames=7\n");
    fprintf(f, "alarm_threshold_db=10\n");
    fprintf(f, "sample_rate_hz=1000000\n");
    fclose(f);

    AppConfig cfg = MakeDefaultAppConfig();
    check(LoadAppConfig(path.c_str(), &cfg), "garbage file loads (clamped)");
    check(cfg.fft_size <= 8192, "fft_size capped at 8192");
    check(cfg.window_function >= WindowFunctionType::Rectangular && cfg.window_function <= WindowFunctionType::BlackmanHarris,
          "window clamped to enum");
    check(cfg.color_map == 0, "bad color_map -> 0");
    check(cfg.average_frames == 16, "sub-16 average_frames rounded up to 16");
    check(cfg.alarm_threshold_db < 0.0f, "positive alarm threshold clamped");
    check(cfg.sample_rate_hz <= 384000, "sample rate capped");
    unlink(path.c_str());
}

// ---------------------------------------------------------------- Colormaps

static void test_colormaps()
{
    section("colormaps");
    check(GetColorMapCount() == 3, "three color maps available");

    SetColorMap(1);
    check(GetColorMap(0) != 0, "hot/cold map returns data");
    SetColorMap(2);
    const uint16_t first = GetColorMap(0);
    bool gray_monotonic = true;
    for (int i = 1; i < 256; i++)
    {
        const uint16_t v = GetColorMap(i);
        const int r = (v >> 11) & 0x1f;
        const int g = (v >> 5) & 0x3f;
        const int b = v & 0x1f;
        if (r != (g >> 1) || (g >> 1) != b)
            gray_monotonic = false;
    }
    (void)first;
    check(gray_monotonic, "grayscale map has r==g==b");

    SetColorMap(2);
    check(GetColorMap(255) == 0xFFFF, "grayscale top entry is white");
    SetColorMap(7);
    check(GetColorMap(0) == 0 && GetColorMap(255) != 0, "out-of-range clamps to a valid map");
    SetColorMap(-3);
    check(GetColorMap(0) == 0 && GetColorMap(255) != 0, "negative clamps to a valid map");
    SetColorMap(0);
}

// ------------------------------------------------------------------- PNG

static bool parse_png(const char *path, int *pw, int *ph, uint8_t **pixels)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr)
        return false;
    std::vector<uint8_t> data;
    uint8_t chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        data.insert(data.end(), chunk, chunk + n);
    fclose(f);

    if (data.size() < 8 || memcmp(data.data(), "\x89PNG\r\n\x1a\n", 8) != 0)
        return false;

    size_t pos = 8;
    std::vector<uint8_t> idat;
    bool ok = false;
    while (pos + 12 <= data.size())
    {
        const uint32_t len = (uint32_t(data[pos]) << 24) | (uint32_t(data[pos + 1]) << 16) |
                             (uint32_t(data[pos + 2]) << 8) | uint32_t(data[pos + 3]);
        const uint8_t *fourcc = &data[pos + 4];
        const uint8_t *payload = &data[pos + 8];
        const uint32_t crc = (uint32_t(payload[len]) << 24) | (uint32_t(payload[len + 1]) << 16) |
                             (uint32_t(payload[len + 2]) << 8) | uint32_t(payload[len + 3]);
        if (crc32(crc32(0, fourcc, 4), payload, len) != crc)
            return false;
        if (memcmp(fourcc, "IHDR", 4) == 0)
        {
            *pw = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
            *ph = (payload[4] << 24) | (payload[5] << 16) | (payload[6] << 8) | payload[7];
            if (payload[8] != 8 || payload[9] != 6)
                return false; // expect 8-bit RGBA
        }
        if (memcmp(fourcc, "IDAT", 4) == 0)
            idat.insert(idat.end(), payload, payload + len);
        pos += 12 + len;
    }
    if (idat.empty())
        return false;

    uLongf decomp_len = (uLongf)((uLong)*pw * *ph * 5 + 64);
    std::vector<uint8_t> raw(decomp_len);
    if (uncompress(raw.data(), &decomp_len, idat.data(), (uLong)idat.size()) != Z_OK)
        return false;
    if (decomp_len != (uLong)*pw * *ph * 4 + (uLong)*ph)
        return false;

    std::vector<uint8_t> px((size_t)*pw * *ph * 4);
    for (int y = 0; y < *ph; y++)
        memcpy(&px[(size_t)y * *pw * 4], &raw[(size_t)y * (*pw * 4 + 1) + 1], (size_t)*pw * 4);
    *pixels = px.data();
    // keep the vector alive via a small static (test-only)
    static std::vector<uint8_t> g_keep;
    g_keep = std::move(px);
    ok = true;
    return ok;
}

static void test_png()
{
    section("PNG writer");
    const int w = 6, h = 4;
    const std::string rgba_path = tmp_path("t.png");
    const std::string c565_path = tmp_path("t565.png");

    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            rgba[(y * w + x) * 4 + 0] = (uint8_t)(x * 40);
            rgba[(y * w + x) * 4 + 1] = (uint8_t)(y * 60);
            rgba[(y * w + x) * 4 + 2] = (uint8_t)((x + y) * 20);
            rgba[(y * w + x) * 4 + 3] = 255;
        }
    check(WritePngRgba(rgba_path.c_str(), w, h, rgba.data()), "WritePngRgba succeeds");

    int pw = 0, ph = 0;
    uint8_t *px = nullptr;
    check(parse_png(rgba_path.c_str(), &pw, &ph, &px), "PNG parses (sig/IHDR/CRCs/IDAT)");
    bool pixels_ok = (pw == w && ph == h && px != nullptr);
    for (int y = 0; pixels_ok && y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const uint8_t *p = &px[(y * w + x) * 4];
            if (p[0] != (uint8_t)(x * 40) || p[1] != (uint8_t)(y * 60) ||
                p[2] != (uint8_t)((x + y) * 20) || p[3] != 255)
            {
                pixels_ok = false;
                break;
            }
        }
    check(pixels_ok, "RGBA pixels round-trip exactly");

    std::vector<uint16_t> rgb565((size_t)w * h);
    for (size_t i = 0; i < rgb565.size(); i++)
    {
        const uint8_t r = 200, g = 120, b = 60;
        rgb565[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    check(WritePngFromRgb565(c565_path.c_str(), w, h, rgb565.data()), "WritePngFromRgb565 succeeds");
    pixels_ok = parse_png(c565_path.c_str(), &pw, &ph, &px);
    for (int y = 0; pixels_ok && y < h; y++)
        for (int x = 0; x < w; x++)
        {
            const uint8_t *p = &px[(y * w + x) * 4];
            // 5-6-5 expansion
            const int r5 = 200 >> 3, g6 = 120 >> 2, b5 = 60 >> 3;
            if (p[0] != ((r5 << 3) | (r5 >> 2)) || p[1] != ((g6 << 2) | (g6 >> 4)) ||
                p[2] != ((b5 << 3) | (b5 >> 2)))
            {
                pixels_ok = false;
                break;
            }
        }
    check(pixels_ok, "RGB565 pixels expand correctly");
    unlink(rgba_path.c_str());
    unlink(c565_path.c_str());
}

// ------------------------------------------------------------------- FFT

static void test_fft()
{
    section("fft.h: decimation FIR + level accessors");

    // D=1 path: 1 kHz tone at 48 kHz, full scale.
    {
        myFFT f;
        f.init(1024, 48000.0f, 1, WindowFunctionType::Hann);
        const int frames = 8 * f.getProcessedLength();
        for (int i = 0; i < frames; i++)
        {
            const float s = 32767.0f * sinf(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
            f.writeInputSample(s, i);
        }
        f.computePower(0.0f);
        const float peak = f.getLastPeakPower();
        check(peak > 32767.0f * 0.4f && peak < 32767.0f * 1.05f, "D=1 full-scale tone peak in expected range");

        const float *bins = f.getBufferIO()->GetData();
        int max_bin = 0;
        for (int i = 1; i < f.getBinCount(); i++)
            if (bins[i] > bins[max_bin])
                max_bin = i;
        const float expected_bin = f.freq2Bin(1000.0f);
        check(fabsf(max_bin - expected_bin) <= 2.0f, "tone lands on the expected bin");
    }

    // D=2 path: same tone, plus an out-of-band 20 kHz tone that the FIR
    // anti-alias filter must attenuate (it would alias to 8 kHz otherwise).
    {
        myFFT f;
        f.init(1024, 48000.0f, 2, WindowFunctionType::Hann);
        const int frames = 8 * f.getProcessedLength();
        for (int i = 0; i < frames; i++)
        {
            const float t = (float)i / 48000.0f;
            const float s = 32767.0f * sinf(2.0f * 3.14159265f * 1000.0f * t) +
                            16000.0f * sinf(2.0f * 3.14159265f * 20000.0f * t);
            f.writeInputSample(s, i);
        }
        f.computePower(0.0f);

        const float *bins = f.getBufferIO()->GetData();
        int alias_bin = (int)std::round(f.freq2Bin(8000.0f));
        int tone_bin = (int)std::round(f.freq2Bin(1000.0f));
        check(bins[alias_bin] < 16000.0f * 0.15f, "FIR suppresses the out-of-band (20 kHz) alias image");
        check(bins[tone_bin] > 32767.0f * 0.4f, "in-band tone survives decimation at expected level");
    }
}

// ------------------------------------------------------------- file source

static bool write_test_wav(const char *path, uint32_t rate, int channels, int frames, bool float32)
{
    FILE *f = fopen(path, "wb");
    if (f == nullptr)
        return false;

    const uint32_t bytesPerSample = float32 ? 4 : 2;
    const uint32_t dataBytes = frames * channels * bytesPerSample;
    const uint16_t fmt = float32 ? 3 : 1;

    auto w16 = [&](uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); };
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8 * i)) & 0xFF, f); };

    fwrite("RIFF", 1, 4, f);
    w32(36 + dataBytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    w32(16);
    w16(fmt);
    w16((uint16_t)channels);
    w32(rate);
    w32(rate * channels * bytesPerSample);
    w16(channels * bytesPerSample);
    w16((uint16_t)(bytesPerSample * 8));
    fwrite("data", 1, 4, f);
    w32(dataBytes);

    for (int i = 0; i < frames; i++)
    {
        for (int c = 0; c < channels; c++)
        {
            // deterministic ramp, different per channel
            const float v = (float)((i * 7 + c * 13) % 1000) / 1000.0f;
            if (float32)
            {
                uint32_t bits;
                memcpy(&bits, &v, 4);
                w32(bits);
            }
            else
            {
                const int16_t s = (int16_t)(v * 30000.0f);
                w16((uint16_t)s);
            }
        }
    }
    fclose(f);
    return true;
}

static void pump_rec_queue(AudioQueue *rec, AudioQueue *fre, int *out_frames)
{
    *out_frames = 0;
    sample_buf *buf = nullptr;
    while (rec->front(&buf))
    {
        rec->pop();
        *out_frames += AU_LEN(buf->cap_); // mono 16-bit in this test: samples == frames
        fre->push(buf);
    }
}

static void test_file_source()
{
    section("FileAudio WAV feeder");
    const std::string wav = tmp_path("in.wav");
    const uint32_t rate = 44100;
    const int frames = 4410; // 100 ms mono 16-bit
    check(write_test_wav(wav.c_str(), rate, 1, frames, false), "test WAV written");

    check(!FileAudio_init("/nonexistent/nope.wav", 1024), "missing file rejected");
    check(strlen(FileAudio_errorText()) > 0, "error text set");

    check(FileAudio_init(wav.c_str(), 1024, 0), "init succeeds");
    check(FileAudio_active(), "source active");
    check(FileAudio_getSampleRate() == (float)rate, "sample rate parsed");
    check(FileAudio_getInputChannelCount() == 1, "channel count parsed");
    check(FileAudio_totalFrames() == frames, "total frames parsed");
    const std::string expected_name = wav.substr(wav.find_last_of('/') + 1);
    check(strcmp(FileAudio_fileName(), expected_name.c_str()) == 0, "file name reported");

    AudioQueue *fre = nullptr;
    AudioQueue *rec = nullptr;
    FileAudio_getBufferQueues(&fre, &rec);
    check(fre != nullptr && rec != nullptr, "queues exposed");

    check(FileAudio_startPlay(), "startPlay succeeds");
    check(FileAudio_isPlaying(), "isPlaying true");

    int pumped = 0;
    for (int spin = 0; spin < 60 && !FileAudio_atEnd(); spin++)
    {
        usleep(20000); // 20 ms
        int got = 0;
        pump_rec_queue(rec, fre, &got);
        pumped += got;
    }
    check(FileAudio_atEnd(), "atEnd reached after full playback");
    {
        // atEnd means "everything pushed"; drain whatever is still queued.
        int got = 0;
        pump_rec_queue(rec, fre, &got);
        pumped += got;
    }
    check(pumped >= frames, "all frames were pumped through the queue");
    check(fabsf(FileAudio_getProgress() - 1.0) < 0.02, "progress reaches 1.0");

    FileAudio_deinit();
    check(!FileAudio_active(), "deinit releases state");

    // 32-bit float WAV
    const std::string wav32 = tmp_path("in32.wav");
    check(write_test_wav(wav32.c_str(), rate, 2, 2048, true), "float32 stereo WAV written");
    check(FileAudio_init(wav32.c_str(), 1024, 0), "float32 init succeeds");
    check(FileAudio_getInputChannelCount() == 2, "stereo parsed");
    check(FileAudio_totalFrames() == 2048, "stereo frame count parsed");
    FileAudio_deinit();

    unlink(wav.c_str());
    unlink(wav32.c_str());
}

// --------------------------------------------------------------- file picker

static void test_file_picker()
{
    section("FilePicker WAV filter");
    const std::string dir = "/tmp/spectro_picker_test";
    system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());

    const char *names[] = {"b_test.wav", "a_upper.WAV", "notes.txt", "c.MP3"};
    for (const char *name : names)
    {
        FILE *f = fopen((dir + "/" + name).c_str(), "w");
        if (f)
            fclose(f);
    }

    const char *suffixes[] = {".wav"};
    linked_list *list = GetFilesInFolderEx(dir.c_str(), suffixes, 1);
    int wav_count = 0;
    bool only_wavs = true;
    for (linked_list *node = list; node != nullptr; node = node->pNext)
    {
        wav_count++;
        if (strcasecmp(node->pStr, "b_test.wav") != 0 && strcasecmp(node->pStr, "a_upper.WAV") != 0)
            only_wavs = false;
    }
    check(wav_count == 2, "exactly the two .wav files found (case-insensitive)");
    check(only_wavs, "no non-WAV entries");

    list = SortLinkedList(list);
    bool sorted_ok = false;
    if (list && list->pNext && list->pNext->pNext == nullptr)
        sorted_ok = strcmp(list->pStr, "a_upper.WAV") == 0 && strcmp(list->pNext->pStr, "b_test.wav") == 0;
    check(sorted_ok, "sorted list order");
    FreeLinkedList(list);
    system(("rm -rf " + dir).c_str());
}

// ------------------------------------------------------------------ main

int main()
{
    test_appconfig_roundtrip();
    test_appconfig_migration_v11();
    test_appconfig_validation();
    test_colormaps();
    test_png();
    test_fft();
    test_file_source();
    test_file_picker();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

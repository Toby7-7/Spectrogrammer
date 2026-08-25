// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0

#include "image_export.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <zlib.h>

namespace
{
constexpr uint32_t kPngSignature[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

// CRC-32 as used by PNG (zlib implements the same polynomial).
uint32_t Crc32Png(uint32_t crc, const uint8_t *buf, size_t len)
{
    return (uint32_t)crc32(crc, buf, (uInt)len);
}

void AppendU32BE(std::vector<uint8_t> *out, uint32_t value)
{
    out->push_back((uint8_t)((value >> 24) & 0xff));
    out->push_back((uint8_t)((value >> 16) & 0xff));
    out->push_back((uint8_t)((value >> 8) & 0xff));
    out->push_back((uint8_t)(value & 0xff));
}

// Appends a PNG chunk: 4CC + BE length + payload + CRC32(4CC + payload).
void AppendChunk(std::vector<uint8_t> *out, const char fourcc[4], const uint8_t *payload, size_t payload_len)
{
    AppendU32BE(out, (uint32_t)payload_len);
    const size_t crc_start = out->size();
    for (int i = 0; i < 4; i++)
        out->push_back((uint8_t)fourcc[i]);
    if (payload != nullptr && payload_len > 0)
        out->insert(out->end(), payload, payload + payload_len);

    uint32_t crc = Crc32Png(0, out->data() + crc_start, out->size() - crc_start);
    AppendU32BE(out, crc);
}

bool WritePngFile(const char *path, int width, int height, const std::vector<uint8_t> &scanlines)
{
    if (path == nullptr || width <= 0 || height <= 0)
        return false;

    std::vector<uint8_t> raw;
    // Filter byte 0 (None) prefix for each row.
    raw.reserve((size_t)(width * 4) * (size_t)height + (size_t)height);
    for (int y = 0; y < height; y++)
    {
        raw.push_back(0);
        raw.insert(raw.end(), scanlines.begin() + (size_t)y * (size_t)width * 4, scanlines.begin() + (size_t)(y + 1) * (size_t)width * 4);
    }

    const uLong comp_bound = compressBound((uLong)raw.size());
    std::vector<uint8_t> compressed(comp_bound);
    uLong comp_len = comp_bound; // in: available space, out: actual size
    const int comp_result = compress2(compressed.data(), &comp_len, raw.data(), (uLong)raw.size(), 6);
    if (comp_result != Z_OK)
        return false;
    compressed.resize(comp_len);

    std::vector<uint8_t> png;
    png.insert(png.end(), kPngSignature, kPngSignature + 8);

    std::vector<uint8_t> ihdr_payload;
    AppendU32BE(&ihdr_payload, (uint32_t)width);
    AppendU32BE(&ihdr_payload, (uint32_t)height);
    ihdr_payload.push_back(8);  // bit depth
    ihdr_payload.push_back(6);  // color type RGBA
    ihdr_payload.push_back(0);  // compression
    ihdr_payload.push_back(0);  // filter
    ihdr_payload.push_back(0);  // interlace
    AppendChunk(&png, "IHDR", ihdr_payload.data(), ihdr_payload.size());
    AppendChunk(&png, "IDAT", compressed.data(), compressed.size());
    AppendChunk(&png, "IEND", nullptr, 0);

    FILE *f = fopen(path, "wb");
    if (f == nullptr)
        return false;
    const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size() && fflush(f) == 0;
    const int close_result = fclose(f);
    return ok && close_result == 0;
}
}

bool WritePngRgba(const char *path, int width, int height, const uint8_t *rgba)
{
    if (rgba == nullptr || width <= 0 || height <= 0)
        return false;
    std::vector<uint8_t> scanlines(rgba, rgba + (size_t)width * (size_t)height * 4);
    return WritePngFile(path, width, height, scanlines);
}

bool WritePngFromRgb565(const char *path, int width, int height, const uint16_t *rgb565)
{
    if (rgb565 == nullptr || width <= 0 || height <= 0)
        return false;

    const size_t pixel_count = (size_t)width * (size_t)height;
    std::vector<uint8_t> rgba(pixel_count * 4);
    for (size_t i = 0; i < pixel_count; i++)
    {
        const uint32_t v = rgb565[i];
        const uint8_t r5 = (uint8_t)((v >> 11) & 0x1f);
        const uint8_t g6 = (uint8_t)((v >> 5) & 0x3f);
        const uint8_t b5 = (uint8_t)(v & 0x1f);
        rgba[i * 4 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        rgba[i * 4 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        rgba[i * 4 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        rgba[i * 4 + 3] = 255;
    }
    return WritePngFile(path, width, height, rgba);
}

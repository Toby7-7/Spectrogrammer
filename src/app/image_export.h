// Copyright (c) 2026 Toby7-7
// SPDX-License-Identifier: Apache-2.0
//
// Minimal PNG exporter used by the "Save as PNG" feature.  Writes 8-bit
// RGBA PNGs with zlib-deflated IDAT chunks (links libz, present on both
// Android and Linux).

#ifndef IMAGE_EXPORT_H
#define IMAGE_EXPORT_H

#include <stdint.h>

// RGBA, 4 bytes/pixel, row 0 = top of the image.
bool WritePngRgba(const char *path, int width, int height, const uint8_t *rgba);

// RGB565 (waterfall image format), row 0 = top of the image.
bool WritePngFromRgb565(const char *path, int width, int height, const uint16_t *rgb565);

#endif // IMAGE_EXPORT_H

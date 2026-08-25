/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifndef NATIVE_AUDIO_ANDROID_DEBUG_H_H
#define NATIVE_AUDIO_ANDROID_DEBUG_H_H

// Portable logging for the audio layer: uses logcat on Android, maps to
// stderr on desktop builds (ALSA/SDL drivers and unit tests).
#define MODULE_NAME "AUDIO-ECHO"

#ifdef ANDROID
#include <android/log.h>

#define LOGV(...) \
  __android_log_print(ANDROID_LOG_VERBOSE, MODULE_NAME, __VA_ARGS__)
#define LOGD(...) \
  __android_log_print(ANDROID_LOG_DEBUG, MODULE_NAME, __VA_ARGS__)
#define LOGI(...) \
  __android_log_print(ANDROID_LOG_INFO, MODULE_NAME, __VA_ARGS__)
#define LOGW(...) \
  __android_log_print(ANDROID_LOG_WARN, MODULE_NAME, __VA_ARGS__)
#define LOGE(...) \
  __android_log_print(ANDROID_LOG_ERROR, MODULE_NAME, __VA_ARGS__)
#define LOGF(...) \
  __android_log_print(ANDROID_LOG_FATAL, MODULE_NAME, __VA_ARGS__)
#else
#include <cstdio>

#define LOGV(...) \
  do { \
    std::printf("V/" MODULE_NAME ": " __VA_ARGS__); \
    std::printf("\n"); \
  } while (0)
#define LOGD(...) \
  do { \
    std::printf("D/" MODULE_NAME ": " __VA_ARGS__); \
    std::printf("\n"); \
  } while (0)
#define LOGI(...) \
  do { \
    std::printf("I/" MODULE_NAME ": " __VA_ARGS__); \
    std::printf("\n"); \
  } while (0)
#define LOGW(...) \
  do { \
    std::printf("W/" MODULE_NAME ": " __VA_ARGS__); \
    std::printf("\n"); \
  } while (0)
#define LOGE(...) \
  do { \
    std::printf("E/" MODULE_NAME ": " __VA_ARGS__); \
    std::printf("\n"); \
  } while (0)
#define LOGF(...) \
  do { \
    std::printf("F/" MODULE_NAME ": " __VA_ARGS__); \
    std::printf("\n"); \
  } while (0)
#endif

#endif  // NATIVE_AUDIO_ANDROID_DEBUG_H_H

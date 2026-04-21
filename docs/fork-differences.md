# Fork Differences

This document summarizes the major differences between this repository and upstream `Spectrogrammer`. It describes what the current branch already contains, not a temporary work log.

## Project Direction
- The development focus shifted from a general experimental spectrum viewer to Android phone usage in portrait orientation.
- The UI is now English by default, with an in-app Simplified Chinese switch.
- The processing chain prioritizes stable, trustworthy spectrum display over experimental changes that risk breaking basic behavior.

## 1. Build System and Repository Layout
- `Makefile`
  - Reworked Android build and packaging flow
  - Added `init-submodules`, `doctor-android`, `push`, `run`, `logcat`, and `clean`
  - Detects Android SDK, NDK, and build-tools
  - Builds the Java foreground service and packages it into the APK
  - Keeps the APK name as `Spectrogrammer.apk`
- `README.md`
  - Rewritten for this fork as the default English-facing repository homepage
- `README.zh-CN.md`
  - Added as the Chinese mirror of the main project guide
- `docs/android-build-install.md`
  - Documents the current Android build, mDNS discovery, and Wi-Fi ADB workflow in English
- `docs/fork-differences.md`
  - Replaces the previous ad-hoc Chinese change log with a repository-friendly English summary
- `fastlane/metadata/...`
  - Updated store listing copy and screenshot references to match the current fork

## 2. Android Runtime and System Behavior
- `src/main_android.cpp`
  - Adds microphone permission checks and gated startup
  - Loads system fonts for better CJK rendering
  - Supports Android back behavior: back out of settings, then move the task to background from the main screen
  - Supports state recovery when returning from background and keep-screen-on handling
  - Keeps the foreground-service notification language aligned with the current app language
- `src/AndroidManifest.xml`
- `src/AndroidManifest.xml.template`
  - Adds permissions such as `RECORD_AUDIO`, `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MICROPHONE`, and `WAKE_LOCK`
  - Registers the foreground service
- `src/java/org/nanoorg/Spectrogrammer/CaptureForegroundService.java`
  - Provides the microphone foreground service used for background capture

## 3. Configuration Persistence and Migration
- `src/app/AppConfig.h`
- `src/app/AppConfig.cpp`
  - Adds versioned configuration storage and migration logic
  - Current configuration version is `11`
  - Persists settings such as:
    - UI language
    - Channel mode
    - Left/right swap
    - Spectrum visibility
    - Overlay text size
    - Overlay text opacity
    - Input gain
  - Current defaults include:
    - UI language `English`
    - Audio source `Default`
    - Channel mode `Stereo split`
    - Sample rate `Auto / 48000 Hz`
    - Input gain `0 dB`
    - FFT size `4096`
    - Smoothing `0.10`
    - Peak marker source `Short hold`
    - Peak hold enabled with `4 s` falloff
    - Background capture enabled
  - Applies safety bounds, including:
    - Scroll speed lower bound `2 ms`
    - FFT upper bound `8192`
    - Peak hold falloff range `0` to `120 s`
    - Input gain range `-24 dB` to `+24 dB`
    - `0` meaning no falloff

## 4. Audio Capture and Processing
- `src/audio/audio_main.cpp`
- `src/audio/audio_main.h`
- `src/audio/audio_recorder.cpp`
- `src/audio/audio_recorder.h`
- `src/audio/audio_driver_alsa.cpp`
- `src/audio/audio_driver_sdl.cpp`
  - Threads audio-source preset, sample rate, and initialization options through the recording chain
  - Falls back to mono input if stereo capture is unavailable
- `src/app/Processor.h`
- `src/app/fft.h`
- `src/app/pass_through.h`
- `src/app/ChunkerProcessor.cpp`
- `src/app/ChunkerProcessor.h`
  - Extends processing parameters for sample rate, FFT size, decimation, window function, target transform interval, and exponential smoothing
  - Adds stereo preprocessing for `Stereo difference`
  - Chooses one-channel or two-channel FFT processing dynamically from the selected channel mode
  - Applies input gain in the display-analysis path instead of mutating the raw recording chain
  - Keeps the more stable main processing path and avoids experimental buffer rewrites that previously introduced broad-spectrum artifacts

## 5. Main UI and Interaction
- `src/app/Spectrogrammer.cpp`
- `src/app/Spectrogrammer.h`
  - Reworked the main screen for phone usage
  - Keeps the top toolbar with four actions: `Pause/Resume`, `Clear Peaks`, `Clear Cursor`, and `Settings`
  - Adds a status line showing input sample rate, view range, FFT size, and frequency resolution
  - Uses categorized settings pages with larger touch targets and spacing for handheld use
- Main plot interaction
  - Single-finger tap or drag moves the manual cursor
  - The cursor appears in both spectrum and waterfall views and shows the current frequency and dB value
  - Two-finger horizontal gestures zoom and pan the current frequency view
- Stereo visualization
  - Supports `Mono`, `Left channel`, `Right channel`, `Stereo mix`, `Stereo difference`, and `Stereo split`
  - `Stereo split` supports left/right order swapping
  - The current target device mapping was adjusted so the displayed left/right channels match the actual microphones
- Peak handling
  - Adds a peak-hold trace
  - Peak markers can use either the live trace or the short-hold trace
  - Peak falloff is configurable and can also be disabled entirely
- Overlay text
  - Overlay label size and opacity are configurable
  - Split stereo mode uses compact `L / R` or `左 / 右` legends to reduce clutter

## 6. Frequency Scales, Axes, and Waterfall
- `src/app/ScaleUI.cpp`
- `src/app/ScaleUI.h`
- `src/app/scale.h`
- `src/app/ScaleBufferX.h`
  - Supports `Linear`, `Logarithmic`, `Music log`, `Mel`, `Bark`, and `ERB`
  - Reworks scale generation so zoomed and high-frequency views stay usable
- `src/app/waterfall.cpp`
- `src/app/waterfall.h`
  - Adds CPU-side history storage
  - Handles view-size changes and GL context loss recovery
  - Fixes waterfall orientation, skew, and row alignment issues
  - Includes the important `GL_UNPACK_ALIGNMENT = 1` fix
  - Restores waterfall and spectrum state after returning from background
- `src/app/imgui_helpers.cpp`
  - Compresses spectrum drawing to screen-pixel columns before rendering so `8192 FFT + Stereo split` does not overflow Dear ImGui 16-bit index limits

## 7. Current Adjustable Features
- Audio source: `Default / Generic / Voice recognition / Camcorder / Unprocessed`
- Channel mode: `Mono / Left channel / Right channel / Stereo mix / Stereo difference / Stereo split`
- Input gain: `-24 dB` to `+24 dB`
- Sample rate: `Auto` or fixed rates, with the highest selectable value depending on device support
- FFT size: `128` to `8192`
- Decimation stages: `0` to `5`
- Window function: `Rectangular / Hann / Hamming / Blackman-Harris`
- Scroll speed: `2 ms` to `250 ms`
- Exponential smoothing: `0.00` to `0.95`
- Overlay text size: `0.7` to `1.8`
- Overlay text opacity: `0.25` to `1.0`
- Frequency-axis scale: `Linear / Logarithmic / Music log / Mel / Bark / ERB`
- Spectrum and waterfall visibility toggles
- Waterfall height: `1/4 screen / 1/3 screen / 2/5 screen / 1/2 screen / 3/5 screen / 2/3 screen / 3/4 screen`
- Peak marker count: `0 / 1 / 3 / 5`
- Peak marker source: `Live / Short hold`
- Peak hold falloff: `0` to `120 s`
- Runtime controls: `Background capture / Keep screen on`

## 8. Current Status and Caveats
- The fork currently prioritizes Android phone usability while keeping the Linux build path available.
- High sample rates and the `Unprocessed` source still depend on the target device driver.
- When a fixed high sample rate is unavailable, the app attempts to fall back to a lower usable rate.
- This document intentionally focuses on source and product behavior, not on temporary local artifacts such as APKs, dex outputs, or tool-specific workspace files.

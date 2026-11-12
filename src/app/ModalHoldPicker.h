#ifndef MODAL_HOLD_PICKER_H
#define MODAL_HOLD_PICKER_H

#include <stdint.h>
#include <string>
#include "BufferIO.h"

enum class HoldPickerResult
{
    None = 0,
    Loaded = 1,
    Saved = 2,
    Deleted = 3,
    Renamed = 4
};

void RefreshFiles(const char *pWorkingDirectory);
void FreeHoldPickerFiles();

HoldPickerResult HoldPicker(const char *pWorkingDirectory,
                            bool bCanSave,
                            const BufferIODouble *in_snapshot,
                            BufferIODouble *out_loaded,
                            float *sample_rate,
                            uint32_t *fft_size,
                            std::string *status_msg);

#endif
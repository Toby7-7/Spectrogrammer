#ifndef PROCESSOR_H
#define PROCESSOR_H

#define _USE_MATH_DEFINES
#include "auformat.h"
#include "BufferIO.h"
#include "AppConfig.h"

class Processor
{
public:
    virtual ~Processor() { };
    virtual void init(int length, float sampleRate, int decimationFactor, WindowFunctionType windowFunction) = 0;
    virtual const char *GetName() const = 0;

    virtual int getProcessedLength() const = 0;
    virtual int getBinCount() const = 0;
    virtual void convertShortToFFT(const AU_FORMAT *input, int offsetDest, int length, int inputStride) = 0;
    virtual void convertFloatToFFT(const float *input, int offsetDest, int length) = 0;
    virtual void computePower(float decay) = 0;
    // Peak/total bin power of the last computePower() call, full-scale units.
    virtual float getLastPeakPower() const { return 0.0f; }
    virtual float getLastTotalPower() const { return 0.0f; }
    virtual float bin2Freq(int bin) const = 0;
    virtual float freq2Bin(float freq) const = 0;

    virtual BufferIODouble *getBufferIO() = 0;
};

#endif

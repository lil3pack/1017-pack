#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace th::dsp
{
    /**
     * Lock-free ring buffer for oscilloscope visualisation.
     *
     * Audio thread pushes peak-decimated samples (one frame = max of a small
     * sample chunk). GUI thread reads the most recent N frames into a linear
     * array for painting. No allocations, no locks.
     */
    class ScopeBuffer
    {
    public:
        static constexpr int frameSize      = 2048;    // visible frames
        static constexpr int decimationMax  = 32;      // samples per frame

        void setDecimation (int samples) noexcept
        {
            decimation = std::max (1, std::min (samples, decimationMax));
        }

        // Audio thread
        void push (float sample) noexcept
        {
            const float abs = std::abs (sample);
            if (abs > accum) accum = abs;
            if (sample > accumSigned || accumSigned == 0.0f) accumSigned = sample;

            if (++sampleCount >= decimation)
            {
                const int w = writeIndex.load (std::memory_order_relaxed);
                frames[w] = accumSigned; // keep sign so the scope looks like a waveform
                writeIndex.store ((w + 1) % frameSize, std::memory_order_release);
                sampleCount = 0;
                accum = 0.0f;
                accumSigned = 0.0f;
            }
        }

        void reset() noexcept
        {
            frames.fill (0.0f);
            writeIndex.store (0, std::memory_order_release);
            sampleCount = 0;
            accum = 0.0f;
            accumSigned = 0.0f;
        }

        // GUI thread — copies the most recent `count` frames in chronological order.
        void readLatest (float* dest, int count) const noexcept
        {
            count = std::min (count, frameSize);
            const int w = writeIndex.load (std::memory_order_acquire);
            for (int i = 0; i < count; ++i)
            {
                const int idx = (w - count + i + frameSize) % frameSize;
                dest[i] = frames[idx];
            }
        }

    private:
        std::array<float, frameSize> frames {};
        std::atomic<int> writeIndex { 0 };

        int   decimation   { 8 };
        int   sampleCount  { 0 };
        float accum        { 0.0f };
        float accumSigned  { 0.0f };
    };
}

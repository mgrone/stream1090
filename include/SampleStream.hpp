/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Sampler.hpp"
#include "BlockRing.hpp"
#include "DemodCore.hpp"
#include "MessageHandler.hpp"

// the main stream class. This class manages reading from the input stream
// and also manages the buffers
template<typename Sampler>
class SampleStream {
public:
    static constexpr size_t NumInputBuffers = 2;
    // for now we will keep one extra sample buffer as history
    static constexpr size_t NumSampleBuffers = 2;
    static constexpr size_t TotalSampleBufferLength = NumSampleBuffers * Sampler::SampleBufferSize + Sampler::SampleBufferOverlap;

    // How long a decoded frame may sit in the output buffer before it is
    // pushed out. Ten milliseconds is below anything downstream notices - a
    // tracker sees aircraft at 2 Hz, and MLAT reads the timestamp carried
    // inside the frame rather than the arrival time of the bytes - and it
    // batches roughly six frames per flush at a busy site.
    static constexpr size_t FlushIntervalMicros = 10000;
    // a block is SampleBufferSize / NumStreams bit periods, i.e. microseconds
    static constexpr size_t BlockMicros = Sampler::SampleBufferSize / Sampler::NumStreams;
    static constexpr size_t FlushEveryBlocks =
        (FlushIntervalMicros + BlockMicros - 1) / BlockMicros;

    SampleStream() : m_inputRingBuffer(0), m_sampleRingBuffer(0) { }
   
    // the main method that streams from InputStream using inputReader
    template<typename InputReaderType, MessageHandler Handler>
    void read(InputReaderType& inputReader, Handler& messageHandler);

    /// Recompute the demodulator's confidence in a single frame bit, straight
    /// from the retained sample ring. The demodulation loop deliberately stores
    /// nothing: this is only ever called for a frame that already matched a
    /// trusted address, a few dozen times a second, so recomputing is far
    /// cheaper than keeping a per-bit history for every stream.
    float confidenceAt(uint8_t frameBit, size_t stream) const noexcept {
        // frame bit f was shifted in (16 + f) bit periods ago; see
        // ShiftRegisters::extractAlignedFrameLong for the 16 bit alignment
        const size_t delay = (size_t(16) + frameBit) * Sampler::NumStreams;
        const auto offsetInBlock = size_t(m_demodPos - m_sampleRingBuffer.readPos());
        // the ring holds linear magnitudes scaled by MagScale
        const float first  = float(m_sampleRingBuffer.lookBack(delay - stream, offsetInBlock));
        const float second = float(m_sampleRingBuffer.lookBack(delay - stream - (Sampler::NumStreams >> 1),
                                                               offsetInBlock));
        return std::fabs(first - second) * (1.0f / MagScale);
    }

    uint8_t getRSSI(uint8_t frameBit) const noexcept {
        // we are in total 128 bits late already. So the first bit is 128 * NumStreams samples old.
        const size_t delay = (size_t(128) - frameBit) * Sampler::NumStreams;
        const auto offsetInBlock = size_t(m_demodPos - m_sampleRingBuffer.readPos());
        // we assume here that we hit the message quite early.
        // Subsequent streams probably would have done a better job.
        // We therefore iterate and take the maximum. Even if stream 3 got a hit, we start at 0
        int32_t peak = 0;
        for (size_t s = 0; s < Sampler::NumStreams; s++) {
            const int32_t v  = m_sampleRingBuffer.lookBack(delay - s, offsetInBlock);
            peak = std::max(peak, v);
        }
        // conversion to float
        float rssi = float(peak) * (1.0f / MagScale);
        // normalize
        rssi = std::min(1.41f, rssi) / 1.41f;
        // and return as byte
        return uint8_t(rssi * 255.0);
    }

    // implements concept RssiProvider for getting the rssi value for long messages
    uint8_t getRSSILong() const noexcept {
        // we sample in the middle of the 112 bit long frame
        return getRSSI(56);
    }

    // implements concept RssiProvider for getting the rssi value for short messages
    uint8_t getRSSIShort() const noexcept {
        // we sample in the middle of the 56 bit short frame
        return getRSSI(28);
    }

    /// Ratio of the weakest preamble pulse to the strongest inter-pulse gap
    /// across the eight preamble slots, sampled just before the frame. A real
    /// preamble scores well above 1; noise hovers around 0.5.
    float preambleScore(size_t stream) const noexcept {
        return preambleScoreAt(stream, 135);
    }

    float preambleScoreAt(size_t stream, size_t PreambleStart) const noexcept {
        constexpr size_t Half = Sampler::NumStreams >> 1;
        const auto offsetInBlock = size_t(m_demodPos - m_sampleRingBuffer.readPos());

        float pulse = std::numeric_limits<float>::max();
        float gap = 0.0f;

        for (size_t slot = 0; slot < 8; ++slot) {
            const size_t base = (PreambleStart - slot) * Sampler::NumStreams;
            for (size_t h = 0; h < 2; ++h) {
                const size_t off = base - stream - h * Half;
                float sum = 0.0f;
                for (size_t k = 0; k < Half; ++k)
                    sum += m_sampleRingBuffer.lookBack(off - k, offsetInBlock);

                const bool isPulse = (h == 0 && (slot == 0 || slot == 1))
                                  || (h == 1 && (slot == 3 || slot == 4));
                if (isPulse)
                    pulse = std::min(pulse, sum);
                else
                    gap = std::max(gap, sum);
            }
        }

        return (gap > 0.0f) ? (pulse / gap) : 0.0f;
    }

    /// Signal-to-noise ratio of a frame: the peak magnitude across the frame's
    /// data region over the median magnitude of a quiet window before it. This
    /// is a ratio, so it is independent of receiver gain. A real transmission
    /// sits well above its local noise floor; a fabricated frame is at it.
    float snr(size_t frameBit) const noexcept {
        const auto offsetInBlock = size_t(m_demodPos - m_sampleRingBuffer.readPos());

        int32_t peak = 0;
        {
            const size_t delay = (size_t(128) - frameBit) * Sampler::NumStreams;
            for (size_t s = 0; s < Sampler::NumStreams; s++)
                peak = std::max(peak, m_sampleRingBuffer.lookBack(delay - s, offsetInBlock));
        }

        // noise floor: a low percentile of magnitudes over a quiet window
        // before the preamble. The low percentile ignores any other
        // transmission that happens to fall in the window, so the estimate is
        // the receiver's own noise level and the ratio transfers between sites.
        constexpr size_t NoiseBits = 64;
        constexpr size_t StartBit = 200;
        std::array<int32_t, NoiseBits * Sampler::NumStreams> window{};
        size_t n = 0;
        for (size_t b = 0; b < NoiseBits; b++) {
            const size_t delay = (size_t(StartBit) + b) * Sampler::NumStreams;
            for (size_t s = 0; s < Sampler::NumStreams; s++) {
                window[n++] = m_sampleRingBuffer.lookBack(delay - s, offsetInBlock);
                if (n == window.size())
                    break;
            }
            if (n == window.size())
                break;
        }
        std::sort(window.begin(), window.begin() + n);
        const float noise = float(window[n / 4]);
        if (noise <= 0.0f)
            return 0.0f;
        return float(peak) / noise;
    }

private:
    // Samples reach the ring as ((I*I) >> 2) + ((Q*Q) >> 2) with I and Q in
    // Q14, so the stored value is the squared magnitude times (SampleOne/2)^2
    // and a linear magnitude comes back as stored / MagScale.
    static constexpr float MagScale = float(SampleOne) * 256.0f;

    uint32_t m_newBits[Sampler::NumStreams];
    // we have one ring buffer for the IQ pipeline
    BlockRing<int32_t, Sampler::InputBufferSize,  NumInputBuffers,  Sampler::InputBufferOverlap>  m_inputRingBuffer;
    // and one for the upsampled magnitudes
    BlockRing<int32_t, Sampler::SampleBufferSize, NumSampleBuffers, Sampler::SampleBufferOverlap> m_sampleRingBuffer;
    // not nice. Will change
    const int32_t* m_demodPos = nullptr;
    size_t m_blocksSinceFlush = 0;
};


template<typename Sampler>
template<typename InputReaderType, MessageHandler Handler>
inline void SampleStream<Sampler>::read(InputReaderType& inputReader, Handler& messageHandler) {  
    // the core logic for message recognition
    DemodCore<Sampler::NumStreams, Handler> demodCore(messageHandler);
    demodCore.setConfidenceSource(this, [](const void* ctx, uint8_t bit, size_t stream) -> float {
        return static_cast<const SampleStream<Sampler>*>(ctx)->confidenceAt(bit, stream);
    });
    const auto preambleSource = [](const void* ctx, size_t stream) -> float {
        return static_cast<const SampleStream<Sampler>*>(ctx)->preambleScore(stream);
    };
    demodCore.setPreambleSource(this, preambleSource);
    const auto snrSource = [](const void* ctx, uint8_t frameBit) -> float {
        return static_cast<const SampleStream<Sampler>*>(ctx)->snr(frameBit);
    };
    demodCore.setSnrSource(this, snrSource);

     // the main loop for reading the stream
    while (!inputReader.eof()) {
        // the read and write positions for the current sample buffer based its index.
        // we start reading at 0 + i * size           
        // however, new values will be written NumStream / 2 later which is the overlap. 
        // check if actually we need the sampler to resample, or if this is a 1:1 sampling
        if constexpr(Sampler::isPassthrough) {
            // tell the input reader to get us some data. Directly as magnitude. Since this is a passthrough sampler
            // we will directly read into the samples buffer. There is no need for using the sampler at all.
            // This works because the amount the input reader is getting us in this particular case is exactly the ChunkSize
            static_assert(Sampler::NumBlocks == Sampler::InputBufferSize);
            inputReader.readMagnitude(m_sampleRingBuffer.writePos());
            m_sampleRingBuffer.advanceWritePos();
        } else {
            // tell the input reader to get us some data. Directly as magnitude.
            inputReader.readMagnitude(m_inputRingBuffer.writePos());
            m_inputRingBuffer.advanceWritePos();
            // now ask the Sampler to resample the input magnitude to the output samples
            // similar to the input buffer, write after the overlap to keep some old values for the next iteration
            if (m_inputRingBuffer.isReadable()) {
                Sampler::sample(m_inputRingBuffer.readPos(), m_sampleRingBuffer.writePos());
                m_inputRingBuffer.advanceReadPos();
                m_sampleRingBuffer.advanceWritePos();
            }
        }
        

        if (m_sampleRingBuffer.isReadable()) {
            m_demodPos = m_sampleRingBuffer.readPos();
            // extract phase shifted bits using manchester encoding
            for (size_t i = 0; i < Sampler::SampleBufferSize; i += Sampler::NumStreams) {
                for (size_t j = 0; j < Sampler::NumStreams; j++) {
                    // Think of having a sample stream of 2Mhz (so what we get from the planes)
                    // stream 0 << compare 0 and 1 
                    // stream 1 << compare 1 and 2
                    // 
                    // stream 0 << compare 2 and 3
                    // stream 1 << compare 3 and 4
                    // ....
                    // because the message might be shifted by one symbol
                    m_newBits[j] = m_demodPos[j] > m_demodPos[j + (Sampler::NumStreams >> 1)]; 
                    //m_sampleReadPos[i + j] > sampleReadPos[i + j + Sampler::SampleBufferOverlap];  
                }
                // and tell the demodulator to deal with the new bits
                demodCore.shiftInNewBits(m_newBits);
                // advance the readpos
                m_demodPos += Sampler::NumStreams;
            }
            m_sampleRingBuffer.advanceReadPos();

            // Hand the buffered frames to the operating system on a fixed
            // cadence rather than one frame at a time. The block boundary is
            // already on this thread, so nothing has to be locked, and it
            // ticks with the samples rather than with the traffic: a lone
            // frame at a quiet site still leaves within FlushIntervalMicros,
            // which a timer could only manage by flushing the stream from a
            // second thread.
            if (++m_blocksSinceFlush >= FlushEveryBlocks) {
                m_blocksSinceFlush = 0;
                if constexpr (requires { messageHandler.flush(); })
                    messageHandler.flush();
            }
        }
    }

    // and whatever is left over when the input ends
    if constexpr (requires { messageHandler.flush(); })
        messageHandler.flush();
}

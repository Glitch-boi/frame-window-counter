#pragma once
// ---------------------------------------------------------------------------------------------
// Software SFX mixer for Frame Window Counter.
//
// Why this exists: FMOD gives every sound its own "channel". Channels are limited, every
// playSound() call costs ~10 locked FMOD calls, and once the limit is hit new sounds steal old
// ones - so a macro with thousands of inputs per second turns into "each sound cuts the previous
// one". This mixer instead adds every sound into ONE output stream, so:
//   * nothing is ever stolen or overwritten - N overlapping sounds are simply summed;
//   * a sound costs one lock-free queue push on the game thread (no FMOD calls at all);
//   * sounds are placed on a sample-accurate timeline derived from the game's own frame numbers,
//     so inputs inside one tick are spread across that tick instead of piling up on one sample.
//
// This header has NO dependency on Geode or FMOD so it can be unit tested off-game
// (see tests/audiomixer_test.cpp). The FMOD glue lives in SoundManager.cpp.
// ---------------------------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace fwcaudio {

// ----------------------------------------------------------------------------------------------
// Clips: decoded once, stored as interleaved STEREO float at the mixer's sample rate.
// ----------------------------------------------------------------------------------------------
struct Clip {
    std::vector<float> data;     // interleaved L R L R ...
    uint32_t frames = 0;         // number of stereo frames
    uint32_t sampleRate = 0;
};
using ClipPtr = std::shared_ptr<const Clip>;

enum class PcmFormat { PCM8, PCM16, PCM24, PCM32, Float };

inline size_t bytesPerSample(PcmFormat f) {
    switch (f) {
    case PcmFormat::PCM8: return 1;
    case PcmFormat::PCM16: return 2;
    case PcmFormat::PCM24: return 3;
    case PcmFormat::PCM32: return 4;
    case PcmFormat::Float: return 4;
    }
    return 0;
}

// Reads one sample (little endian, which is every platform Geode runs on).
inline float readSample(const uint8_t* p, PcmFormat f) {
    switch (f) {
    case PcmFormat::PCM8:
        return static_cast<float>(static_cast<int8_t>(p[0])) / 128.0f;
    case PcmFormat::PCM16: {
        int16_t v;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 32768.0f;
    }
    case PcmFormat::PCM24: {
        int32_t v = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) | (static_cast<int32_t>(p[2]) << 16);
        if (v & 0x800000) v |= static_cast<int32_t>(0xFF000000u); // sign-extend
        return static_cast<float>(v) / 8388608.0f;
    }
    case PcmFormat::PCM32: {
        int32_t v;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(static_cast<double>(v) / 2147483648.0);
    }
    case PcmFormat::Float: {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return std::isfinite(v) ? v : 0.0f;
    }
    }
    return 0.0f;
}

// Converts raw interleaved PCM into a stereo float Clip at `dstRate`.
//  * mono is duplicated to both ears, >2 channels keep the first two
//  * sample-rate conversion is linear interpolation (plenty for short click SFX)
//  * TRAILING digital silence (below trimThreshold) is cut so a voice finishes - and stops
//    costing CPU - as soon as the sound is actually inaudible. Leading audio is never touched.
// Returns nullptr for empty / invalid / completely silent input.
inline ClipPtr buildClip(const uint8_t* bytes, size_t byteLen, PcmFormat fmt, int channels,
                         int srcRate, int dstRate, float trimThreshold = 1e-4f) {
    const size_t bps = bytesPerSample(fmt);
    if (!bytes || bps == 0 || channels < 1 || srcRate <= 0 || dstRate <= 0) return nullptr;

    const size_t frameBytes = bps * static_cast<size_t>(channels);
    const size_t srcFrames = byteLen / frameBytes;
    if (srcFrames == 0) return nullptr;

    // 1) decode to stereo float at the source rate
    std::vector<float> src(srcFrames * 2);
    for (size_t i = 0; i < srcFrames; i++) {
        const uint8_t* fp = bytes + i * frameBytes;
        float l = readSample(fp, fmt);
        float r = channels >= 2 ? readSample(fp + bps, fmt) : l;
        src[i * 2] = l;
        src[i * 2 + 1] = r;
    }

    // 2) resample
    auto clip = std::make_shared<Clip>();
    clip->sampleRate = static_cast<uint32_t>(dstRate);
    if (srcRate == dstRate) {
        clip->data = std::move(src);
        clip->frames = static_cast<uint32_t>(srcFrames);
    }
    else {
        const double ratio = static_cast<double>(srcRate) / static_cast<double>(dstRate);
        const size_t dstFrames = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(srcFrames) / ratio)));
        clip->data.resize(dstFrames * 2);
        for (size_t i = 0; i < dstFrames; i++) {
            double pos = static_cast<double>(i) * ratio;
            size_t i0 = static_cast<size_t>(pos);
            if (i0 >= srcFrames) i0 = srcFrames - 1;
            size_t i1 = std::min(i0 + 1, srcFrames - 1);
            float frac = static_cast<float>(pos - static_cast<double>(i0));
            for (size_t c = 0; c < 2; c++) {
                float a = src[i0 * 2 + c];
                float b = src[i1 * 2 + c];
                clip->data[i * 2 + c] = a + (b - a) * frac;
            }
        }
        clip->frames = static_cast<uint32_t>(dstFrames);
    }

    // 3) trim trailing silence
    uint32_t keep = clip->frames;
    while (keep > 0) {
        float l = std::fabs(clip->data[(keep - 1) * 2]);
        float r = std::fabs(clip->data[(keep - 1) * 2 + 1]);
        if (l > trimThreshold || r > trimThreshold) break;
        keep--;
    }
    if (keep == 0) return nullptr; // nothing audible in this file
    if (keep != clip->frames) {
        clip->frames = keep;
        clip->data.resize(static_cast<size_t>(keep) * 2);
        clip->data.shrink_to_fit();
    }
    return clip;
}

// ----------------------------------------------------------------------------------------------
// Mixer
// ----------------------------------------------------------------------------------------------
//  Game thread  : schedule(), requestClear(), position()         (single producer)
//  Audio thread : render()                                       (single consumer)
// Everything shared between the two is atomic or goes through the lock-free ring buffer.
class Mixer {
public:
    struct Stats {
        uint64_t scheduled = 0;      // voices accepted into the queue
        uint64_t droppedQueueFull = 0;
        uint64_t droppedVoiceCap = 0;
        uint32_t activeVoices = 0;   // as of the last rendered block
        uint32_t peakActiveVoices = 0;
        float limiterGain = 1.0f;    // 1.0 = limiter idle, <1 = reducing level
    };

    // maxActive is only a SAFETY net (CPU / memory) for absurd inputs; it is not a rate limit.
    explicit Mixer(uint32_t sampleRate, size_t ringCapacity = (1u << 16), size_t maxActive = 8192)
        : m_rate(sampleRate ? sampleRate : 44100),
          m_ringMask(roundUpPow2(ringCapacity) - 1),
          m_ring(m_ringMask + 1),
          m_maxActive(std::max<size_t>(1, maxActive)) {
        m_active.reserve(m_maxActive);
        // limiter level recovers (doubles) roughly every 80 ms
        m_releasePerSample = std::pow(2.0, 1.0 / (0.08 * static_cast<double>(m_rate)));
    }

    uint32_t sampleRate() const { return m_rate; }

    // ---- game thread -------------------------------------------------------------------------
    // Queues `clip` to start at absolute timeline sample `startSample` (see position()).
    // A start in the past means "as soon as possible", so nothing is ever skipped because it was
    // scheduled late. Returns false only if the queue is completely full (counted in stats).
    bool schedule(const ClipPtr& clip, uint64_t startSample, float gain = 1.0f) {
        if (!clip || clip->frames == 0) return false;
        const uint64_t tail = m_tail.load(std::memory_order_relaxed);
        const uint64_t head = m_head.load(std::memory_order_acquire);
        if (tail - head >= m_ring.size()) {
            m_droppedQueueFull.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        Slot& s = m_ring[tail & m_ringMask];
        s.clip = clip;
        s.start = startSample;
        s.gain = gain;
        s.epoch = m_clearEpoch.load(std::memory_order_relaxed);
        m_tail.store(tail + 1, std::memory_order_release);
        m_scheduled.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Drops every voice scheduled BEFORE this call (queued and playing). Voices scheduled after
    // it are unaffected, even if the audio thread has not processed the clear yet.
    void requestClear() { m_clearEpoch.fetch_add(1, std::memory_order_release); }

    // Samples rendered so far = "now" on the mixer timeline.
    uint64_t position() const { return m_position.load(std::memory_order_acquire); }

    Stats stats() const {
        Stats st;
        st.scheduled = m_scheduled.load(std::memory_order_relaxed);
        st.droppedQueueFull = m_droppedQueueFull.load(std::memory_order_relaxed);
        st.droppedVoiceCap = m_droppedVoiceCap.load(std::memory_order_relaxed);
        st.activeVoices = m_activeCount.load(std::memory_order_relaxed);
        st.peakActiveVoices = m_peakActive.load(std::memory_order_relaxed);
        st.limiterGain = m_limiterGainPublic.load(std::memory_order_relaxed);
        return st;
    }

    // ---- audio thread ------------------------------------------------------------------------
    // Writes `frames` interleaved stereo frames (overwrites `out`).
    // Never allocates (m_active is pre-reserved), never locks, never calls into FMOD.
    void render(float* out, uint32_t frames) {
        if (!out || frames == 0) return;
        std::memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(float));

        // 1) honour clear requests
        const uint32_t epoch = m_clearEpoch.load(std::memory_order_acquire);
        if (epoch != m_seenEpoch) {
            m_seenEpoch = epoch;
            m_active.erase(std::remove_if(m_active.begin(), m_active.end(),
                [&](const Voice& v) { return v.epoch < epoch; }), m_active.end());
        }

        // 2) move newly scheduled voices from the ring into the active list
        {
            uint64_t head = m_head.load(std::memory_order_relaxed);
            const uint64_t tail = m_tail.load(std::memory_order_acquire);
            while (head != tail) {
                Slot& s = m_ring[head & m_ringMask];
                if (s.epoch >= epoch) {
                    if (m_active.size() < m_maxActive) {
                        m_active.push_back(Voice{ std::move(s.clip), s.start, 0u, s.gain, s.epoch });
                    }
                    else {
                        m_droppedVoiceCap.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                s.clip.reset();
                head++;
            }
            m_head.store(head, std::memory_order_release);
        }

        // 3) sum every voice that overlaps this block
        const uint64_t blockStart = m_pos;
        const uint64_t blockEnd = blockStart + frames;
        size_t i = 0;
        while (i < m_active.size()) {
            Voice& v = m_active[i];
            if (v.start >= blockEnd) { ++i; continue; } // not due yet

            const Clip& c = *v.clip;
            const uint32_t dst = v.start > blockStart ? static_cast<uint32_t>(v.start - blockStart) : 0u;
            const uint32_t n = std::min(frames - dst, c.frames - v.pos);
            const float* sp = c.data.data() + static_cast<size_t>(v.pos) * 2;
            float* dp = out + static_cast<size_t>(dst) * 2;
            const size_t cnt = static_cast<size_t>(n) * 2;
            if (v.gain == 1.0f) {
                for (size_t k = 0; k < cnt; k++) dp[k] += sp[k];
            }
            else {
                const float g = v.gain;
                for (size_t k = 0; k < cnt; k++) dp[k] += sp[k] * g;
            }
            v.pos += n;

            if (v.pos >= c.frames) {
                if (i + 1 != m_active.size()) m_active[i] = std::move(m_active.back());
                m_active.pop_back(); // re-check the element swapped into slot i
            }
            else {
                ++i;
            }
        }

        // 4) keep the (possibly huge) sum inside [-1, 1] without harsh clipping
        applyLimiter(out, frames);

        m_pos = blockEnd;
        m_position.store(blockEnd, std::memory_order_release);

        const uint32_t activeNow = static_cast<uint32_t>(m_active.size());
        m_activeCount.store(activeNow, std::memory_order_relaxed);
        if (activeNow > m_peakActive.load(std::memory_order_relaxed)) {
            m_peakActive.store(activeNow, std::memory_order_relaxed);
        }
    }

private:
    struct Slot {
        ClipPtr clip;
        uint64_t start = 0;
        float gain = 1.0f;
        uint32_t epoch = 0;
    };
    struct Voice {
        ClipPtr clip;
        uint64_t start;
        uint32_t pos;
        float gain;
        uint32_t epoch;
    };

    static size_t roundUpPow2(size_t v) {
        size_t p = 2;
        while (p < v) p <<= 1;
        return p;
    }

    // Look-ahead (the whole block is already mixed) peak limiter:
    //  * fast attack: gain drops to the level that keeps the block under the ceiling within ~1.5 ms
    //  * smooth release: gain recovers by doubling every ~80 ms
    //  * idle (exact pass-through) whenever the mix is below the ceiling
    void applyLimiter(float* out, uint32_t frames) {
        constexpr float kCeiling = 0.95f;
        constexpr uint32_t kAttackFrames = 64;

        float peak = 0.0f;
        bool bad = false;
        const size_t cnt = static_cast<size_t>(frames) * 2;
        for (size_t k = 0; k < cnt; k++) {
            const float a = std::fabs(out[k]);
            if (!std::isfinite(a)) bad = true; // note: std::max would silently swallow a NaN
            else if (a > peak) peak = a;
        }
        if (bad) { // never let a corrupt value poison the stream: emit silence for this block
            std::memset(out, 0, cnt * sizeof(float));
            m_gain = 1.0f;
            return;
        }

        const float required = peak > kCeiling ? kCeiling / peak : 1.0f;
        float target;
        uint32_t ramp;
        if (required < m_gain) {
            target = required;
            ramp = std::min(frames, kAttackFrames);
        }
        else {
            const double grown = static_cast<double>(m_gain) * std::pow(m_releasePerSample, static_cast<double>(frames));
            target = static_cast<float>(std::min<double>({ static_cast<double>(required), 1.0, grown }));
            ramp = frames;
        }
        target = std::max(target, 1e-6f);

        if (m_gain == 1.0f && target == 1.0f) {
            // fast path - exact pass-through (peak <= ceiling <= 1, nothing to clamp)
        }
        else {
            const float g0 = m_gain;
            for (uint32_t f = 0; f < frames; f++) {
                float g = f < ramp ? g0 + (target - g0) * (static_cast<float>(f + 1) / static_cast<float>(ramp)) : target;
                float l = out[f * 2] * g;
                float r = out[f * 2 + 1] * g;
                out[f * 2] = std::max(-1.0f, std::min(1.0f, l));
                out[f * 2 + 1] = std::max(-1.0f, std::min(1.0f, r));
            }
        }
        m_gain = target;
        m_limiterGainPublic.store(target, std::memory_order_relaxed);
    }

    const uint32_t m_rate;
    const size_t m_ringMask;
    std::vector<Slot> m_ring;
    const size_t m_maxActive;

    // producer side
    alignas(64) std::atomic<uint64_t> m_tail{ 0 };
    // consumer side
    alignas(64) std::atomic<uint64_t> m_head{ 0 };
    alignas(64) std::atomic<uint64_t> m_position{ 0 };
    std::atomic<uint32_t> m_clearEpoch{ 0 };

    // audio-thread-only state
    std::vector<Voice> m_active;
    uint64_t m_pos = 0;
    uint32_t m_seenEpoch = 0;
    float m_gain = 1.0f;
    double m_releasePerSample = 1.0;

    // statistics
    std::atomic<uint64_t> m_scheduled{ 0 };
    std::atomic<uint64_t> m_droppedQueueFull{ 0 };
    std::atomic<uint64_t> m_droppedVoiceCap{ 0 };
    std::atomic<uint32_t> m_activeCount{ 0 };
    std::atomic<uint32_t> m_peakActive{ 0 };
    std::atomic<float> m_limiterGainPublic{ 1.0f };
};

// ----------------------------------------------------------------------------------------------
// Frame clock: maps game frame numbers onto the mixer timeline.
// ----------------------------------------------------------------------------------------------
// The mixer only learns the audio position in whole blocks, so "play at the current position"
// would quantise every sound to a block boundary (~10-20 ms) and pile up everything that happened
// in that window. Instead we anchor one game frame to one timeline sample and derive every other
// frame from it (position = anchor + frameDelta * samplesPerFrame). Relative timing is then exact
// to the sample. `lead` keeps the schedule slightly ahead of the audio position so that, in
// steady state, nothing ever has to be re-anchored. It re-anchors only after a real
// discontinuity: level restart (frame goes backwards), long pause/lag, or running far ahead.
struct FrameClock {
    bool anchored = false;
    int anchorFrame = 0;
    uint64_t anchorSample = 0;

    void reset() { anchored = false; }

    uint64_t sampleFor(int frame, uint64_t now, double samplesPerFrame, uint64_t lead, uint64_t maxAhead) {
        if (!anchored || frame < anchorFrame) reanchor(frame, now + lead);

        auto compute = [&]() {
            double delta = static_cast<double>(frame - anchorFrame) * samplesPerFrame;
            return anchorSample + static_cast<uint64_t>(delta < 0.0 ? 0.0 : delta + 0.5);
        };
        uint64_t pos = compute();

        if (pos + lead < now) { // far behind real time (lag spike / pause): jump forward
            reanchor(frame, now + lead);
            pos = anchorSample;
        }
        else if (pos < now) {   // slightly late: start as soon as possible, keep the anchor
            pos = now;
        }
        else if (pos > now + lead + maxAhead) { // far ahead (fast-forward): compress time
            reanchor(frame, now + lead);
            pos = anchorSample;
        }
        return pos;
    }

private:
    void reanchor(int frame, uint64_t sample) {
        anchored = true;
        anchorFrame = frame;
        anchorSample = sample;
    }
};

// Start positions for a batch of events given their game frames (ascending). Events that share a
// frame are spread evenly across that frame's duration, so 50 inputs in one tick become a rapid
// roll instead of 50 sounds on the same sample (which would just be one very loud click).
inline void computeStarts(FrameClock& clock, const int* frames, size_t n, uint64_t now,
                          double samplesPerFrame, uint64_t lead, uint64_t maxAhead, uint64_t* outStarts) {
    size_t i = 0;
    while (i < n) {
        size_t j = i + 1;
        while (j < n && frames[j] == frames[i]) j++;
        const size_t cnt = j - i;
        const uint64_t base = clock.sampleFor(frames[i], now, samplesPerFrame, lead, maxAhead);
        for (size_t k = 0; k < cnt; k++) {
            outStarts[i + k] = base + static_cast<uint64_t>(static_cast<double>(k) * samplesPerFrame / static_cast<double>(cnt));
        }
        i = j;
    }
}

} // namespace fwcaudio

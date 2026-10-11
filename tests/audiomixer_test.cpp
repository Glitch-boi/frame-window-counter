// Build & run (no game / FMOD needed):
//   g++ -std=c++20 -O2 -Wall -Wextra -pthread -o audiomixer_test tests/audiomixer_test.cpp && ./audiomixer_test
// Race detector:
//   g++ -std=c++20 -O1 -g -fsanitize=thread -pthread -o audiomixer_tsan tests/audiomixer_test.cpp && ./audiomixer_tsan
#include "../src/Audio/AudioMixer.hpp"
#include <chrono>
#include <cstdio>
#include <limits>
#include <random>
#include <thread>

using namespace fwcaudio;

static int g_fails = 0;
static int g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { std::printf("  FAIL line %d: %s\n", __LINE__, #cond); g_fails++; } } while (0)
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// Constant-amplitude clip, `frames` long.
static ClipPtr makeConst(uint32_t frames, float amp, uint32_t rate = 48000) {
    auto c = std::make_shared<Clip>();
    c->frames = frames;
    c->sampleRate = rate;
    c->data.assign(static_cast<size_t>(frames) * 2, amp);
    return c;
}
// Distinct value per frame so misplacement is detectable: L = (i+1)*scale, R = -(i+1)*scale
static ClipPtr makeRamp(uint32_t frames, float scale) {
    auto c = std::make_shared<Clip>();
    c->frames = frames;
    c->sampleRate = 48000;
    c->data.resize(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; i++) {
        c->data[i * 2] = static_cast<float>(i + 1) * scale;
        c->data[i * 2 + 1] = -static_cast<float>(i + 1) * scale;
    }
    return c;
}
static std::vector<float> renderAll(Mixer& m, uint32_t totalFrames, uint32_t block) {
    std::vector<float> out(static_cast<size_t>(totalFrames) * 2);
    for (uint32_t done = 0; done < totalFrames;) {
        uint32_t n = std::min(block, totalFrames - done);
        m.render(out.data() + static_cast<size_t>(done) * 2, n);
        done += n;
    }
    return out;
}

// ------------------------------------------------------------------------------------------------
static void testBuildClip() {
    std::puts("[buildClip] formats, channels, resampling, trimming");
    {   // PCM16 mono -> duplicated to stereo
        int16_t pcm[4] = { 16384, -16384, 32767, 0 };
        auto c = buildClip(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm), PcmFormat::PCM16, 1, 48000, 48000);
        CHECK(c && c->frames == 3); // trailing zero frame trimmed
        CHECK(c && near(c->data[0], 0.5) && near(c->data[1], 0.5));
        CHECK(c && near(c->data[2], -0.5) && near(c->data[3], -0.5));
        CHECK(c && near(c->data[4], 32767.0 / 32768.0, 1e-5));
    }
    {   // PCM16 stereo keeps channels apart
        int16_t pcm[4] = { 16384, -8192, 4096, 8192 };
        auto c = buildClip(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm), PcmFormat::PCM16, 2, 48000, 48000);
        CHECK(c && c->frames == 2);
        CHECK(c && near(c->data[0], 0.5) && near(c->data[1], -0.25));
        CHECK(c && near(c->data[2], 0.125) && near(c->data[3], 0.25));
    }
    {   // 8 bit signed
        int8_t pcm[2] = { 64, -128 };
        auto c = buildClip(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm), PcmFormat::PCM8, 1, 48000, 48000);
        CHECK(c && c->frames == 2 && near(c->data[0], 0.5) && near(c->data[2], -1.0));
    }
    {   // 24 bit with sign extension: 0x400000 = +0.5, 0xC00000 = -0.5
        uint8_t pcm[6] = { 0x00, 0x00, 0x40, 0x00, 0x00, 0xC0 };
        auto c = buildClip(pcm, sizeof(pcm), PcmFormat::PCM24, 1, 48000, 48000);
        CHECK(c && c->frames == 2 && near(c->data[0], 0.5) && near(c->data[2], -0.5));
    }
    {   // 32 bit + float, NaN/inf in float data must become silence rather than poisoning the mix
        int32_t pcm32[1] = { 1 << 30 };
        auto c = buildClip(reinterpret_cast<const uint8_t*>(pcm32), sizeof(pcm32), PcmFormat::PCM32, 1, 48000, 48000);
        CHECK(c && near(c->data[0], 0.5));
        float f[3] = { 0.25f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        auto c2 = buildClip(reinterpret_cast<const uint8_t*>(f), sizeof(f), PcmFormat::Float, 1, 48000, 48000);
        CHECK(c2 && c2->frames == 1 && near(c2->data[0], 0.25)); // NaN/inf frames are silence -> trimmed
    }
    {   // more than 2 channels: first two are used
        int16_t pcm[3] = { 16384, 8192, 30000 };
        auto c = buildClip(reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm), PcmFormat::PCM16, 3, 48000, 48000);
        CHECK(c && c->frames == 1 && near(c->data[0], 0.5) && near(c->data[1], 0.25));
    }
    {   // upsample 24000 -> 48000: twice the frames, linear ramp interpolated
        float f[4] = { 0.0f, 0.2f, 0.4f, 0.6f };
        auto c = buildClip(reinterpret_cast<const uint8_t*>(f), sizeof(f), PcmFormat::Float, 1, 24000, 48000);
        CHECK(c && c->frames == 8);
        CHECK(c && near(c->data[2 * 1], 0.1, 1e-5)); // halfway between 0.0 and 0.2
        CHECK(c && near(c->data[2 * 2], 0.2, 1e-5));
    }
    {   // 44100 -> 48000 keeps duration (frames scale by the ratio)
        std::vector<float> f(44100, 0.3f);
        auto c = buildClip(reinterpret_cast<const uint8_t*>(f.data()), f.size() * 4, PcmFormat::Float, 1, 44100, 48000);
        CHECK(c && c->frames == 48000);
    }
    {   // trailing silence trimmed, leading silence preserved
        float f[6] = { 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f };
        auto c = buildClip(reinterpret_cast<const uint8_t*>(f), sizeof(f), PcmFormat::Float, 1, 48000, 48000);
        CHECK(c && c->frames == 3 && near(c->data[0], 0.0) && near(c->data[4], 0.5));
    }
    {   // invalid / silent input
        float z[4] = { 0, 0, 0, 0 };
        CHECK(!buildClip(reinterpret_cast<const uint8_t*>(z), sizeof(z), PcmFormat::Float, 1, 48000, 48000));
        CHECK(!buildClip(nullptr, 8, PcmFormat::Float, 1, 48000, 48000));
        CHECK(!buildClip(reinterpret_cast<const uint8_t*>(z), 0, PcmFormat::Float, 1, 48000, 48000));
        CHECK(!buildClip(reinterpret_cast<const uint8_t*>(z), sizeof(z), PcmFormat::Float, 0, 48000, 48000));
        CHECK(!buildClip(reinterpret_cast<const uint8_t*>(z), sizeof(z), PcmFormat::Float, 1, 0, 48000));
        CHECK(!buildClip(reinterpret_cast<const uint8_t*>(z), 3, PcmFormat::Float, 1, 48000, 48000)); // < 1 frame
    }
}

// ------------------------------------------------------------------------------------------------
static void testPlacementAndSum() {
    std::puts("[mixer] exact placement, sum instead of overwrite, late/future voices, gain");
    {   // exact sample placement across block boundaries
        Mixer m(48000);
        m.schedule(makeRamp(100, 1e-4f), 1000);
        auto out = renderAll(m, 2048, 512); // 4 blocks; voice straddles blocks 1 and 2
        for (uint32_t f = 0; f < 2048; f++) {
            float expect = (f >= 1000 && f < 1100) ? static_cast<float>(f - 1000 + 1) * 1e-4f : 0.0f;
            if (!near(out[f * 2], expect, 1e-7) || !near(out[f * 2 + 1], -expect, 1e-7)) {
                CHECK(false);
                std::printf("    frame %u expected %g got %g\n", f, expect, out[f * 2]);
                break;
            }
        }
        CHECK(m.position() == 2048);
    }
    {   // overlapping identical voices SUM (the bug being fixed: they used to cut each other off)
        Mixer m(48000);
        for (int i = 0; i < 10; i++) m.schedule(makeConst(200, 0.01f), 100);
        auto out = renderAll(m, 512, 256);
        CHECK(near(out[(100 + 50) * 2], 0.10, 1e-6));   // 10 x 0.01
        CHECK(near(out[(99) * 2], 0.0) && near(out[(300) * 2], 0.0));
        CHECK(m.stats().droppedQueueFull == 0 && m.stats().droppedVoiceCap == 0);
    }
    {   // a voice scheduled in the past starts immediately, it is NOT skipped
        Mixer m(48000);
        renderAll(m, 1024, 512);
        m.schedule(makeConst(50, 0.2f), 10); // 10 is long gone
        std::vector<float> out(512 * 2);
        m.render(out.data(), 512);
        CHECK(near(out[0], 0.2) && near(out[49 * 2], 0.2) && near(out[50 * 2], 0.0));
    }
    {   // a voice in the future waits
        Mixer m(48000);
        m.schedule(makeConst(10, 0.3f), 5000);
        auto out = renderAll(m, 4096, 1024);
        for (float v : out) { if (v != 0.0f) { CHECK(false); break; } }
        std::vector<float> o2(2048 * 2);
        m.render(o2.data(), 2048);
        CHECK(near(o2[(5000 - 4096) * 2], 0.3) && near(o2[(5010 - 4096) * 2], 0.0));
    }
    {   // per-voice gain
        Mixer m(48000);
        m.schedule(makeConst(10, 0.4f), 0, 0.5f);
        auto out = renderAll(m, 64, 64);
        CHECK(near(out[0], 0.2, 1e-6));
    }
    {   // bad input is rejected, not crashed on
        Mixer m(48000);
        CHECK(!m.schedule(nullptr, 0));
        auto empty = std::make_shared<Clip>();
        CHECK(!m.schedule(empty, 0));
        float out[8];
        m.render(out, 0);      // zero frames: no-op
        m.render(nullptr, 4);  // null buffer: no-op
        CHECK(true);
    }
}

// ------------------------------------------------------------------------------------------------
// THE scenario from the bug report: 12,000+ sounds per second must all be heard.
static void testStress12k() {
    std::puts("[stress] 12,000 sounds/second x 5 s, verified against an independent reference mix");
    const uint32_t rate = 48000;
    const uint32_t seconds = 5;
    const uint32_t total = rate * seconds;
    const uint32_t perSecond = 12000;
    const uint32_t clipLen = 1440; // 30 ms click
    const float amp = 1e-5f;      // tiny, so even hundreds of overlaps stay far below the limiter

    auto clip = makeRamp(clipLen, amp / clipLen);
    std::mt19937 rng(12345);

    std::vector<uint64_t> starts;
    for (uint32_t i = 0; i < perSecond * seconds; i++) starts.push_back(rng() % (total - clipLen));
    std::sort(starts.begin(), starts.end());

    // independent reference: plain accumulation into doubles
    std::vector<double> ref(static_cast<size_t>(total) * 2, 0.0);
    for (uint64_t s : starts)
        for (uint32_t i = 0; i < clipLen; i++) {
            ref[(s + i) * 2] += clip->data[i * 2];
            ref[(s + i) * 2 + 1] += clip->data[i * 2 + 1];
        }

    Mixer m(rate); // default caps - proves they are never reached at this density
    std::vector<float> out(static_cast<size_t>(total) * 2);
    size_t next = 0;
    const uint32_t block = 512;
    const uint32_t ahead = rate / 5; // schedule up to 200 ms ahead, like the game does
    for (uint32_t done = 0; done < total; done += block) {
        uint32_t n = std::min(block, total - done);
        while (next < starts.size() && starts[next] < static_cast<uint64_t>(done) + n + ahead) { m.schedule(clip, starts[next]); next++; }
        m.render(out.data() + static_cast<size_t>(done) * 2, n);
    }

    double maxErr = 0.0;
    for (size_t i = 0; i < out.size(); i++) maxErr = std::max(maxErr, std::fabs(static_cast<double>(out[i]) - ref[i]));
    auto st = m.stats();
    std::printf("    voices scheduled=%llu  dropped(queue)=%llu dropped(cap)=%llu  peak simultaneous=%u  max |error| vs reference=%.3g\n",
        (unsigned long long)st.scheduled, (unsigned long long)st.droppedQueueFull, (unsigned long long)st.droppedVoiceCap,
        st.peakActiveVoices, maxErr);
    CHECK(st.scheduled == perSecond * seconds);
    CHECK(st.droppedQueueFull == 0 && st.droppedVoiceCap == 0);
    CHECK(maxErr < 1e-8);                   // every single voice is present, sample for sample
    CHECK(st.limiterGain == 1.0f);          // limiter stayed out of the way at this level
    CHECK(st.peakActiveVoices > 300);       // it genuinely was a dense overlap
}

// ------------------------------------------------------------------------------------------------
static void testLimiter() {
    std::puts("[limiter] never exceeds 1.0, silent when idle, recovers after a burst");
    {   // quiet signal: bit-exact pass-through
        Mixer m(48000);
        auto clip = makeRamp(300, 1e-3f);
        m.schedule(clip, 0);
        auto out = renderAll(m, 512, 512);
        bool exact = true;
        for (uint32_t i = 0; i < 300 * 2; i++) if (out[i] != clip->data[i]) exact = false;
        CHECK(exact);
        CHECK(m.stats().limiterGain == 1.0f);
    }
    {   // 2000 full-scale voices at the same instant
        Mixer m(48000);
        auto loud = makeConst(2000, 0.5f);
        for (int i = 0; i < 2000; i++) m.schedule(loud, 1000);
        auto out = renderAll(m, 8192, 512);
        float peak = 0; bool finite = true;
        for (float v : out) { peak = std::max(peak, std::fabs(v)); if (!std::isfinite(v)) finite = false; }
        std::printf("    peak after 2000x overload = %.4f, limiter gain now %.6f\n", peak, m.stats().limiterGain);
        CHECK(finite);
        CHECK(peak <= 1.0f);
        CHECK(peak > 0.5f);   // loud but not silenced
    }
    {   // after the burst is over the level comes back, and later sounds are unaltered
        Mixer m(48000);
        auto loud = makeConst(500, 0.5f);
        for (int i = 0; i < 500; i++) m.schedule(loud, 0);
        renderAll(m, 4096, 512);
        CHECK(m.stats().limiterGain < 0.5f);                  // it did engage
        renderAll(m, 48000 * 2, 512);                         // 2 s of silence
        CHECK(m.stats().limiterGain == 1.0f);                 // fully recovered
        auto probe = makeRamp(100, 1e-3f);
        m.schedule(probe, m.position() + 10);
        auto out = renderAll(m, 512, 512);
        CHECK(near(out[(10 + 99) * 2], probe->data[99 * 2], 1e-9));
    }
    {   // NaN can never reach the output
        Mixer m(48000);
        auto bad = std::make_shared<Clip>();
        bad->frames = 4; bad->sampleRate = 48000;
        bad->data = { 0.1f, 0.1f, std::numeric_limits<float>::quiet_NaN(), 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
        m.schedule(bad, 0);
        auto out = renderAll(m, 64, 64);
        bool finite = true; for (float v : out) if (!std::isfinite(v)) finite = false;
        CHECK(finite);
    }
}

// ------------------------------------------------------------------------------------------------
static void testClearAndCaps() {
    std::puts("[mixer] clear semantics, voice cap, queue-full accounting");
    {   // clear kills everything scheduled BEFORE it, keeps everything scheduled AFTER it
        Mixer m(48000);
        m.schedule(makeConst(1000, 0.1f), 0);       // before the clear, still queued
        m.requestClear();
        m.schedule(makeConst(1000, 0.2f), 0);       // after the clear, but audio thread hasn't run yet
        auto out = renderAll(m, 512, 512);
        CHECK(near(out[10 * 2], 0.2, 1e-6));        // only the second one
    }
    {   // clear also removes voices that are already playing / pending in the active list
        Mixer m(48000);
        m.schedule(makeConst(100000, 0.1f), 0);
        m.schedule(makeConst(10, 0.1f), 1000000);   // far future
        renderAll(m, 512, 512);
        m.requestClear();
        auto out = renderAll(m, 512, 512);
        bool silent = true; for (float v : out) if (v != 0.0f) silent = false;
        CHECK(silent);
        CHECK(m.stats().activeVoices == 0);
    }
    {   // the safety cap is a hard limit on SIMULTANEOUS voices and is accounted for
        Mixer m(48000, 1 << 12, /*maxActive*/ 100);
        for (int i = 0; i < 300; i++) m.schedule(makeConst(5000, 0.0001f), 0);
        renderAll(m, 256, 256);
        auto st = m.stats();
        CHECK(st.activeVoices == 100);
        CHECK(st.droppedVoiceCap == 200);
    }
    {   // queue full: schedule() reports false and counts it; nothing corrupts
        Mixer m(48000, 64, 1000);
        int accepted = 0;
        for (int i = 0; i < 100; i++) if (m.schedule(makeConst(10, 0.001f), 0)) accepted++;
        CHECK(accepted == 64);
        CHECK(m.stats().droppedQueueFull == 36);
        auto out = renderAll(m, 64, 64);
        CHECK(near(out[0], 64 * 0.001, 1e-6));
        CHECK(m.schedule(makeConst(10, 0.001f), 0)); // room again after the audio thread drained it
    }
}

// ------------------------------------------------------------------------------------------------
static void testFrameClock() {
    std::puts("[frameClock] steady state, in-frame spread, restart, pause, fast-forward");
    const double rate = 48000.0;
    const double spf = rate / 240.0;          // 200 samples per tick
    const uint32_t chunk = 512;

    {   // Simulate the real pipeline: game ticks at 240 Hz, audio "now" advances as a coarse staircase.
        FrameClock clk;
        uint64_t now = 100000;                // arbitrary start
        uint64_t lead = chunk, maxAhead = 12000;
        int reanchors = 0; uint64_t lastAnchor = 0; bool first = true;
        uint64_t prev = 0; bool havePrev = false; int badSpacing = 0, behindNow = 0;
        double t = 0;                         // seconds of game time
        std::mt19937 rng(7);
        for (int tick = 1000; tick < 1000 + 240 * 10; tick++) {   // 10 seconds
            t = (tick - 1000) / 240.0;
            // audio now: staircase of 512-blocks that runs a random 0..chunk ahead of the ideal clock
            uint64_t ideal = 100000 + static_cast<uint64_t>(t * rate);
            uint64_t stairs = (ideal / chunk) * chunk + (rng() % 2 ? chunk : 0);
            now = std::max(now, stairs);
            uint64_t s = clk.sampleFor(tick, now, spf, lead, maxAhead);
            if (first || clk.anchorSample != lastAnchor) { reanchors++; lastAnchor = clk.anchorSample; first = false; }
            if (s < now) behindNow++;
            if (havePrev && s - prev != 200 && s - prev != 199 && s - prev != 201) badSpacing++;
            prev = s; havePrev = true;
        }
        std::printf("    re-anchors in 10 s of steady play: %d, ticks scheduled behind 'now': %d, irregular spacings: %d\n", reanchors, behindNow, badSpacing);
        CHECK(reanchors <= 3);     // anchored once (+ at most a couple of corrections)
        CHECK(badSpacing <= 3);    // consecutive ticks are exactly one tick (200 samples) apart
    }
    {   // inputs inside one tick are spread across the tick
        FrameClock clk;
        std::vector<int> frames(50, 777);
        std::vector<uint64_t> starts(50);
        computeStarts(clk, frames.data(), frames.size(), 5000, spf, 512, 12000, starts.data());
        bool increasing = true;
        for (size_t i = 1; i < starts.size(); i++) if (starts[i] <= starts[i - 1]) increasing = false;
        CHECK(increasing);
        CHECK(starts[0] == 5000 + 512);
        CHECK(starts[49] - starts[0] == static_cast<uint64_t>(49.0 * spf / 50.0)); // last one still inside the tick
        CHECK(starts[49] - starts[0] < static_cast<uint64_t>(spf));
    }
    {   // batch with several frames: each group anchored relative to the first
        FrameClock clk;
        int frames[] = { 10, 10, 11, 13 };
        uint64_t st[4];
        computeStarts(clk, frames, 4, 0, spf, 0, 100000, st);
        CHECK(st[2] - st[0] == 200);
        CHECK(st[3] - st[0] == 600);
        CHECK(st[1] - st[0] == 100);
    }
    {   // restart (frame goes backwards) re-anchors
        FrameClock clk;
        uint64_t a = clk.sampleFor(5000, 1000, spf, 512, 12000);
        uint64_t b = clk.sampleFor(10, 3000, spf, 512, 12000);
        CHECK(b >= 3000 + 512 && b > a);
    }
    {   // long pause (10 s of wall time, game frame barely moved) re-anchors instead of dumping into the past
        FrameClock clk;
        clk.sampleFor(100, 1000, spf, 512, 12000);
        uint64_t b = clk.sampleFor(101, 1000 + 480000, spf, 512, 12000);
        CHECK(b >= 1000 + 480000);
    }
    {   // fast-forward (game way ahead of real time) is compressed, never scheduled seconds into the future
        FrameClock clk;
        clk.sampleFor(0, 0, spf, 512, 12000);
        uint64_t b = clk.sampleFor(24000, 100, spf, 512, 12000); // 100 s of game time at once
        CHECK(b <= 100 + 512 + 12000);
    }
}

// ------------------------------------------------------------------------------------------------
static void testThreads() {
    std::puts("[threads] game thread schedules while audio thread renders (run under TSan too)");
    const uint32_t rate = 48000;
    Mixer m(rate, 1 << 14, 100000);
    auto clip = makeConst(64, 1e-7f);          // sum of one voice = 64 frames * 2 ch * 1e-7
    const int totalVoices = 200000;
    std::atomic<bool> done{ false };

    std::thread audio([&] {
        std::vector<float> buf(256 * 2);
        double sum = 0;
        while (!done.load(std::memory_order_acquire)) {
            m.render(buf.data(), 256);
        }
        // final drain: render until nothing is left
        for (int i = 0; i < 2000; i++) m.render(buf.data(), 256);
        (void)sum;
    });

    int accepted = 0;
    std::thread game([&] {
        for (int i = 0; i < totalVoices; i++) {
            if (m.schedule(clip, m.position() + (i % 977))) accepted++;
            else std::this_thread::yield();   // queue momentarily full: normal back-pressure
        }
    });
    game.join();
    done.store(true, std::memory_order_release);
    audio.join();

    auto st = m.stats();
    std::printf("    accepted=%d scheduled(stat)=%llu droppedQueue=%llu droppedCap=%llu\n", accepted,
        (unsigned long long)st.scheduled, (unsigned long long)st.droppedQueueFull, (unsigned long long)st.droppedVoiceCap);
    CHECK(static_cast<uint64_t>(accepted) == st.scheduled);
    CHECK(st.droppedVoiceCap == 0);
    CHECK(st.activeVoices == 0); // everything that was accepted was eventually played and finished
}


// ------------------------------------------------------------------------------------------------
// End-to-end: macro ticks -> per-tick batch -> FrameClock/computeStarts -> Mixer, with a simulated FMOD
// stream thread that renders in coarse chunks AHEAD of real time (like a real stream buffer does).
static void testPipeline() {
    std::puts("[pipeline] 240 TPS x 50 inputs/tick (= 12,000/s) through clock + mixer + simulated stream thread");
    const uint32_t rate = 48000;
    const double spf = rate / 240.0;
    const uint32_t chunk = 512;
    const int seconds = 5, tps = 240, perTick = 50;

    // one-sample impulses: every voice is one audible sample, so onsets are individually countable
    auto impulse = std::make_shared<Clip>();
    impulse->frames = 1; impulse->sampleRate = rate; impulse->data = { 1e-4f, 1e-4f };

    Mixer m(rate);
    FrameClock clock;
    std::vector<float> out;                 // everything the "stream" produced
    std::vector<uint64_t> allStarts;
    std::mt19937 rng(99);
    uint64_t produced = 0;
    std::vector<float> block(chunk * 2);
    auto produceUntil = [&](uint64_t target) {
        while (produced < target) { m.render(block.data(), chunk); out.insert(out.end(), block.begin(), block.end()); produced += chunk; }
    };

    produceUntil(4096); // initial prebuffer, as a real stream has
    int tickCount = seconds * tps;
    std::vector<int> frames(perTick);
    std::vector<uint64_t> starts(perTick);
    int latencyViolations = 0;
    for (int tick = 0; tick < tickCount; tick++) {
        const uint64_t realTime = static_cast<uint64_t>(tick * spf);                       // where the speakers are
        produceUntil(realTime + 4096 + (rng() % (2 * chunk)));                            // stream thread keeps a jittery lead
        for (int k = 0; k < perTick; k++) frames[k] = 1000 + tick;                         // all 50 inputs share the tick's frame
        uint64_t now = m.position();
        computeStarts(clock, frames.data(), perTick, now, spf, chunk, rate / 5, starts.data());
        for (int k = 0; k < perTick; k++) { m.schedule(impulse, starts[k]); allStarts.push_back(starts[k]); }
        // sound must not be scheduled in the past of what was already rendered, nor absurdly far ahead
        if (starts[0] + chunk < now) latencyViolations++;
        if (starts[perTick - 1] > now + chunk + rate / 5 + 400) latencyViolations++;
    }
    produceUntil(allStarts.back() + 4 * chunk);

    size_t audible = 0, wrongLevel = 0;
    for (size_t f = 0; f < out.size() / 2; f++) {
        if (out[f * 2] != 0.0f) { audible++; if (std::fabs(out[f * 2] - 1e-4f) > 1e-9f) wrongLevel++; }
    }
    // every onset must be on a DISTINCT sample (inputs inside a tick are spread, not stacked)
    std::sort(allStarts.begin(), allStarts.end());
    size_t duplicates = 0;
    for (size_t i = 1; i < allStarts.size(); i++) if (allStarts[i] == allStarts[i - 1]) duplicates++;

    auto st = m.stats();
    std::printf("    sounds=%zu  audible onsets=%zu  stacked-on-same-sample=%zu  dropped=%llu  latency violations=%d  re-anchors: clock anchored at frame %d\n",
        allStarts.size(), audible, duplicates, (unsigned long long)(st.droppedQueueFull + st.droppedVoiceCap), latencyViolations, clock.anchorFrame);
    CHECK(allStarts.size() == static_cast<size_t>(seconds * 12000));
    CHECK(audible == allStarts.size() - duplicates);   // every scheduled sound is heard
    CHECK(wrongLevel == 0);
    CHECK(duplicates == 0);
    CHECK(st.droppedQueueFull == 0 && st.droppedVoiceCap == 0);
    CHECK(latencyViolations == 0);
}

// ------------------------------------------------------------------------------------------------
static void benchmark() {
    std::puts("[benchmark] cost of mixing 12,000 sounds/second");
    for (uint32_t clipMs : { 30u, 100u, 300u }) {
        const uint32_t rate = 48000, total = rate * 10, block = 512;
        const uint32_t clipLen = rate * clipMs / 1000;
        auto clip = makeConst(clipLen, 1e-6f);
        Mixer m(rate, 1 << 16, 16384);
        std::vector<float> out(static_cast<size_t>(block) * 2);
        std::mt19937 rng(1);
        uint64_t nextStart = 0;
        const double voiceGap = rate / 12000.0;
        double clk = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (uint32_t done = 0; done < total; done += block) {
            while (static_cast<double>(nextStart) < done + block + rate / 10) {
                m.schedule(clip, nextStart);
                clk += voiceGap; nextStart = static_cast<uint64_t>(clk);
            }
            m.render(out.data(), block);
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        auto st = m.stats();
        std::printf("    %3u ms clips: 10 s of audio in %.3f s  => %.1f%% of ONE core, peak %u simultaneous voices, dropped %llu\n",
            clipMs, secs, secs / 10.0 * 100.0, st.peakActiveVoices, (unsigned long long)(st.droppedQueueFull + st.droppedVoiceCap));
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
        // sanitizer builds are 3-10x slower; wall-clock assertions are meaningless there
#else
        CHECK(secs < 10.0 * 0.5); // even the worst case must comfortably beat real time (very generous bound)
#endif
        (void)rng;
    }
}

int main() {
    testBuildClip();
    testPlacementAndSum();
    testStress12k();
    testLimiter();
    testClearAndCaps();
    testFrameClock();
    testThreads();
    testPipeline();
    benchmark();
    std::printf("\n%d checks, %d failures -> %s\n", g_checks, g_fails, g_fails ? "FAILED" : "ALL PASSED");
    return g_fails ? 1 : 0;
}

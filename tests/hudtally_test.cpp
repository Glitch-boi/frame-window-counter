// g++ -std=c++20 -O2 -Wall -Wextra -o hudtally_test tests/hudtally_test.cpp && ./hudtally_test
// Proves the optimised preset matching / recounting is IDENTICAL to the original naive loops.
#include "../src/Math/HudTally.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>

struct Act { int frame; bool shouldDraw; double frameWindow; int ifCount; bool isPlayer2; };
struct Preset { int id; bool useIF; double minVal; double maxVal; bool showInHud; std::string audioPath; };
using PresetMap = std::map<std::string, Preset>;

static int g_fails = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { std::printf("  FAIL line %d: %s\n", __LINE__, #cond); g_fails++; } } while (0)

// ---- verbatim copies of the ORIGINAL algorithms ------------------------------------------------
static void naiveTally(const std::vector<Act>& acts, const PresetMap& presets, int upTo, std::map<int, int>& counts) {
    for (const auto& action : acts) {
        if (action.shouldDraw && action.frame <= upTo) {
            double fw = action.frameWindow;
            double ifVal = static_cast<double>(action.ifCount);
            for (const auto& [idStr, preset] : presets) {
                double targetVal = preset.useIF ? ifVal : fw;
                if (targetVal >= preset.minVal && targetVal <= preset.maxVal) counts[preset.id]++;
            }
        }
    }
}
struct Trigger { std::string path; int frame; bool operator==(const Trigger&) const = default; };
struct TickResult { std::map<int, int> counts; std::vector<Trigger> sounds; std::vector<int> animated; };

// original per-tick loop
static TickResult naiveTick(const std::vector<Act>& acts, PresetMap& presets, int lastFrame, int currentFrame) {
    TickResult r;
    std::vector<int> updated;
    auto it = std::upper_bound(acts.begin(), acts.end(), lastFrame, [](int f, const Act& a) { return f < a.frame; });
    while (it != acts.end() && it->frame <= currentFrame) {
        auto& action = *it;
        if (action.shouldDraw) {
            double fw = action.frameWindow; int ifVal = action.ifCount;
            for (auto& [idStr, preset] : presets) {
                double targetVal = preset.useIF ? static_cast<double>(ifVal) : fw;
                if (targetVal >= preset.minVal && targetVal <= preset.maxVal) {
                    if (!preset.audioPath.empty() && preset.showInHud) r.sounds.push_back({ preset.audioPath, action.frame });
                    r.counts[preset.id]++;
                    if (preset.showInHud) updated.push_back(preset.id);
                }
            }
        }
        ++it;
    }
    std::sort(updated.begin(), updated.end());
    updated.erase(std::unique(updated.begin(), updated.end()), updated.end());
    r.animated = updated;
    return r;
}
// the new per-tick loop (same structure as PlayLayerHook.cpp)
static TickResult newTick(const std::vector<Act>& acts, PresetMap& presets, int lastFrame, int currentFrame) {
    TickResult r;
    std::vector<int> updated;
    hudtally::MatchCache<PresetMap> matcher(presets);
    auto it = std::upper_bound(acts.begin(), acts.end(), lastFrame, [](int f, const Act& a) { return f < a.frame; });
    while (it != acts.end() && it->frame <= currentFrame) {
        auto& action = *it;
        if (action.shouldDraw) {
            auto& kh = matcher.hitsFor(action.frameWindow, action.ifCount);
            kh.actionCount++;
            for (Preset* p : kh.hits)
                if (!p->audioPath.empty() && p->showInHud) r.sounds.push_back({ p->audioPath, action.frame });
        }
        ++it;
    }
    for (auto& kh : matcher.entries())
        for (Preset* p : kh.hits) {
            r.counts[p->id] += kh.actionCount;
            if (p->showInHud) updated.push_back(p->id);
        }
    std::sort(updated.begin(), updated.end());
    updated.erase(std::unique(updated.begin(), updated.end()), updated.end());
    r.animated = updated;
    return r;
}

static PresetMap randomPresets(std::mt19937& rng, int n) {
    PresetMap m;
    for (int i = 0; i < n; i++) {
        Preset p;
        p.id = i;
        p.useIF = rng() % 4 == 0;
        double a = (rng() % 40) / 2.0, b = (rng() % 40) / 2.0;     // multiples of 0.5 => exact boundary hits
        p.minVal = std::min(a, b); p.maxVal = std::max(a, b);
        p.showInHud = rng() % 3 != 0;
        p.audioPath = (rng() % 2) ? "click" + std::to_string(i % 5) + ".ogg" : "";
        m[std::to_string(i)] = p;
    }
    return m;
}
static std::vector<Act> randomActions(std::mt19937& rng, int n, bool withNaN) {
    std::vector<Act> v;
    int frame = 0;
    for (int i = 0; i < n; i++) {
        if (rng() % 3 != 0) frame += 1 + rng() % 3;               // many actions share a frame (high-CPS macro)
        Act a;
        a.frame = frame;
        a.shouldDraw = rng() % 8 != 0;
        a.frameWindow = (rng() % 40) / 2.0;
        if (withNaN && rng() % 15 == 0) a.frameWindow = std::numeric_limits<double>::quiet_NaN();
        a.ifCount = 1 + rng() % 5;
        a.isPlayer2 = rng() % 2;
        v.push_back(a);
    }
    return v;                                                      // already ascending by frame
}

int main() {
    std::puts("[recount] tallyUpTo == naive loop (random scenarios, boundary values, NaN windows)");
    {
        std::mt19937 rng(2024);
        int scenarios = 0;
        for (int round = 0; round < 400; round++) {
            auto presets = randomPresets(rng, 1 + rng() % 100);
            auto acts = randomActions(rng, rng() % 600, round % 3 == 0);
            int upTo = (acts.empty() ? 0 : acts.back().frame) + (static_cast<int>(rng() % 10) - 5);
            std::map<int, int> a, b;
            naiveTally(acts, presets, upTo, a);
            hudtally::tallyUpTo(acts, presets, upTo, b);
            CHECK(a == b);
            scenarios++;
        }
        std::printf("    %d scenarios identical\n", scenarios);
    }
    std::puts("[per tick] MatchCache loop == naive loop: counts, animated presets AND the exact order of sound triggers");
    {
        std::mt19937 rng(77);
        int ticks = 0, triggers = 0;
        for (int round = 0; round < 200; round++) {
            auto presets = randomPresets(rng, 1 + rng() % 60);
            auto acts = randomActions(rng, 300 + rng() % 500, round % 4 == 0);
            int last = -1;
            const int maxFrame = acts.empty() ? 0 : acts.back().frame;
            for (int cur = 0; cur <= maxFrame + 2; cur += 1 + (rng() % 3 == 0 ? static_cast<int>(rng() % 4) : 0)) {
                auto x = naiveTick(acts, presets, last, cur);
                auto y = newTick(acts, presets, last, cur);
                CHECK(x.counts == y.counts);
                CHECK(x.animated == y.animated);
                CHECK(x.sounds == y.sounds);
                triggers += static_cast<int>(x.sounds.size());
                last = cur; ticks++;
            }
        }
        std::printf("    %d ticks, %d sound triggers - identical order\n", ticks, triggers);
    }
    std::puts("[edge] empty inputs, equal min/max boundary, NaN never matches");
    {
        PresetMap none;
        std::map<int, int> c;
        hudtally::tallyUpTo(std::vector<Act>{ { 1, true, 1.0, 1, false } }, none, 100, c);
        CHECK(c.empty());
        PresetMap one; one["0"] = { 0, false, 1.0, 1.0, true, "" };
        std::vector<Act> acts = { { 5, true, 1.0, 1, false }, { 5, true, 1.0000001, 1, false }, { 6, true, std::numeric_limits<double>::quiet_NaN(), 1, false }, { 7, false, 1.0, 1, false } };
        std::map<int, int> c2;
        hudtally::tallyUpTo(acts, one, 100, c2);
        CHECK(c2[0] == 1);                    // only the exact 1.0 that is drawable
        std::map<int, int> c3;
        hudtally::tallyUpTo(acts, one, 4, c3);
        CHECK(c3.empty());                    // frames after upToFrame are excluded
    }
    std::puts("[benchmark] respawn recount: 720,000 actions (60 s at 12,000 inputs/s) x 100 presets");
    {
        std::mt19937 rng(5);
        auto presets = randomPresets(rng, 100);
        std::vector<Act> acts;
        for (int i = 0; i < 720000; i++) acts.push_back({ i / 50, true, 1.0 + (i % 3), 1, false });
        std::map<int, int> a, b;
        auto t0 = std::chrono::steady_clock::now();
        naiveTally(acts, presets, 1 << 30, a);
        auto t1 = std::chrono::steady_clock::now();
        hudtally::tallyUpTo(acts, presets, 1 << 30, b);
        auto t2 = std::chrono::steady_clock::now();
        double naive = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double fast = std::chrono::duration<double, std::milli>(t2 - t1).count();
        std::printf("    naive %.1f ms   ->   new %.1f ms   (%.0fx faster), results identical: %s\n", naive, fast, naive / std::max(fast, 0.001), a == b ? "yes" : "NO");
        CHECK(a == b);
    }
    std::printf("\n%d checks, %d failures -> %s\n", g_checks, g_fails, g_fails ? "FAILED" : "ALL PASSED");
    return g_fails ? 1 : 0;
}

#pragma once
// Geode-free helpers that decide which label presets an action hits and how often.
// Extracted from PlayLayerHook.cpp so the optimised versions can be tested against the naive loops
// (tests/hudtally_test.cpp). Behaviour must stay IDENTICAL to the original per-action x per-preset scan.
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace hudtally {

// Does `preset` match an action with window `fw` and I/F count `ifVal`?
template <class Preset>
inline bool presetMatches(const Preset& p, double fw, int ifVal) {
    const double target = p.useIF ? static_cast<double>(ifVal) : fw;
    return target >= p.minVal && target <= p.maxVal;
}

// Adds, for every drawable action with frame <= upToFrame, one hit to each preset it matches.
// (The caller clears `counts` first if it wants a fresh tally.)
// Actions are grouped by (window, I/F) and the presets are scanned once per distinct pair:
// O(actions * log(pairs)) instead of O(actions * presets). Keys use the BIT PATTERN of the window value
// so a NaN can never break the map's ordering (undefined behaviour with a double key).
template <class Counts, class ActionVec, class PresetMap>
inline void tallyUpTo(const ActionVec& actions, const PresetMap& presets, int upToFrame, Counts& counts) {
    std::map<std::pair<uint64_t, int>, int> tally;
    for (const auto& action : actions) {
        if (action.shouldDraw && action.frame <= upToFrame) {
            uint64_t bits;
            std::memcpy(&bits, &action.frameWindow, sizeof(bits));
            tally[{ bits, action.ifCount }]++;
        }
    }
    for (const auto& [key, count] : tally) {
        double fw;
        std::memcpy(&fw, &key.first, sizeof(fw));
        for (const auto& [idStr, preset] : presets) {
            if (presetMatches(preset, fw, key.second)) {
                counts[preset.id] += count;
            }
        }
    }
}

template <class Preset>
struct KeyHits {
    double fw;
    int ifVal;
    std::vector<Preset*> hits;   // presets that match this (window, I/F)
    int actionCount = 0;         // how many actions with this (window, I/F) were seen
};

// Used inside ONE physics step: actions with the same (window, I/F) match exactly the same presets,
// so the preset list is scanned once per distinct pair and the hit counts are applied afterwards.
template <class PresetMap>
class MatchCache {
public:
    using Preset = typename PresetMap::mapped_type;

    explicit MatchCache(PresetMap& presets) : m_presets(presets) {}

    // The returned reference is valid until the next hitsFor() call.
    KeyHits<Preset>& hitsFor(double fw, int ifVal) {
        for (auto& k : m_entries) {
            if (k.fw == fw && k.ifVal == ifVal) return k;
        }
        m_entries.push_back(KeyHits<Preset>{ fw, ifVal, {}, 0 });
        auto& k = m_entries.back();
        for (auto& [idStr, preset] : m_presets) {
            if (presetMatches(preset, fw, ifVal)) k.hits.push_back(&preset);
        }
        return k;
    }

    std::vector<KeyHits<Preset>>& entries() { return m_entries; }

private:
    PresetMap& m_presets;
    std::vector<KeyHits<Preset>> m_entries;
};

} // namespace hudtally

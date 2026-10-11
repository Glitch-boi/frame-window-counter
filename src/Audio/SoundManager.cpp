#include "SoundManager.hpp"
#include "AudioMixer.hpp"
#include "../Data/State.hpp"
#include <Geode/fmod/fmod.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace {
    using Clock = std::chrono::steady_clock;

    // 仅作为安全网（防止病态输入吃光 CPU / 内存），不是速率限制：
    // 12000 音效/秒、每个 300ms，同时存在的音效也只有约 6600 个。
    constexpr size_t kVoiceCap = 16384;
    constexpr size_t kQueueCapacity = 1u << 16;
    constexpr size_t kLegacyMaxVoicesPerSound = 32;

    // ------------------------------------------------------------------ 解码缓存
    struct ClipEntry {
        fwcaudio::ClipPtr clip;          // 空 = 加载失败
        Clock::time_point retryAt{};     // 失败后在此时间之前不再碰磁盘
    };
    std::unordered_map<std::string, ClipEntry> s_clips;

    // ------------------------------------------------------------------ 混音流
    // Mixer 一旦创建就不再释放：FMOD 的流线程可能还在 render() 里，泄漏几百 KB 远比释放后被访问安全。
    std::atomic<fwcaudio::Mixer*> s_mixer{ nullptr };
    FMOD::ChannelGroup* s_mixGroup = nullptr;
    FMOD::Sound* s_stream = nullptr;
    FMOD::Channel* s_streamChannel = nullptr;
    int s_rate = 0;
    uint32_t s_chunk = 512;
    Clock::time_point s_nextHealthCheck{};
    Clock::time_point s_nextStartAttempt{};
    float s_lastGroupVol = -1.f;
    float s_lastChannelVol = -1.f;

    fwcaudio::FrameClock s_clock;
    struct Pending {
        fwcaudio::ClipPtr clip;
        int frame;
    };
    std::vector<Pending> s_pending;
    std::vector<int> s_scratchFrames;
    std::vector<uint64_t> s_scratchStarts;
    uint64_t s_lastReportedDrops = 0;
    Clock::time_point s_nextDropReport{};

    // ------------------------------------------------------------------ 旧引擎（备用）
    FMOD::ChannelGroup* s_legacyGroup = nullptr;
    std::unordered_map<std::string, FMOD::Sound*> s_legacySounds;
    std::unordered_map<std::string, std::vector<FMOD::Channel*>> s_legacyChannels;

    enum class Mode { Mute, Mixer, Legacy };

    std::string resolveFullPath(const std::string& path) {
        std::filesystem::path p(path);
        if (!p.is_absolute()) p = Mod::get()->getConfigDir() / p;
        return p.string();
    }

    // FMOD 流线程回调：把混音结果写进 FMOD 给的缓冲区。这里不允许调用任何 FMOD API。
    FMOD_RESULT F_CALLBACK pcmReadCb(FMOD_SOUND*, void* data, unsigned int datalen) {
        const unsigned int frames = datalen / (2u * static_cast<unsigned int>(sizeof(float)));
        if (auto* mixer = s_mixer.load(std::memory_order_acquire)) {
            mixer->render(static_cast<float*>(data), frames);
        }
        else {
            std::memset(data, 0, datalen);
        }
        return FMOD_OK;
    }

    // 无限流不需要真正的定位；提供空实现避免 FMOD 在循环时尝试 seek
    FMOD_RESULT F_CALLBACK pcmSetPosCb(FMOD_SOUND*, int, unsigned int, FMOD_TIMEUNIT) {
        return FMOD_OK;
    }

    void teardownStream() {
        if (s_stream) {
            s_stream->release(); // 同时停止其通道
            s_stream = nullptr;
        }
        s_streamChannel = nullptr;
        s_lastChannelVol = -1.f;
    }

    // 确保混音流存在且在播放。失败返回 false（调用方会退回旧引擎），并在 5 秒后才会再次尝试。
    bool ensureMixerStream(FMOD::System* sys) {
        const auto now = Clock::now();

        if (s_streamChannel) {
            if (now < s_nextHealthCheck) return true;
            s_nextHealthCheck = now + std::chrono::milliseconds(500);

            bool playing = false;
            if (s_streamChannel->isPlaying(&playing) == FMOD_OK && playing) return true;

            log::warn("[FWC audio] mixer stream stopped (audio device change?) - restarting it");
            teardownStream();
        }
        if (now < s_nextStartAttempt) return false;
        s_nextStartAttempt = now + std::chrono::seconds(5);

        int rate = 0;
        FMOD_SPEAKERMODE speakerMode = FMOD_SPEAKERMODE_DEFAULT;
        int rawSpeakers = 0;
        if (sys->getSoftwareFormat(&rate, &speakerMode, &rawSpeakers) != FMOD_OK || rate < 8000) rate = 48000;

        // 缓冲区大小来自设置：越小延迟越低，越大越不容易爆音
        int64_t bufSetting = Mod::get()->getSettingValue<int64_t>("audio-buffer-size");
        s_chunk = static_cast<uint32_t>(std::clamp<int64_t>(bufSetting, 128, 4096));

        auto* mixer = s_mixer.load(std::memory_order_acquire);
        if (!mixer || s_rate != rate) {
            // 缓存的片段已按旧采样率重采样，必须作废
            s_clips.clear();
            mixer = new fwcaudio::Mixer(static_cast<uint32_t>(rate), kQueueCapacity, kVoiceCap);
            s_mixer.store(mixer, std::memory_order_release);
            s_rate = rate;
            s_clock.reset();
        }

        if (!s_mixGroup) {
            if (sys->createChannelGroup("ModFrameSFXMixGroup", &s_mixGroup) != FMOD_OK || !s_mixGroup) {
                s_mixGroup = nullptr;
                log::warn("[FWC audio] could not create channel group - using the legacy audio engine");
                return false;
            }
        }

        FMOD_CREATESOUNDEXINFO ex;
        std::memset(&ex, 0, sizeof(ex));
        ex.cbsize = sizeof(ex);
        ex.numchannels = 2;
        ex.defaultfrequency = rate;
        ex.format = FMOD_SOUND_FORMAT_PCMFLOAT;
        ex.decodebuffersize = s_chunk;
        ex.length = static_cast<unsigned int>(rate) * 2u * static_cast<unsigned int>(sizeof(float)) * 4u; // 4 秒后自动循环，回调不受影响
        ex.pcmreadcallback = pcmReadCb;
        ex.pcmsetposcallback = pcmSetPosCb;

        FMOD::Sound* stream = nullptr;
        FMOD_RESULT res = sys->createSound(nullptr, FMOD_OPENUSER | FMOD_CREATESTREAM | FMOD_LOOP_NORMAL | FMOD_2D, &ex, &stream);
        if (res != FMOD_OK || !stream) {
            log::warn("[FWC audio] could not create the mixer stream (FMOD error {}) - using the legacy audio engine", static_cast<int>(res));
            return false;
        }

        FMOD::Channel* channel = nullptr;
        res = sys->playSound(stream, s_mixGroup, false, &channel);
        if (res != FMOD_OK || !channel) {
            log::warn("[FWC audio] could not start the mixer stream (FMOD error {}) - using the legacy audio engine", static_cast<int>(res));
            stream->release();
            return false;
        }
        channel->setPriority(0);
        channel->setVolumeRamp(false);

        s_stream = stream;
        s_streamChannel = channel;
        s_nextHealthCheck = now + std::chrono::milliseconds(500);
        s_lastGroupVol = -1.f;
        s_lastChannelVol = -1.f;
        log::info("[FWC audio] SFX mixer started: {} Hz, buffer {} samples", rate, s_chunk);
        return true;
    }

    // 游戏设置里的 SFX 音量 -> 通道组；本 Mod 的 Sound Volume -> 流通道。只在数值变化时才调用 FMOD。
    void syncVolumes(FMODAudioEngine* engine) {
        if (s_mixGroup && engine->m_sfxVolume != s_lastGroupVol) {
            s_mixGroup->setVolume(engine->m_sfxVolume);
            s_lastGroupVol = engine->m_sfxVolume;
        }
        const float mine = static_cast<float>(g_soundVolume);
        if (s_streamChannel && mine != s_lastChannelVol) {
            s_streamChannel->setVolume(mine);
            s_lastChannelVol = mine;
        }
    }

    // 用 FMOD 把整个文件解码成 PCM，再转成混音器使用的立体声 float 片段。
    fwcaudio::ClipPtr decodeClip(FMOD::System* sys, const std::string& fullPath, int dstRate) {
        FMOD::Sound* sound = nullptr;
        if (sys->createSound(fullPath.c_str(), FMOD_DEFAULT | FMOD_LOOP_OFF | FMOD_CREATESAMPLE | FMOD_2D, nullptr, &sound) != FMOD_OK || !sound) {
            return nullptr;
        }
        struct Releaser {
            FMOD::Sound* s;
            ~Releaser() { if (s) s->release(); }
        } releaser{ sound };

        FMOD_SOUND_TYPE type = FMOD_SOUND_TYPE_UNKNOWN;
        FMOD_SOUND_FORMAT format = FMOD_SOUND_FORMAT_NONE;
        int channels = 0;
        int bits = 0;
        if (sound->getFormat(&type, &format, &channels, &bits) != FMOD_OK || channels < 1) return nullptr;

        unsigned int bytes = 0;
        if (sound->getLength(&bytes, FMOD_TIMEUNIT_PCMBYTES) != FMOD_OK || bytes == 0) return nullptr;

        float freq = 0.f;
        int priority = 0;
        if (sound->getDefaults(&freq, &priority) != FMOD_OK || freq < 1.f) return nullptr;

        fwcaudio::PcmFormat pcm;
        switch (format) {
        case FMOD_SOUND_FORMAT_PCM8: pcm = fwcaudio::PcmFormat::PCM8; break;
        case FMOD_SOUND_FORMAT_PCM16: pcm = fwcaudio::PcmFormat::PCM16; break;
        case FMOD_SOUND_FORMAT_PCM24: pcm = fwcaudio::PcmFormat::PCM24; break;
        case FMOD_SOUND_FORMAT_PCM32: pcm = fwcaudio::PcmFormat::PCM32; break;
        case FMOD_SOUND_FORMAT_PCMFLOAT: pcm = fwcaudio::PcmFormat::Float; break;
        default: return nullptr;
        }

        void* p1 = nullptr;
        void* p2 = nullptr;
        unsigned int l1 = 0;
        unsigned int l2 = 0;
        if (sound->lock(0, bytes, &p1, &p2, &l1, &l2) != FMOD_OK) return nullptr;

        std::vector<uint8_t> raw(static_cast<size_t>(l1) + l2);
        if (p1 && l1) std::memcpy(raw.data(), p1, l1);
        if (p2 && l2) std::memcpy(raw.data() + l1, p2, l2);
        sound->unlock(p1, p2, l1, l2);

        return fwcaudio::buildClip(raw.data(), raw.size(), pcm, channels, static_cast<int>(std::lround(freq)), dstRate);
    }

    fwcaudio::ClipPtr getClip(const std::string& path) {
        const auto now = Clock::now();
        auto it = s_clips.find(path);
        if (it != s_clips.end()) {
            if (it->second.clip) return it->second.clip;
            if (now < it->second.retryAt) return nullptr; // 刚失败过：不要在每个音效上都去访问磁盘
        }

        auto& entry = s_clips[path];
        entry.clip = nullptr;

        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system || s_rate <= 0) return nullptr;

        const std::string full = resolveFullPath(path);
        std::error_code ec;
        if (!std::filesystem::exists(full, ec)) {
            entry.retryAt = now + std::chrono::seconds(2);
            return nullptr;
        }

        entry.clip = decodeClip(engine->m_system, full, s_rate);
        if (!entry.clip) {
            entry.retryAt = now + std::chrono::seconds(5);
            log::warn("[FWC audio] could not decode '{}'", full);
        }
        return entry.clip;
    }

    // ------------------------------------------------------------------ 旧引擎
    void playLegacy(const std::string& path) {
        auto audioEngine = FMODAudioEngine::sharedEngine();
        if (!audioEngine || !audioEngine->m_system) return;
        FMOD::System* sys = audioEngine->m_system;

        if (!s_legacyGroup) {
            if (sys->createChannelGroup("ModFrameSFXGroup", &s_legacyGroup) != FMOD_OK || !s_legacyGroup) {
                s_legacyGroup = nullptr;
                return;
            }
        }
        s_legacyGroup->setVolume(audioEngine->m_sfxVolume * static_cast<float>(g_soundVolume));

        const std::string fullPathStr = resolveFullPath(path);
        FMOD::Sound* sound = nullptr;
        auto it = s_legacySounds.find(fullPathStr);
        if (it != s_legacySounds.end()) {
            sound = it->second;
        }
        else {
            std::error_code ec;
            if (!std::filesystem::exists(fullPathStr, ec)) return;
            if (sys->createSound(fullPathStr.c_str(), FMOD_DEFAULT | FMOD_LOOP_OFF | FMOD_CREATESAMPLE, nullptr, &sound) != FMOD_OK || !sound) return;
            sound->setMode(FMOD_LOOP_OFF);
            s_legacySounds[fullPathStr] = sound;
        }
        if (!sound) return;

        auto& channels = s_legacyChannels[fullPathStr];
        for (auto chIt = channels.begin(); chIt != channels.end();) {
            bool isPlaying = false;
            if (*chIt && (*chIt)->isPlaying(&isPlaying) == FMOD_OK && isPlaying) ++chIt;
            else chIt = channels.erase(chIt);
        }
        while (channels.size() >= kLegacyMaxVoicesPerSound) {
            if (channels.front()) channels.front()->stop();
            channels.erase(channels.begin());
        }

        FMOD::Channel* channel = nullptr;
        if (sys->playSound(sound, s_legacyGroup, false, &channel) == FMOD_OK && channel) {
            channel->setMode(FMOD_LOOP_OFF);
            channel->setLoopCount(0);
            channel->setPriority(0);
            channel->setVolumeRamp(false);
            channels.push_back(channel);
        }
    }

    // 决定这一批音效怎么播
    Mode prepareEngine() {
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return Mode::Mute;
        if (engine->m_sfxVolume <= 0.0f || g_soundVolume <= 0.0) return Mode::Mute;

        if (Mod::get()->getSettingValue<bool>("use-legacy-audio")) return Mode::Legacy;
        if (!ensureMixerStream(engine->m_system)) return Mode::Legacy; // 自动退回
        syncVolumes(engine);
        return Mode::Mixer;
    }
}

void SoundManager::queueFrameSound(const std::string& path, int frame) {
    if (path.empty()) return;

    // 同一批里的第一个音效决定模式；之后的音效沿用（s_pending 非空 = 本批使用混音器）
    if (s_pending.empty()) {
        Mode mode = prepareEngine();
        if (mode == Mode::Mute) return;
        if (mode == Mode::Legacy) {
            playLegacy(path);
            return;
        }
    }

    auto clip = getClip(path);
    if (!clip) return;
    s_pending.push_back({ std::move(clip), frame });
}

void SoundManager::flushFrameSounds() {
    if (s_pending.empty()) return;

    auto* mixer = s_mixer.load(std::memory_order_acquire);
    if (!mixer || s_rate <= 0) {
        s_pending.clear();
        return;
    }

    const size_t n = s_pending.size();
    s_scratchFrames.resize(n);
    s_scratchStarts.resize(n);
    for (size_t i = 0; i < n; i++) s_scratchFrames[i] = s_pending[i].frame;

    // 每帧对应多少个采样，由宏的 TPS 决定
    const double spf = static_cast<double>(s_rate) / std::max(1.0, g_macroFps);
    const uint64_t lead = s_chunk;                                  // 保持时间线略微领先音频位置，稳态下不需要重新对齐
    const uint64_t maxAhead = static_cast<uint64_t>(s_rate) / 5;    // 最多提前 200ms，超出（快进）则压缩时间
    fwcaudio::computeStarts(s_clock, s_scratchFrames.data(), n, mixer->position(), spf, lead, maxAhead, s_scratchStarts.data());

    for (size_t i = 0; i < n; i++) {
        mixer->schedule(s_pending[i].clip, s_scratchStarts[i]);
    }
    s_pending.clear();

    // 理论上只在极端情况（队列 / 同时音效数的安全上限被打满）才会丢音效；真发生时留一条日志
    const auto st = mixer->stats();
    const uint64_t drops = st.droppedQueueFull + st.droppedVoiceCap;
    const auto now = Clock::now();
    if (drops > s_lastReportedDrops && now >= s_nextDropReport) {
        log::warn("[FWC audio] {} sounds were skipped by the safety limits (simultaneous voices {}, queue {}) - this only happens with extreme macros",
            drops - s_lastReportedDrops, st.activeVoices, st.droppedQueueFull);
        s_lastReportedDrops = drops;
        s_nextDropReport = now + std::chrono::seconds(5);
    }
}

void SoundManager::playSound(const std::string& path) {
    if (path.empty()) return;

    Mode mode = prepareEngine();
    if (mode == Mode::Mute) return;
    if (mode == Mode::Legacy) {
        playLegacy(path);
        return;
    }

    auto clip = getClip(path);
    auto* mixer = s_mixer.load(std::memory_order_acquire);
    if (clip && mixer) {
        mixer->schedule(clip, mixer->position()); // 立即开始
    }
}

void SoundManager::preload(const std::string& path) {
    if (path.empty()) return;
    // 只在使用混音器时才需要解码；旧引擎自己按需加载
    if (prepareEngine() == Mode::Mixer) {
        getClip(path);
    }
}

void SoundManager::stopAll() {
    if (auto* mixer = s_mixer.load(std::memory_order_acquire)) {
        mixer->requestClear(); // 只丢弃「此刻之前」排入的音效；之后新排入的不受影响
    }
    s_pending.clear();
    s_clock.reset();

    if (s_legacyGroup) s_legacyGroup->stop();
    s_legacyChannels.clear();
}

void SoundManager::clearCache() {
    stopAll();
    s_clips.clear();

    teardownStream();
    s_nextStartAttempt = Clock::time_point{};
    if (s_mixGroup) {
        s_mixGroup->release();
        s_mixGroup = nullptr;
    }

    for (auto& [path, sound] : s_legacySounds) {
        if (sound) sound->release();
    }
    s_legacySounds.clear();
    if (s_legacyGroup) {
        s_legacyGroup->release();
        s_legacyGroup = nullptr;
    }
}

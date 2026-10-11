#pragma once
#include <Geode/Geode.hpp>
#include <string>

// 音效管理器。
//
// 1.4.0 起不再给每个音效单独开一个 FMOD 通道（通道数有限、每次调用约 10 次加锁的 FMOD API、超出上限就会
// 互相截断），而是把所有音效叠加进「一条」软件混音输出流（见 AudioMixer.hpp）：
//   * 同时播放的音效数量不再受 FMOD 通道数限制，也不会再出现「后一个把前一个顶掉」；
//   * 游戏线程上每个音效只需一次无锁入队，不再调用 FMOD；
//   * 音效按游戏帧号落在采样精度的时间线上，同一 tick 内的多次输入会均匀铺开。
// 如果混音流无法启动（或在设置里勾选了 Use Legacy Audio Engine），会自动退回旧的「一音效一通道」方式。
class SoundManager {
public:
    // 立即播放一次（试听等非游戏场景）。
    static void playSound(const std::string& path);

    // 游戏中使用：处理一个物理步里触发的每个音效时调用 queueFrameSound()，
    // 该物理步全部处理完之后调用且仅调用一次 flushFrameSounds()。frame 为该输入所属的游戏帧号。
    static void queueFrameSound(const std::string& path, int frame);
    static void flushFrameSounds();

    // 提前解码并缓存，避免关卡里第一次触发时卡一下。
    static void preload(const std::string& path);

    // 静音并重置时间线（关卡重开 / 回档 / 退出）。
    static void stopAll();

    // 释放所有缓存的音频数据。
    static void clearCache();
};

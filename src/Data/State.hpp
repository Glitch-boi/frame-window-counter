#pragma once
#include "Types.hpp"
#include <map>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>

extern std::map<std::string, FrameWindowPreset> g_windowPresets;
extern std::map<std::string, LabelPreset> g_labelPresets;
extern std::map<std::string, FrameAction> g_frameActions;
extern std::vector<FrameAction> g_tickActionsCache;
extern std::vector<FrameAction> g_validActions;

extern double g_macroFps;
extern bool g_modEnabled;
extern bool g_showBase, g_showN, g_showF, g_showC, g_showNF, g_showNC, g_showFC, g_showNFC;
extern bool g_forcePrecRedraw;
extern int g_finalLStarDisplayMode;
extern double g_respawnTime;      // 死亡到复活时间 (秒)
extern double g_targetTime;       // 目标求解时间 (秒)
extern double g_kT;               // Nerve 常数
extern double g_kU;               // Fatigue 常数
extern double g_kC;               // CPS 常数

// ---- 显示设置 (Display Settings) ----
extern double g_precLabelScale;   // 精度标签 (左下角 L*) 大小倍率
extern double g_counterScale;     // 计数标签 (左上角 HUD) 大小倍率
extern double g_markerScale;      // 游戏内标记 (圆圈 + 文字) 大小倍率
extern double g_hudOpacity;       // 精度/计数标签不透明度
extern double g_windowScale;      // 所有 Mod 弹窗的整体大小倍率
extern bool g_showMarkers;        // 是否显示游戏内标记
extern bool g_showBasePct;        // 是否显示 Base L* 百分比 (HUD + 编辑器每行)
extern double g_soundVolume;      // 本 Mod 音效音量 (0~1)，叠加在游戏 SFX 音量之上
extern bool g_showNotifications;  // 是否显示本 Mod 的所有通知（自动保存、快捷键提示等）

extern std::atomic<int> g_calcId;
extern std::atomic<bool> g_isCalculating;
extern std::atomic<float> g_calcProgress;
extern std::atomic<bool> g_isLStarDirty;
extern std::chrono::steady_clock::time_point g_lastAutoSaveTime;

extern std::vector<double> g_vBase, g_vN, g_vF, g_vC, g_vNF, g_vNC, g_vFC, g_vNFC;

inline std::string formatWindowVal(double val) {
    if (std::abs(val) < 1e-9) return "0";
    std::ostringstream oss;
    oss << std::setprecision(6) << val;
    return oss.str();
}

// 生成二维 window 预设键：格式为 "if_window"，例如 "1_5" 或 "2_5"
inline std::string makeWindowPresetKey(int ifCount, double val) {
    if (ifCount < 1) ifCount = 1;
    return std::to_string(ifCount) + "_" + formatWindowVal(val);
}

void loadModData();
void saveSettings();
void saveFrames();
void updateTickCache();
void doAutoSave(GJGameLevel* level);
void resetCalcSettingsToDefault();
void saveDisplaySettings();              // 仅保存显示设置（滑条拖动时调用，开销很小）
void resetDisplaySettingsToDefault();

// 本 Mod 所有的 Notification 都必须经过这里，这样「Show Notifications」开关才能一次性关掉全部提示。
void showModNotification(std::string const& text, geode::NotificationIcon icon = geode::NotificationIcon::Success);

// ---- 在 PlayLayerHook.cpp 中实现，供 UI / 快捷键调用 ----
int getCurrentPhysicsFrame();                               // 当前物理帧 (tick)
void triggerHUDRefresh();                                   // 重新统计并重建 HUD 计数
void triggerHUDLayoutRefresh();                             // 仅按新的大小/透明度重建 HUD，不改动计数
void spawnFeedbackMarker(int frame, bool isPlayer2, double frameWindow, int ifCount); // 快捷键添加后在玩家处生成标记
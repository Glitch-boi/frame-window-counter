#include <Geode/Geode.hpp>

// 快捷键仅桌面端 (Windows / macOS) 提供；移动端没有键盘，整个文件在移动端编译为空。
#ifndef GEODE_IS_MOBILE

#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include "../Data/State.hpp"
#include "../Common.hpp"
#include "../Math/Calculator.hpp"
#include "../UI/AddFramePopup.hpp"
#include "../UI/FrameActionPopup.hpp"
#include "../UI/PrecisionSettingsPopup.hpp"
#include "../UI/LStarCalcSettingsPopup.hpp"
#include "../UI/LabelPresetPopup.hpp"
#include "../UI/WindowPresetPopup.hpp"
#include "../UI/DisplaySettingsPopup.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

    // 通过快捷键添加的动作 key（最新的在最后），供"撤销"使用
    std::vector<std::string> s_keybindAddedKeys;

    enum class AddStatus { Added, Duplicate, NoLevel, ModDisabled };

    struct AddOutcome {
        AddStatus status;
        int frame;
    };

    bool isAnyTextInputFocused(CCNode* root) {
        if (!root) return false;

        if (auto textInput = typeinfo_cast<CCTextInputNode*>(root)) {
            if (textInput->m_selected) {
                return true;
            }
        }

        if (auto children = root->getChildren()) {
            for (int i = 0; i < static_cast<int>(children->count()); i++) {
                auto child = static_cast<CCNode*>(children->objectAtIndex(i));
                if (isAnyTextInputFocused(child)) {
                    return true;
                }
            }
        }
        return false;
    }

    // 与原先 O 键行为完全一致：有 Mod 弹窗打开则全部关闭，否则打开编辑器
    void toggleModPopups(CCScene* scene) {
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
        if (isAnyTextInputFocused(scene)) return;

        bool closedAny = false;
        if (auto children = scene->getChildren()) {
            for (int i = static_cast<int>(children->count()) - 1; i >= 0; i--) {
                auto child = static_cast<CCNode*>(children->objectAtIndex(i));

                if (typeinfo_cast<AddFramePopup*>(child) ||
                    typeinfo_cast<FrameActionPopup*>(child) ||
                    typeinfo_cast<PrecisionSettingsPopup*>(child) ||
                    typeinfo_cast<LStarCalcSettingsPopup*>(child) ||
                    typeinfo_cast<LabelPresetPopup*>(child) ||
                    typeinfo_cast<WindowPresetPopup*>(child) ||
                    typeinfo_cast<DisplaySettingsPopup*>(child)) {
                    child->removeFromParentAndCleanup(true);
                    closedAny = true;
                }
            }
        }

        if (!closedAny) {
            if (auto popup = FrameActionPopup::create()) {
                popup->setID("FrameActionPopup"_spr);
                popup->showInstant();
            }
        }
    }

    void notify(std::string const& text, NotificationIcon icon) {
        if (!Mod::get()->getSettingValue<bool>("keybind-notify")) return;
        Notification::create(text, icon)->show();
    }

    // 与 AddFramePopup / 合并功能使用同一套 key 规则： "<帧>_<0|1>_<序号>"
    std::string makeUniqueActionKey(int frame, bool isPlayer2) {
        std::string base = std::to_string(frame) + (isPlayer2 ? "_1" : "_0");
        int idx = 0;
        std::string key = base + "_" + std::to_string(idx);
        while (g_frameActions.contains(key)) {
            idx++;
            key = base + "_" + std::to_string(idx);
        }
        return key;
    }

    AddOutcome addOneFrameWindowAtCurrentTick() {
        auto pl = PlayLayer::get();
        if (!pl) return { AddStatus::NoLevel, -1 };
        if (!g_modEnabled) return { AddStatus::ModDisabled, -1 };

        const int offset = static_cast<int>(Mod::get()->getSettingValue<int64_t>("keybind-frame-offset"));
        const int currentTick = getCurrentPhysicsFrame();
        const int frame = std::max(0, currentTick + offset);

        // 同一帧同一玩家已有动作时不重复添加（防止连按 / 复活后重复按产生重复输入）
        for (auto const& [key, act] : g_frameActions) {
            if (act.frame == frame && !act.isPlayer2) {
                return { AddStatus::Duplicate, frame };
            }
        }

        FrameAction act;
        act.frame = frame;
        act.shouldDraw = true;
        act.frameWindow = 1.0;
        act.isPlayer2 = false;
        act.ifCount = 1;

        auto key = makeUniqueActionKey(frame, false);
        g_frameActions[key] = act;
        s_keybindAddedKeys.push_back(key);

        saveFrames();           // 停止进行中的计算、标记 L* 为过期、重建按帧排序缓存
        triggerHUDRefresh();    // 立即刷新左上角计数

        if (frame == currentTick) {
            spawnFeedbackMarker(frame, false, act.frameWindow, act.ifCount);
        }
        return { AddStatus::Added, frame };
    }

    // 撤销最近一次通过快捷键添加的帧。找不到（已被手动删除）的会被跳过。
    bool undoLastKeybindFrame(int& frameOut) {
        while (!s_keybindAddedKeys.empty()) {
            auto key = s_keybindAddedKeys.back();
            s_keybindAddedKeys.pop_back();

            auto it = g_frameActions.find(key);
            if (it == g_frameActions.end()) continue;

            frameOut = it->second.frame;
            g_frameActions.erase(it);
            saveFrames();
            triggerHUDRefresh();
            return true;
        }
        return false;
    }

    void openEditorAtFrame(int frame) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return;

        auto popup = typeinfo_cast<FrameActionPopup*>(scene->getChildByID("FrameActionPopup"_spr));
        if (!popup) {
            popup = FrameActionPopup::create();
            if (!popup) return;
            popup->setID("FrameActionPopup"_spr);
            popup->showInstant();
        }
        // 编辑器已打开时不关闭它（O 键才是开关），只刷新并跳转到新帧
        if (frame >= 0) popup->jumpToFrame(frame);
    }

    void maybeAutoRecalc() {
        if (Mod::get()->getSettingValue<bool>("keybind-auto-recalc")) {
            startGlobalRecalc();
        }
    }

    void handleAddFrame(bool alsoOpenEditor) {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
        if (isAnyTextInputFocused(scene)) return;

        auto out = addOneFrameWindowAtCurrentTick();
        switch (out.status) {
        case AddStatus::NoLevel:
            notify("Frame Window: you need to be in a level", NotificationIcon::Warning);
            break;
        case AddStatus::ModDisabled:
            notify("Frame Window Counter is disabled", NotificationIcon::Warning);
            break;
        case AddStatus::Duplicate:
            notify(fmt::format("Tick {} already has a 1P frame", out.frame), NotificationIcon::Warning);
            break;
        case AddStatus::Added:
            notify(fmt::format("Added 1-frame window at tick {}", out.frame), NotificationIcon::Success);
            maybeAutoRecalc();
            break;
        }

        if (alsoOpenEditor) {
            // 即使这次没能添加（不在关卡里 / 重复帧），也照常打开 UI，行为与按 O 一致
            openEditorAtFrame(out.status == AddStatus::Added || out.status == AddStatus::Duplicate ? out.frame : -1);
        }
    }

    void handleUndo() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return;
        if (isAnyTextInputFocused(scene)) return;

        int frame = -1;
        if (!undoLastKeybindFrame(frame)) {
            notify("Nothing to undo", NotificationIcon::Warning);
            return;
        }
        notify(fmt::format("Removed frame {}", frame), NotificationIcon::Success);
        maybeAutoRecalc();

        // 编辑器正开着时同步刷新列表
        if (auto popup = typeinfo_cast<FrameActionPopup*>(scene->getChildByID("FrameActionPopup"_spr))) {
            popup->reloadFromState();
        }
    }

} // namespace

$on_game(Loaded) {
    listenForKeybindSettingPresses("open-editor-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
            toggleModPopups(scene);
        }
    });

    listenForKeybindSettingPresses("add-frame-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        handleAddFrame(false);
    });

    listenForKeybindSettingPresses("add-frame-open-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        handleAddFrame(true);
    });

    listenForKeybindSettingPresses("undo-frame-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return;
        handleUndo();
    });
}

#endif // !GEODE_IS_MOBILE

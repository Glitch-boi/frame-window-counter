#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>

#include <Geode/modify/PauseLayer.hpp>
#include "../Data/State.hpp"
#include "../Common.hpp"
#include "../UI/FrameActionPopup.hpp"

using namespace geode::prelude;

// 注意：原先写死在此处的 O 键 (CCKeyboardDispatcher) 已迁移为可自定义的 Geode 快捷键设置，
// 见 KeybindHooks.cpp（默认仍为 O）。

class $modify(MyDirector, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();

        auto scene = this->getRunningScene();
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - g_lastAutoSaveTime).count() >= AUTOSAVETIME) {
            g_lastAutoSaveTime = now;
            if (auto pl = PlayLayer::get(); pl && pl->m_level) {
                doAutoSave(pl->m_level);
            }
        }

        if (g_modEnabled) {
            if (auto playLayer = PlayLayer::get()) {
                if (!playLayer->m_isPaused && playLayer->m_player1 && !playLayer->m_player1->m_isDead) {
                    if (auto popup = typeinfo_cast<FrameActionPopup*>(scene->getChildByID("FrameActionPopup"_spr))) {
                        int currentFrame = getCurrentPhysicsFrame();
                        popup->doTrackingTick(currentFrame);
                    }
                }
            }
        }
    }
};
#include "PopupScale.hpp"
#include "../Data/State.hpp"
#include "../Math/DisplayMath.hpp"
#include "AddFramePopup.hpp"
#include "FrameActionPopup.hpp"
#include "PrecisionSettingsPopup.hpp"
#include "LStarCalcSettingsPopup.hpp"
#include "LabelPresetPopup.hpp"
#include "WindowPresetPopup.hpp"
#include "DisplaySettingsPopup.hpp"

using namespace geode::prelude;

void applyWindowScale(FLAlertLayer* popup) {
    if (!popup || !popup->m_mainLayer) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto size = popup->m_mainLayer->getContentSize();

    popup->m_mainLayer->setScale(dispmath::fitPopupScale(
        g_windowScale, size.width, size.height, winSize.width, winSize.height
    ));
}

static bool isModPopup(CCNode* node) {
    return typeinfo_cast<AddFramePopup*>(node) ||
        typeinfo_cast<FrameActionPopup*>(node) ||
        typeinfo_cast<PrecisionSettingsPopup*>(node) ||
        typeinfo_cast<LStarCalcSettingsPopup*>(node) ||
        typeinfo_cast<LabelPresetPopup*>(node) ||
        typeinfo_cast<WindowPresetPopup*>(node) ||
        typeinfo_cast<DisplaySettingsPopup*>(node);
}

void applyWindowScaleToOpenModPopups(CCNode* except) {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    auto children = scene->getChildren();
    if (!children) return;

    for (int i = 0; i < static_cast<int>(children->count()); i++) {
        auto child = static_cast<CCNode*>(children->objectAtIndex(i));
        if (!child || child == except) continue;
        if (!isModPopup(child)) continue;

        if (auto alert = typeinfo_cast<FLAlertLayer*>(child)) {
            applyWindowScale(alert);
        }
    }
}

#include "DisplaySettingsPopup.hpp"
#include "PopupScale.hpp"
#include "../Data/State.hpp"
#include "../Common.hpp"
#include "../Math/DisplayMath.hpp"

using namespace geode::prelude;

namespace {
    constexpr double SCALE_STEP = 0.05;     // 所有滑条按 5% 吸附
    constexpr float SLIDER_SCALE = 0.7f;    // 滑条整体缩放（GD 原生滑条默认偏宽）
    constexpr float SLIDER_X = 222.f;       // 滑条中心 X
    constexpr float VALUE_X = 334.f;        // 数值标签中心 X

    std::string percentText(double v) {
        return fmt::format("{}%", static_cast<int>(std::lround(v * 100.0)));
    }
}

bool DisplaySettingsPopup::init() {
    if (!Popup::init(360.f, 290.f)) return false;
    this->setTitle("Display Settings");

    auto size = m_mainLayer->getContentSize();

    auto menu = CCMenu::create();
    menu->setPosition({ 0, 0 });
    m_mainLayer->addChild(menu);

    // 一行 = 名称 + 滑条 + 当前百分比
    auto addRow = [&](const char* name, float y, SEL_MenuHandler selector,
                      Slider*& sliderOut, CCLabelBMFont*& valueOut) {
        auto nameLbl = CCLabelBMFont::create(name, "bigFont.fnt");
        nameLbl->setScale(0.36f);
        nameLbl->setAnchorPoint({ 0.f, 0.5f });
        nameLbl->setPosition({ 16.f, y });
        m_mainLayer->addChild(nameLbl);

        sliderOut = Slider::create(this, selector);
        sliderOut->setScale(SLIDER_SCALE);
        sliderOut->setPosition({ SLIDER_X, y });
        sliderOut->setLiveDragging(true);   // 拖动时实时回调，HUD 立即变化
        m_mainLayer->addChild(sliderOut);

        valueOut = CCLabelBMFont::create("100%", "bigFont.fnt");
        valueOut->setScale(0.36f);
        valueOut->setPosition({ VALUE_X, y });
        m_mainLayer->addChild(valueOut);
    };

    addRow("Precision Size", 232.f, menu_selector(DisplaySettingsPopup::onPrecSlider), m_precSlider, m_precValue);
    addRow("Counter Size", 197.f, menu_selector(DisplaySettingsPopup::onCounterSlider), m_counterSlider, m_counterValue);
    addRow("Marker Size", 162.f, menu_selector(DisplaySettingsPopup::onMarkerSlider), m_markerSlider, m_markerValue);
    addRow("HUD Opacity", 127.f, menu_selector(DisplaySettingsPopup::onOpacitySlider), m_opacitySlider, m_opacityValue);
    addRow("Window Size", 92.f, menu_selector(DisplaySettingsPopup::onWindowSlider), m_windowSlider, m_windowValue);

    // 显示游戏内标记 开关
    auto markerLbl = CCLabelBMFont::create("Show Markers", "bigFont.fnt");
    markerLbl->setScale(0.36f);
    markerLbl->setAnchorPoint({ 0.f, 0.5f });
    markerLbl->setPosition({ 16.f, 58.f });
    m_mainLayer->addChild(markerLbl);

    m_markerToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(DisplaySettingsPopup::onToggleMarkers), 0.6f
    );
    m_markerToggle->setPosition({ 140.f, 58.f });
    menu->addChild(m_markerToggle);

    // 重置
    auto resetSpr = ButtonSprite::create("Reset Defaults", "goldFont.fnt", "GJ_button_05.png", 0.8f);
    resetSpr->setScale(0.7f);
    auto resetBtn = CCMenuItemSpriteExtra::create(
        resetSpr, this, menu_selector(DisplaySettingsPopup::onResetDefaults)
    );
    resetBtn->setPosition({ size.width / 2, 24.f });
    menu->addChild(resetBtn);

    this->syncFromState();
    return true;
}

void DisplaySettingsPopup::syncFromState() {
    m_syncing = true;
    if (m_precSlider) m_precSlider->setValue(dispmath::valueToSlider(g_precLabelScale, MIN_LABEL_SCALE, MAX_LABEL_SCALE));
    if (m_counterSlider) m_counterSlider->setValue(dispmath::valueToSlider(g_counterScale, MIN_LABEL_SCALE, MAX_LABEL_SCALE));
    if (m_markerSlider) m_markerSlider->setValue(dispmath::valueToSlider(g_markerScale, MIN_LABEL_SCALE, MAX_LABEL_SCALE));
    if (m_opacitySlider) m_opacitySlider->setValue(dispmath::valueToSlider(g_hudOpacity, MIN_HUD_OPACITY, MAX_HUD_OPACITY));
    if (m_windowSlider) m_windowSlider->setValue(dispmath::valueToSlider(g_windowScale, MIN_WINDOW_SCALE, MAX_WINDOW_SCALE));
    if (m_markerToggle) m_markerToggle->toggle(g_showMarkers);
    m_syncing = false;

    this->updateValueLabels();
}

void DisplaySettingsPopup::updateValueLabels() {
    if (m_precValue) m_precValue->setString(percentText(g_precLabelScale).c_str());
    if (m_counterValue) m_counterValue->setString(percentText(g_counterScale).c_str());
    if (m_markerValue) m_markerValue->setString(percentText(g_markerScale).c_str());
    if (m_opacityValue) m_opacityValue->setString(percentText(g_hudOpacity).c_str());
    if (m_windowValue) m_windowValue->setString(percentText(g_windowScale).c_str());
}

// 滑条回调：直接读取对应滑条当前位置 (0~1)，映射到真实范围
void DisplaySettingsPopup::onPrecSlider(CCObject*) {
    if (m_syncing || !m_precSlider) return;
    g_precLabelScale = dispmath::sliderToValue(m_precSlider->getValue(), MIN_LABEL_SCALE, MAX_LABEL_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();
    triggerHUDLayoutRefresh();
}

void DisplaySettingsPopup::onCounterSlider(CCObject*) {
    if (m_syncing || !m_counterSlider) return;
    g_counterScale = dispmath::sliderToValue(m_counterSlider->getValue(), MIN_LABEL_SCALE, MAX_LABEL_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();
    triggerHUDLayoutRefresh();
}

void DisplaySettingsPopup::onMarkerSlider(CCObject*) {
    if (m_syncing || !m_markerSlider) return;
    g_markerScale = dispmath::sliderToValue(m_markerSlider->getValue(), MIN_LABEL_SCALE, MAX_LABEL_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();   // 已存在的标记会在下一次 tick 自动应用新大小
}

void DisplaySettingsPopup::onOpacitySlider(CCObject*) {
    if (m_syncing || !m_opacitySlider) return;
    g_hudOpacity = dispmath::sliderToValue(m_opacitySlider->getValue(), MIN_HUD_OPACITY, MAX_HUD_OPACITY, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();
    triggerHUDLayoutRefresh();
}

void DisplaySettingsPopup::onWindowSlider(CCObject*) {
    if (m_syncing || !m_windowSlider) return;
    g_windowScale = dispmath::sliderToValue(m_windowSlider->getValue(), MIN_WINDOW_SCALE, MAX_WINDOW_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();

    // 实时预览：缩放场景中其他已打开的 Mod 弹窗（例如底下的编辑器）。
    // 本弹窗自身不在拖动时缩放（否则滑块会从手指下滑走），关闭后再次打开即生效。
    applyWindowScaleToOpenModPopups(this);
}

void DisplaySettingsPopup::onToggleMarkers(CCObject*) {
    g_showMarkers = !g_showMarkers;
    saveDisplaySettings();
}

void DisplaySettingsPopup::onResetDefaults(CCObject*) {
    resetDisplaySettingsToDefault();
    this->syncFromState();
    triggerHUDLayoutRefresh();
    // 点击而非拖动，可以连同自身一起立即应用
    applyWindowScaleToOpenModPopups(nullptr);
}

void DisplaySettingsPopup::onClose(CCObject* sender) {
    saveDisplaySettings();
    Popup::onClose(sender);
}

DisplaySettingsPopup* DisplaySettingsPopup::create() {
    auto ret = new DisplaySettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void DisplaySettingsPopup::showInstant() {
    this->show();
    if (this->m_mainLayer) {
        this->m_mainLayer->stopAllActions();
        applyWindowScale(this);
    }
    if (this->m_bgSprite) {
        this->m_bgSprite->stopAllActions();
        this->m_bgSprite->setOpacity(150);
    }
}

#include "DisplaySettingsPopup.hpp"
#include "PopupScale.hpp"
#include "../Data/State.hpp"
#include "../Common.hpp"
#include "../Math/DisplayMath.hpp"

using namespace geode::prelude;

namespace {
    constexpr double SCALE_STEP = 0.05;     // every slider snaps to 5%
    constexpr float SLIDER_SCALE = 0.7f;    // GD's native slider is ~208 units wide; 0.7 -> ~146
    constexpr float SLIDER_X = 222.f;       // slider centre X (spans ~149..295)
    constexpr float VALUE_X = 334.f;        // percentage label centre X

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

    // One row = name + slider + current percentage
    auto addRow = [&](const char* name, float y, SEL_MenuHandler selector,
                      Slider*& sliderOut, CCLabelBMFont*& valueOut) {
        auto nameLbl = CCLabelBMFont::create(name, "bigFont.fnt");
        nameLbl->setScale(0.36f);
        nameLbl->setAnchorPoint({ 0.f, 0.5f });
        nameLbl->setPosition({ 16.f, y });
        m_mainLayer->addChild(nameLbl);

        // IMPORTANT: GD's Slider is a CCLayer whose content size is the whole SCREEN (569x320), so calling
        // setScale() on it pivots around the screen centre and drags the slider ~(86, 49) units away from
        // its row (that was the 1.3.0 misplacement bug). Scale a zero-size holder instead: its pivot is its
        // own origin, and the slider inside stays unscaled and centred on that origin.
        auto holder = CCNode::create();
        holder->setPosition({ SLIDER_X, y });
        holder->setScale(SLIDER_SCALE);
        m_mainLayer->addChild(holder);

        sliderOut = Slider::create(this, selector);
        sliderOut->setLiveDragging(true);   // live callbacks while dragging, so the HUD follows the thumb
        holder->addChild(sliderOut);

        valueOut = CCLabelBMFont::create("100%", "bigFont.fnt");
        valueOut->setScale(0.36f);
        valueOut->setPosition({ VALUE_X, y });
        m_mainLayer->addChild(valueOut);
    };

    addRow("Precision Size", 238.f, menu_selector(DisplaySettingsPopup::onPrecSlider), m_precSlider, m_precValue);
    addRow("Counter Size", 208.f, menu_selector(DisplaySettingsPopup::onCounterSlider), m_counterSlider, m_counterValue);
    addRow("Marker Size", 178.f, menu_selector(DisplaySettingsPopup::onMarkerSlider), m_markerSlider, m_markerValue);
    addRow("HUD Opacity", 148.f, menu_selector(DisplaySettingsPopup::onOpacitySlider), m_opacitySlider, m_opacityValue);
    addRow("Sound Volume", 118.f, menu_selector(DisplaySettingsPopup::onVolumeSlider), m_volumeSlider, m_volumeValue);
    addRow("Window Size", 88.f, menu_selector(DisplaySettingsPopup::onWindowSlider), m_windowSlider, m_windowValue);

    // Switches: Show Markers (left) and Notifications (right)
    auto markerLbl = CCLabelBMFont::create("Show Markers", "bigFont.fnt");
    markerLbl->setScale(0.36f);
    markerLbl->setAnchorPoint({ 0.f, 0.5f });
    markerLbl->setPosition({ 16.f, 56.f });
    m_mainLayer->addChild(markerLbl);

    m_markerToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(DisplaySettingsPopup::onToggleMarkers), 0.6f
    );
    m_markerToggle->setPosition({ 120.f, 56.f });
    menu->addChild(m_markerToggle);

    auto notifyLbl = CCLabelBMFont::create("Notifications", "bigFont.fnt");
    notifyLbl->setScale(0.36f);
    notifyLbl->setAnchorPoint({ 0.f, 0.5f });
    notifyLbl->setPosition({ 190.f, 56.f });
    m_mainLayer->addChild(notifyLbl);

    m_notifyToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(DisplaySettingsPopup::onToggleNotifications), 0.6f
    );
    m_notifyToggle->setPosition({ 298.f, 56.f });
    menu->addChild(m_notifyToggle);

    // Reset
    auto resetSpr = ButtonSprite::create("Reset Defaults", "goldFont.fnt", "GJ_button_05.png", 0.8f);
    resetSpr->setScale(0.7f);
    auto resetBtn = CCMenuItemSpriteExtra::create(
        resetSpr, this, menu_selector(DisplaySettingsPopup::onResetDefaults)
    );
    resetBtn->setPosition({ size.width / 2, 22.f });
    menu->addChild(resetBtn);

    this->syncFromState();
    this->scheduleUpdate();
    return true;
}

void DisplaySettingsPopup::syncFromState() {
    m_syncing = true;
    if (m_precSlider) m_precSlider->setValue(dispmath::valueToSlider(g_precLabelScale, MIN_LABEL_SCALE, MAX_LABEL_SCALE));
    if (m_counterSlider) m_counterSlider->setValue(dispmath::valueToSlider(g_counterScale, MIN_LABEL_SCALE, MAX_LABEL_SCALE));
    if (m_markerSlider) m_markerSlider->setValue(dispmath::valueToSlider(g_markerScale, MIN_LABEL_SCALE, MAX_LABEL_SCALE));
    if (m_opacitySlider) m_opacitySlider->setValue(dispmath::valueToSlider(g_hudOpacity, MIN_HUD_OPACITY, MAX_HUD_OPACITY));
    if (m_volumeSlider) m_volumeSlider->setValue(dispmath::valueToSlider(g_soundVolume, MIN_SOUND_VOLUME, MAX_SOUND_VOLUME));
    if (m_windowSlider) m_windowSlider->setValue(dispmath::valueToSlider(g_windowScale, MIN_WINDOW_SCALE, MAX_WINDOW_SCALE));
    if (m_markerToggle) m_markerToggle->toggle(g_showMarkers);
    if (m_notifyToggle) m_notifyToggle->toggle(g_showNotifications);
    m_syncing = false;

    this->updateValueLabels();
}

void DisplaySettingsPopup::updateValueLabels() {
    if (m_precValue) m_precValue->setString(percentText(g_precLabelScale).c_str());
    if (m_counterValue) m_counterValue->setString(percentText(g_counterScale).c_str());
    if (m_markerValue) m_markerValue->setString(percentText(g_markerScale).c_str());
    if (m_opacityValue) m_opacityValue->setString(percentText(g_hudOpacity).c_str());
    if (m_volumeValue) m_volumeValue->setString(percentText(g_soundVolume).c_str());
    if (m_windowValue) m_windowValue->setString(percentText(g_windowScale).c_str());
}

// Slider callbacks read the slider's current position (0..1) and map it to the real range.
void DisplaySettingsPopup::onPrecSlider(CCObject*) {
    if (m_syncing || !m_precSlider) return;
    g_precLabelScale = dispmath::sliderToValue(m_precSlider->getValue(), MIN_LABEL_SCALE, MAX_LABEL_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();
    m_hudDirty = true;
}

void DisplaySettingsPopup::onCounterSlider(CCObject*) {
    if (m_syncing || !m_counterSlider) return;
    g_counterScale = dispmath::sliderToValue(m_counterSlider->getValue(), MIN_LABEL_SCALE, MAX_LABEL_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();
    m_hudDirty = true;
}

void DisplaySettingsPopup::onMarkerSlider(CCObject*) {
    if (m_syncing || !m_markerSlider) return;
    g_markerScale = dispmath::sliderToValue(m_markerSlider->getValue(), MIN_LABEL_SCALE, MAX_LABEL_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();   // existing markers pick up the new size on the next tick
}

void DisplaySettingsPopup::onOpacitySlider(CCObject*) {
    if (m_syncing || !m_opacitySlider) return;
    g_hudOpacity = dispmath::sliderToValue(m_opacitySlider->getValue(), MIN_HUD_OPACITY, MAX_HUD_OPACITY, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();
    m_hudDirty = true;
}

void DisplaySettingsPopup::onVolumeSlider(CCObject*) {
    if (m_syncing || !m_volumeSlider) return;
    g_soundVolume = dispmath::sliderToValue(m_volumeSlider->getValue(), MIN_SOUND_VOLUME, MAX_SOUND_VOLUME, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();   // the audio engine picks the new volume up on the next sound
}

void DisplaySettingsPopup::onWindowSlider(CCObject*) {
    if (m_syncing || !m_windowSlider) return;
    g_windowScale = dispmath::sliderToValue(m_windowSlider->getValue(), MIN_WINDOW_SCALE, MAX_WINDOW_SCALE, SCALE_STEP);
    saveDisplaySettings();
    this->updateValueLabels();

    // Live preview on the OTHER open mod windows (e.g. the editor underneath). This popup is not rescaled
    // while dragging, otherwise the thumb would slide out from under the finger; it applies on next open.
    applyWindowScaleToOpenModPopups(this);
}

void DisplaySettingsPopup::onToggleMarkers(CCObject*) {
    g_showMarkers = !g_showMarkers;
    saveDisplaySettings();
}

void DisplaySettingsPopup::onToggleNotifications(CCObject*) {
    g_showNotifications = !g_showNotifications;
    saveDisplaySettings();
}

void DisplaySettingsPopup::onResetDefaults(CCObject*) {
    resetDisplaySettingsToDefault();
    this->syncFromState();
    m_hudDirty = false;
    triggerHUDLayoutRefresh();
    // A click, not a drag, so this popup can be rescaled together with the others right away
    applyWindowScaleToOpenModPopups(nullptr);
}

// Rebuilding the HUD on every drag event is wasteful (several events can arrive per frame): do it once per frame.
void DisplaySettingsPopup::update(float dt) {
    Popup::update(dt);
    if (m_hudDirty) {
        m_hudDirty = false;
        triggerHUDLayoutRefresh();
    }
}

void DisplaySettingsPopup::onClose(CCObject* sender) {
    if (m_hudDirty) {
        m_hudDirty = false;
        triggerHUDLayoutRefresh();
    }
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

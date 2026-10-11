#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

// Display settings: precision / counter / marker size, HUD opacity, sound volume, size of every window,
// plus the Show Markers and Notifications switches.
class DisplaySettingsPopup : public geode::Popup {
protected:
    Slider* m_precSlider = nullptr;
    Slider* m_counterSlider = nullptr;
    Slider* m_markerSlider = nullptr;
    Slider* m_opacitySlider = nullptr;
    Slider* m_volumeSlider = nullptr;
    Slider* m_windowSlider = nullptr;

    cocos2d::CCLabelBMFont* m_precValue = nullptr;
    cocos2d::CCLabelBMFont* m_counterValue = nullptr;
    cocos2d::CCLabelBMFont* m_markerValue = nullptr;
    cocos2d::CCLabelBMFont* m_opacityValue = nullptr;
    cocos2d::CCLabelBMFont* m_volumeValue = nullptr;
    cocos2d::CCLabelBMFont* m_windowValue = nullptr;
    CCMenuItemToggler* m_markerToggle = nullptr;
    CCMenuItemToggler* m_notifyToggle = nullptr;

    bool m_syncing = false;     // ignore slider callbacks while we move the sliders from code
    bool m_hudDirty = false;    // HUD needs a rebuild; done at most once per frame, not once per drag event

    bool init() override;
    void update(float dt) override;
    void syncFromState();
    void updateValueLabels();

    void onPrecSlider(cocos2d::CCObject*);
    void onCounterSlider(cocos2d::CCObject*);
    void onMarkerSlider(cocos2d::CCObject*);
    void onOpacitySlider(cocos2d::CCObject*);
    void onVolumeSlider(cocos2d::CCObject*);
    void onWindowSlider(cocos2d::CCObject*);
    void onToggleMarkers(cocos2d::CCObject*);
    void onToggleNotifications(cocos2d::CCObject*);
    void onResetDefaults(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);

public:
    static DisplaySettingsPopup* create();
    void showInstant();
};

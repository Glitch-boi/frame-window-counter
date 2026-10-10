#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

// 显示设置：精度标签 / 计数标签 / 游戏内标记 的大小、HUD 不透明度、所有弹窗的整体大小。
class DisplaySettingsPopup : public geode::Popup {
protected:
    Slider* m_precSlider = nullptr;
    Slider* m_counterSlider = nullptr;
    Slider* m_markerSlider = nullptr;
    Slider* m_opacitySlider = nullptr;
    Slider* m_windowSlider = nullptr;

    cocos2d::CCLabelBMFont* m_precValue = nullptr;
    cocos2d::CCLabelBMFont* m_counterValue = nullptr;
    cocos2d::CCLabelBMFont* m_markerValue = nullptr;
    cocos2d::CCLabelBMFont* m_opacityValue = nullptr;
    cocos2d::CCLabelBMFont* m_windowValue = nullptr;
    CCMenuItemToggler* m_markerToggle = nullptr;

    bool m_syncing = false;   // 以代码设置滑条位置时，忽略回调，避免把值写回

    bool init() override;
    void syncFromState();
    void updateValueLabels();

    void onPrecSlider(cocos2d::CCObject*);
    void onCounterSlider(cocos2d::CCObject*);
    void onMarkerSlider(cocos2d::CCObject*);
    void onOpacitySlider(cocos2d::CCObject*);
    void onWindowSlider(cocos2d::CCObject*);
    void onToggleMarkers(cocos2d::CCObject*);
    void onResetDefaults(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*);

public:
    static DisplaySettingsPopup* create();
    void showInstant();
};

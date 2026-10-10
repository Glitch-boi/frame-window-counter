#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <vector>
#include <string>
#include <cstdint>

class FrameActionPopup : public geode::Popup {
public:
    int m_currentPage = 0;
    int m_itemsPerPage = 50;
    int m_lastHighlightedFrame = -1;
    std::vector<std::string> m_sortedKeys;

    cocos2d::CCLabelBMFont* m_computingLabel = nullptr;
    CCMenuItemSpriteExtra* m_finalLStarBtn = nullptr;
    cocos2d::CCLabelBMFont* m_finalLStarLabel = nullptr;
    bool m_wasCalculating = true;
    bool m_wasDirty = false;
    uint64_t m_pctSignature = ~0ull;   // 上次刷新每行 L* 百分比时的状态签名（开关/计算中/过期/动作数）

protected:
    geode::ScrollLayer* m_scrollLayer = nullptr;
    geode::TextInput* m_pageInput = nullptr;
    cocos2d::CCLabelBMFont* m_totalPagesLabel = nullptr;
    CCMenuItemSpriteExtra* m_prevBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextBtn = nullptr;

    bool init() override;
    void updateFinalLStarText();
    void onCycleLStar(cocos2d::CCObject*);
    void update(float dt) override;

    void onAddFramePrompt(cocos2d::CCObject*);
    void onClearAll(cocos2d::CCObject*);
    void onDeleteFrame(cocos2d::CCObject* sender);
    void onOpenPrecision(cocos2d::CCObject*);
    void onToggleMod(cocos2d::CCObject* sender);
    void onSelectAll(cocos2d::CCObject*);
    void onDeselectAll(cocos2d::CCObject*);
    void onPrevPage(cocos2d::CCObject*);
    void onNextPage(cocos2d::CCObject*);

    void refreshList(bool rebuildKeys = false);
    cocos2d::CCNode* createCellTemplate(int index);
    void onToggleDraw(cocos2d::CCObject* sender);
    void onTogglePlayer(cocos2d::CCObject* sender);

    void onSwitchToLabels(cocos2d::CCObject*);
    void onSwitchToWindows(cocos2d::CCObject*);
    void onOpenDisplay(cocos2d::CCObject*);
    void updateCellPercents();
    void onExportFWC(cocos2d::CCObject*);
    void onImportGDR(cocos2d::CCObject*);
    void onMergeFrames(cocos2d::CCObject* sender);
    void mergeDuplicateFrames();

public:
    static FrameActionPopup* create();
    void showInstant();
    void instantClose();

    void jumpToFrame(int targetFrame);
    void reloadFromState();            // 外部（快捷键撤销等）修改了动作列表后刷新，保持滚动位置
    void doTrackingTick(int currentFrame);
    void highlightCell(int targetFrame);
    void scrollToCell(int indexInPage);
};
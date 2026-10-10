#pragma once
#include <Geode/Geode.hpp>

// 按"窗口大小"设置缩放一个 Mod 弹窗。
// 默认倍率 (1.0) 下不会改变任何原有布局；放大时会被限制在屏幕范围内，避免弹窗被推出屏幕。
// 必须在 show() 之后、并且在 stopAllActions()/setScale(1) 之后调用（各弹窗的 showInstant() 已处理）。
void applyWindowScale(FLAlertLayer* popup);

// 把当前"窗口大小"实时应用到场景中所有已打开的 Mod 弹窗（用于滑条拖动时的实时预览）。
// except：不处理的弹窗（滑条所在的弹窗自身，拖动时缩放它会让滑块脱离手指）。
void applyWindowScaleToOpenModPopups(cocos2d::CCNode* except = nullptr);

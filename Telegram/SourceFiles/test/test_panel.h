/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtWidgets/QWidget>

namespace Test {

// A grab taken while Ui::SeparatePanel is still showing is not the
// panel's children: toggleOpacityAnimation() copies the widget into
// _animationCache, calls hideChildren(), and paintEvent draws that
// cache into rect().marginsRemoved(...) at reduced opacity, with
// marginRatio = (1 - opacity) / 5. The content region then holds a
// squeezed, faded copy of the whole frame — or nothing — so a pixel
// check reads "the widget painted nothing" even though every logged
// geometry rect is identical to a settled grab. The layout never
// moved; the frame did.
//
// PanelShowSettled answers when that cache has stopped painting. It
// returns only after the panel's own hideChildren() state has cleared
// (every direct QWidget child shown again, or the widget was never
// in that state). Elapsed time is not the contract. At its deadline
// it FAILs, naming the panel, the observed state, and deadlineMs,
// and never returns a cache frame as success. After it returns,
// a blank content grab is "painted nothing"; a
// PANEL_SHOW_SETTLE settled=0 / panel show settle FAIL is
// "captured during the show animation".

enum class PanelShowState {
	Hidden,
	ShowCache,
	Live,
};

[[nodiscard]] QString PanelShowStateName(PanelShowState state);

[[nodiscard]] PanelShowState ReadPanelShowState(not_null<QWidget*> panel);

[[nodiscard]] bool PanelShowSettled(
	const QString &name,
	not_null<QWidget*> panel,
	crl::time deadline);

} // namespace Test

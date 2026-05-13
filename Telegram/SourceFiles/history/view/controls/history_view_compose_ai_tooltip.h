/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"
#include "base/unique_qptr.h"

namespace Ui {
class ImportantTooltip;
class RpWidget;
} // namespace Ui

namespace HistoryView::Controls {

class ComposeTooltipManager final {
public:
	ComposeTooltipManager(
		not_null<QWidget*> parent,
		not_null<Ui::RpWidget*> button,
		rpl::producer<TextWithEntities> text,
		base::const_string prefKey,
		Fn<int()> widthProvider);

	void hideAndRemember();
	void updateVisibility(bool buttonShown);
	void updateGeometry();
	void raise();

private:
	const not_null<Ui::RpWidget*> _button;
	const base::const_string _prefKey;
	const Fn<int()> _widthProvider;
	base::unique_qptr<Ui::ImportantTooltip> _tooltip;
	bool _shown = false;

};

using AiTooltipManager = ComposeTooltipManager;

} // namespace HistoryView::Controls

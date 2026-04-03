/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Ui {
class ImportantTooltip;
class RpWidget;
} // namespace Ui

namespace HistoryView::Controls {

class ComposeAiButton;

class AiTooltipManager final {
public:
	AiTooltipManager(
		not_null<QWidget*> parent,
		not_null<ComposeAiButton*> button,
		Fn<int()> widthProvider);

	void hideAndRemember();
	void updateVisibility(bool buttonShown);
	void updateGeometry();
	void raise();

private:
	const not_null<ComposeAiButton*> _button;
	const Fn<int()> _widthProvider;
	base::unique_qptr<Ui::ImportantTooltip> _tooltip;
	bool _shown = false;

};

} // namespace HistoryView::Controls

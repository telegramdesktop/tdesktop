/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/labels.h"

namespace HistoryView::Controls {

class CharactersLimitLabel final : public Ui::FlatLabel {
public:
	CharactersLimitLabel(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> widgetToAlign,
		style::align align);

	void setLeft(int value);

};

} // namespace HistoryView::Controls

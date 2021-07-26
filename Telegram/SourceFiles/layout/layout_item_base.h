/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "layout/abstract_layout_item.h"

namespace HistoryView {
struct TextState;
struct StateRequest;
} // namespace HistoryView

class LayoutItemBase : public AbstractLayoutItem {
public:
	using TextState = HistoryView::TextState;
	using StateRequest = HistoryView::StateRequest;

	using AbstractLayoutItem::AbstractLayoutItem;

	virtual void initDimensions() = 0;

	[[nodiscard]] virtual TextState getState(
		QPoint point,
		StateRequest request) const;
	[[nodiscard]] virtual TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const;

};

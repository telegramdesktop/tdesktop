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

namespace Ui {
enum CachedRoundCorners : int;
} // namespace Ui

constexpr auto FullSelection = TextSelection { 0xFFFF, 0xFFFF };

inline bool IsSubGroupSelection(TextSelection selection) {
	return (selection.from == 0xFFFF) && (selection.to != 0xFFFF);
}

inline bool IsGroupItemSelection(
		TextSelection selection,
		int index) {
	Expects(index >= 0 && index < 0x0F);

	return IsSubGroupSelection(selection) && (selection.to & (1 << index));
}

[[nodiscard]] inline TextSelection AddGroupItemSelection(
		TextSelection selection,
		int index) {
	Expects(index >= 0 && index < 0x0F);

	const auto bit = uint16(1U << index);
	return TextSelection(
		0xFFFF,
		IsSubGroupSelection(selection) ? (selection.to | bit) : bit);
}

[[nodiscard]] inline TextSelection RemoveGroupItemSelection(
		TextSelection selection,
		int index) {
	Expects(index >= 0 && index < 0x0F);

	const auto bit = uint16(1U << index);
	return IsSubGroupSelection(selection)
		? TextSelection(0xFFFF, selection.to & ~bit)
		: selection;
}

int32 documentColorIndex(DocumentData *document, QString &ext);
style::color documentColor(int colorIndex);
style::color documentDarkColor(int colorIndex);
style::color documentOverColor(int colorIndex);
style::color documentSelectedColor(int colorIndex);
Ui::CachedRoundCorners documentCorners(int colorIndex);

class PaintContextBase {
public:
	PaintContextBase(crl::time ms, bool selecting) : ms(ms), selecting(selecting) {
	}
	crl::time ms;
	bool selecting;

};

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

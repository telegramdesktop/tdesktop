/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/runtime_composer.h"

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

class LayoutItemBase
	: public RuntimeComposer<LayoutItemBase>
	, public ClickHandlerHost {
public:
	using TextState = HistoryView::TextState;
	using StateRequest = HistoryView::StateRequest;

	LayoutItemBase() {
	}

	LayoutItemBase(const LayoutItemBase &other) = delete;
	LayoutItemBase &operator=(const LayoutItemBase &other) = delete;

	int maxWidth() const {
		return _maxw;
	}
	int minHeight() const {
		return _minh;
	}
	virtual void initDimensions() = 0;
	virtual int resizeGetHeight(int width) {
		_width = qMin(width, _maxw);
		_height = _minh;
		return _height;
	}

	[[nodiscard]] virtual TextState getState(
		QPoint point,
		StateRequest request) const;
	[[nodiscard]] virtual TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const;

	int width() const {
		return _width;
	}
	int height() const {
		return _height;
	}

	bool hasPoint(QPoint point) const {
		return QRect(0, 0, width(), height()).contains(point);
	}

	virtual ~LayoutItemBase() {
	}

protected:
	int _width = 0;
	int _height = 0;
	int _maxw = 0;
	int _minh = 0;

};

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"

namespace Data {
struct Giveaway;
} // namespace Data

namespace HistoryView {

class TextRows final {
public:
	void add(Ui::Text::String text, int skipTop);

	[[nodiscard]] bool isEmpty() const;
	[[nodiscard]] int maxWidth() const;
	[[nodiscard]] int minHeight() const;

	[[nodiscard]] int countHeight(int newWidth) const;

	[[nodiscard]] int length() const;

private:
	struct Row {
		Ui::Text::String text;
		int skipTop = 0;
	};
	std::vector<Row> _rows;
};

[[nodiscard]] TextSelection UnshiftItemSelection(
	TextSelection selection,
	const TextRows &byText);
[[nodiscard]] TextSelection ShiftItemSelection(
	TextSelection selection,
	const TextRows &byText);

class Giveaway final : public Media {
public:
	Giveaway(
		not_null<Element*> parent,
		not_null<Data::Giveaway*> giveaway);
	~Giveaway();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	bool toggleSelectionByHandlerClick(
			const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override;
	TextForMimeData selectedText(TextSelection selection) const override;

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

private:
	struct Channel {
		Ui::Text::String name;
	};

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void fillFromData(not_null<Data::Giveaway*> giveaway);

	TextSelection toDateSelection(TextSelection selection) const;
	TextSelection fromDateSelection(TextSelection selection) const;
	QMargins inBubblePadding() const;

	TextRows _rows;
	std::vector<Channel> _channels;
	TextRows _date;
	int _rowsHeight = 0;
	int _channelsHeight = 0;
	int _dateHeight = 0;

};

} // namespace HistoryView

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_giveaway.h"

#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_media_types.h"
#include "lang/lang_keys.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_utilities.h"
#include "styles/style_chat.h"

namespace HistoryView {

void TextRows::add(Ui::Text::String text, int skipTop) {
}

bool TextRows::isEmpty() const {
	return _rows.empty()
		|| (_rows.size() == 1 && _rows.front().text.isEmpty());
}

int TextRows::maxWidth() const {
	return 0;
}

int TextRows::minHeight() const {
	return 0;
}

int TextRows::countHeight(int newWidth) const {
	return 0;
}

int TextRows::length() const {
	return 0;
}

TextSelection UnshiftItemSelection(
		TextSelection selection,
		const TextRows &byText) {
	return UnshiftItemSelection(selection, byText.length());
}

TextSelection ShiftItemSelection(
		TextSelection selection,
		const TextRows &byText) {
	return ShiftItemSelection(selection, byText.length());
}

Giveaway::Giveaway(
	not_null<Element*> parent,
	not_null<Data::Giveaway*> giveaway)
: Media(parent) {
	fillFromData(giveaway);
}

Giveaway::~Giveaway() = default;

void Giveaway::fillFromData(not_null<Data::Giveaway*> giveaway) {
	_rows.add(Ui::Text::String(
		st::semiboldTextStyle,
		tr::lng_prizes_title(tr::now, lt_count, giveaway->quantity),
		kDefaultTextOptions,
		st::msgMinWidth), st::chatGiveawayPrizesTop);

	const auto duration = (giveaway->months < 12)
		? tr::lng_months(tr::now, lt_count, giveaway->months)
		: tr::lng_years(tr::now, lt_count, giveaway->months / 12);
	_rows.add(Ui::Text::String(
		st::defaultTextStyle,
		tr::lng_prizes_about(
			tr::now,
			lt_count,
			giveaway->quantity,
			lt_duration,
			Ui::Text::Bold(duration),
			Ui::Text::RichLangValue),
		kDefaultTextOptions,
		st::msgMinWidth), st::chatGiveawayPrizesSkip);

	_rows.add(Ui::Text::String(
		st::semiboldTextStyle,
		tr::lng_prizes_participants(tr::now),
		kDefaultTextOptions,
		st::msgMinWidth), st::chatGiveawayParticipantsTop);

	for (const auto &channel : giveaway->channels) {
		_channels.push_back({ Ui::Text::String(
			st::semiboldTextStyle,
			channel->name(),
			kDefaultTextOptions,
			st::msgMinWidth)
		});
	}

	const auto channels = int(_channels.size());
	_rows.add(Ui::Text::String(
		st::defaultTextStyle,
		(giveaway->all
			? tr::lng_prizes_participants_all
			: tr::lng_prizes_participants_new)(tr::now, lt_count, channels),
		kDefaultTextOptions,
		st::msgMinWidth), st::chatGiveawayParticipantsSkip);

	_date.add(Ui::Text::String(
		st::semiboldTextStyle,
		tr::lng_prizes_date(tr::now),
		kDefaultTextOptions,
		st::msgMinWidth), st::chatGiveawayDateTop);

	_rows.add(Ui::Text::String(
		st::defaultTextStyle,
		langDateTime(base::unixtime::parse(giveaway->untilDate)),
		kDefaultTextOptions,
		st::msgMinWidth), st::chatGiveawayDateSkip);
}

QSize Giveaway::countOptimalSize() {
	// init dimensions
	auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	accumulate_max(maxWidth, _rows.maxWidth());
	minHeight += _rows.minHeight();

	//minHeight +=

	accumulate_max(maxWidth, _date.maxWidth());
	minHeight += _date.minHeight();

	auto padding = inBubblePadding();
	maxWidth += padding.left() + padding.right();
	minHeight += padding.top() + padding.bottom();
	return { maxWidth, minHeight };
}

QSize Giveaway::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	auto innerWidth = newWidth
		- st::msgPadding.left()
		- st::msgPadding.right();

	auto newHeight = 0;
	_rowsHeight = _rows.countHeight(innerWidth);
	newHeight += _rowsHeight;

	//newHeight +=

	newHeight += _date.countHeight(innerWidth);
	_dateHeight = _date.minHeight();
	newHeight += _dateHeight;

	auto padding = inBubblePadding();
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection Giveaway::toDateSelection(TextSelection selection) const {
	return UnshiftItemSelection(selection, _rows);
}

TextSelection Giveaway::fromDateSelection(TextSelection selection) const {
	return ShiftItemSelection(selection, _rows);
}

void Giveaway::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	auto &semibold = stm->msgServiceFg;

	auto padding = inBubblePadding();
	auto tshift = padding.top();

	//_rows.draw(p, {
	//	.position = { padding.left(), tshift },
	//	.outerWidth = width(),
	//	.availableWidth = paintw,
	//	.now = context.now,
	//	.selection = context.selection,
	//});
	//tshift += _rows.countHeight(paintw);

	//_date.draw(p, {
	//	.position = { padding.left(), tshift },
	//	.outerWidth = width(),
	//	.availableWidth = paintw,
	//	.now = context.now,
	//	.selection = toDateSelection(context.selection),
	//});
}

TextState Giveaway::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();

	auto symbolAdd = 0;
	if (_rowsHeight > 0) {
		if (point.y() >= tshift && point.y() < tshift + _rowsHeight) {
			//result = TextState(_parent, _rows.getState(
			//	point - QPoint(padding.left(), tshift),
			//	paintw,
			//	width(),
			//	request.forText()));
		} else if (point.y() >= tshift + _rowsHeight) {
			symbolAdd += _rows.length();
		}
		tshift += _rowsHeight;
	}
	if (_channelsHeight > 0) {
		tshift += _channelsHeight;
	}
	if (_dateHeight > 0) {
		if (point.y() >= tshift && point.y() < tshift + _dateHeight) {
			//result = TextState(_parent, _date.getState(
			//	point - QPoint(padding.left(), tshift),
			//	paintw,
			//	width(),
			//	request.forText()));
		} else if (point.y() >= tshift + _dateHeight) {
			symbolAdd += _date.length();
		}
		tshift += _dateHeight;
	}
	result.symbol += symbolAdd;
	return result;
}

TextSelection Giveaway::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	//if (_date.isEmpty() || selection.to <= _rows.length()) {
	//	return _rows.adjustSelection(selection, type);
	//}
	//const auto dateSelection = _date.adjustSelection(
	//	toDateSelection(selection),
	//	type);
	//if (selection.from >= _rows.length()) {
	//	return fromDateSelection(dateSelection);
	//}
	//const auto rowsSelection = _rows.adjustSelection(selection, type);
	//return { rowsSelection.from, fromDateSelection(dateSelection).to };
	return selection;
}

bool Giveaway::hasHeavyPart() const {
	return false;
}

void Giveaway::unloadHeavyPart() {
}

uint16 Giveaway::fullSelectionLength() const {
	return 0;
}

TextForMimeData Giveaway::selectedText(TextSelection selection) const {
	//auto rowsResult = _rows.toTextForMimeData(selection);
	//auto dateResult = _date.toTextForMimeData(toDateSelection(selection));
	//if (rowsResult.empty()) {
	//	return dateResult;
	//} else if (dateResult.empty()) {
	//	return rowsResult;
	//}
	//return rowsResult.append('\n').append(std::move(dateResult));
	return {};
}

QMargins Giveaway::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.top() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.bottom() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

} // namespace HistoryView

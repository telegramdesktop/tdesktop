/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_media.h"

#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "storage/storage_shared_media.h"
#include "ui/text_options.h"
#include "styles/style_history.h"

namespace {

using PointState = HistoryView::PointState;
using TextState = HistoryView::TextState;

} // namespace

Storage::SharedMediaTypesMask HistoryMedia::sharedMediaTypes() const {
	return {};
}

bool HistoryMedia::isDisplayed() const {
	return true;
}

QSize HistoryMedia::countCurrentSize(int newWidth) {
	return QSize(qMin(newWidth, maxWidth()), minHeight());
}

Text HistoryMedia::createCaption(not_null<HistoryItem*> item) const {
	if (item->emptyText()) {
		return Text();
	}
	const auto minResizeWidth = st::minPhotoSize
		- st::msgPadding.left()
		- st::msgPadding.right();
	auto result = Text(minResizeWidth);
	result.setMarkedText(
		st::messageTextStyle,
		item->originalText(),
		Ui::ItemTextOptions(item));
	if (const auto width = _parent->skipBlockWidth()) {
		result.updateSkipBlock(width, _parent->skipBlockHeight());
	}
	return result;
}

TextSelection HistoryMedia::skipSelection(TextSelection selection) const {
	return HistoryView::UnshiftItemSelection(
		selection,
		fullSelectionLength());
}

TextSelection HistoryMedia::unskipSelection(TextSelection selection) const {
	return HistoryView::ShiftItemSelection(
		selection,
		fullSelectionLength());
}

PointState HistoryMedia::pointState(QPoint point) const {
	return QRect(0, 0, width(), height()).contains(point)
		? PointState::Inside
		: PointState::Outside;
}

TextState HistoryMedia::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const {
	Unexpected("Grouping method call.");
}

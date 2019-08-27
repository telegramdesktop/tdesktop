/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media.h"

#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "storage/storage_shared_media.h"
#include "ui/text_options.h"
#include "styles/style_history.h"

namespace HistoryView {

Storage::SharedMediaTypesMask Media::sharedMediaTypes() const {
	return {};
}

not_null<History*> Media::history() const {
	return _parent->history();
}

bool Media::isDisplayed() const {
	return true;
}

QSize Media::countCurrentSize(int newWidth) {
	return QSize(qMin(newWidth, maxWidth()), minHeight());
}

Ui::Text::String Media::createCaption(not_null<HistoryItem*> item) const {
	if (item->emptyText()) {
		return {};
	}
	const auto minResizeWidth = st::minPhotoSize
		- st::msgPadding.left()
		- st::msgPadding.right();
	auto result = Ui::Text::String(minResizeWidth);
	result.setMarkedText(
		st::messageTextStyle,
		item->originalText(),
		Ui::ItemTextOptions(item));
	if (const auto width = _parent->skipBlockWidth()) {
		result.updateSkipBlock(width, _parent->skipBlockHeight());
	}
	return result;
}

TextSelection Media::skipSelection(TextSelection selection) const {
	return UnshiftItemSelection(selection, fullSelectionLength());
}

TextSelection Media::unskipSelection(TextSelection selection) const {
	return ShiftItemSelection(selection, fullSelectionLength());
}

PointState Media::pointState(QPoint point) const {
	return QRect(0, 0, width(), height()).contains(point)
		? PointState::Inside
		: PointState::Outside;
}

TextState Media::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const {
	Unexpected("Grouping method call.");
}

} // namespace HistoryView

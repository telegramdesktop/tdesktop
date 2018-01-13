/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_media.h"

#include "history/history_item.h"
#include "storage/storage_shared_media.h"

Storage::SharedMediaTypesMask HistoryMedia::sharedMediaTypes() const {
	return {};
}

bool HistoryMedia::isDisplayed() const {
	return !_parent->isHiddenByGroup();
}

QSize HistoryMedia::countCurrentSize(int newWidth) {
	return QSize(qMin(newWidth, maxWidth()), minHeight());
}

TextSelection HistoryMedia::skipSelection(TextSelection selection) const {
	return internal::unshiftSelection(selection, fullSelectionLength());
}

TextSelection HistoryMedia::unskipSelection(TextSelection selection) const {
	return internal::shiftSelection(selection, fullSelectionLength());
}

HistoryTextState HistoryMedia::getStateGrouped(
		const QRect &geometry,
		QPoint point,
		HistoryStateRequest request) const {
	Unexpected("Grouping method call.");
}

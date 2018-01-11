/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_media_pointer.h"

#include "history/history_media.h"

HistoryMediaPtr::HistoryMediaPtr() = default;

HistoryMediaPtr::HistoryMediaPtr(std::unique_ptr<HistoryMedia> pointer)
: _pointer(std::move(pointer)) {
	if (_pointer) {
		_pointer->attachToParent();
	}
}

void HistoryMediaPtr::reset(std::unique_ptr<HistoryMedia> pointer) {
	*this = std::move(pointer);
}

HistoryMediaPtr &HistoryMediaPtr::operator=(std::unique_ptr<HistoryMedia> pointer) {
	if (_pointer) {
		_pointer->detachFromParent();
	}
	_pointer = std::move(pointer);
	if (_pointer) {
		_pointer->attachToParent();
	}
	return *this;
}

HistoryMediaPtr::~HistoryMediaPtr() {
	reset();
}

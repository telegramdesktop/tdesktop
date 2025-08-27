/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_translation.h"

#include "data/data_changes.h"
#include "history/history.h"
#include "main/main_session.h"

namespace {

using UpdateFlag = Data::HistoryUpdate::Flag;

} // namespace

HistoryTranslation::HistoryTranslation(
	not_null<History*> history,
	const LanguageId &offerFrom)
: _history(history) {
	this->offerFrom(offerFrom);
}

void HistoryTranslation::offerFrom(LanguageId id) {
	if (_offerFrom == id) {
		return;
	}
	_offerFrom = id;
	auto &changes = _history->session().changes();
	changes.historyUpdated(_history, UpdateFlag::TranslateFrom);
}

LanguageId HistoryTranslation::offeredFrom() const {
	return _offerFrom;
}

void HistoryTranslation::translateTo(LanguageId id) {
	if (_translatedTo == id) {
		return;
	}
	_translatedTo = id;
	auto &changes = _history->session().changes();
	changes.historyUpdated(_history, UpdateFlag::TranslatedTo);
}

LanguageId HistoryTranslation::translatedTo() const {
	return _translatedTo;
}

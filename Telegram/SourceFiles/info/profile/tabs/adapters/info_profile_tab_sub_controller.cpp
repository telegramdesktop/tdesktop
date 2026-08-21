/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_sub_controller.h"

#include "main/main_session.h"

namespace Info::Profile {

MediaSubController::MediaSubController(
	not_null<AbstractController*> parent,
	Storage::SharedMediaType mediaType,
	PeerData *migrated,
	bool withSearch)
: AbstractController(parent->parentController())
, _parent(parent)
, _key(parent->key())
, _section(mediaType)
, _migrated(migrated) {
	if (withSearch) {
		_search = std::make_unique<Api::DelayedSearchController>(
			&session());
		_search->setQueryFast(produceSearchQuery(QString()));
	}
}

MediaSubController::~MediaSubController() = default;

Key MediaSubController::key() const {
	return _key;
}

style::color MediaSubController::listBackground() const {
	return _parent->listBackground();
}

PeerData *MediaSubController::migrated() const {
	return _migrated;
}

::Info::Section MediaSubController::section() const {
	return _section;
}

rpl::producer<SparseIdsMergedSlice> MediaSubController::mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const {
	if (_search && !_search->currentQuery().query.isEmpty()) {
		return _search->idsSlice(aroundId, limitBefore, limitAfter);
	}
	return AbstractController::mediaSource(
		aroundId,
		limitBefore,
		limitAfter);
}

rpl::producer<QString> MediaSubController::mediaSourceQueryValue() const {
	return _search
		? _search->currentQueryValue()
		: AbstractController::mediaSourceQueryValue();
}

void MediaSubController::applySearchQuery(const QString &query) {
	if (_search) {
		_search->setQuery(produceSearchQuery(query));
	}
}

} // namespace Info::Profile

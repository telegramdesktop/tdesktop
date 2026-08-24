/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_search_controller.h"
#include "info/info_controller.h"

namespace Info::Profile {

class MediaSubController final : public AbstractController {
public:
	MediaSubController(
		not_null<AbstractController*> parent,
		Storage::SharedMediaType mediaType,
		PeerData *migrated,
		bool withSearch);
	~MediaSubController();

	Key key() const override;
	PeerData *migrated() const override;
	::Info::Section section() const override;
	style::color listBackground() const override;

	rpl::producer<SparseIdsMergedSlice> mediaSource(
		SparseIdsMergedSlice::UniversalMsgId aroundId,
		int limitBefore,
		int limitAfter) const override;
	rpl::producer<QString> mediaSourceQueryValue() const override;

	void applySearchQuery(const QString &query);

private:
	using SearchQuery = Api::DelayedSearchController::Query;

	[[nodiscard]] SearchQuery produceSearchQuery(
		const QString &query) const;

	const not_null<AbstractController*> _parent;
	const Key _key;
	const ::Info::Section _section;
	PeerData * const _migrated = nullptr;
	std::unique_ptr<Api::DelayedSearchController> _search;

};

} // namespace Info::Profile

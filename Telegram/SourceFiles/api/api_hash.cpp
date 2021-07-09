/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_hash.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "main/main_session.h"

namespace Api {
namespace {

[[nodiscard]] int32 CountDocumentVectorHash(
		const QVector<DocumentData*> vector) {
	auto result = HashInit();
	for (const auto document : vector) {
		HashUpdate(result, document->id);
	}
	return HashFinalize(result);
}

[[nodiscard]] int32 CountSpecialStickerSetHash(
		not_null<Main::Session*> session,
		uint64 setId) {
	const auto &sets = session->data().stickers().sets();
	const auto it = sets.find(setId);
	if (it != sets.cend()) {
		return CountDocumentVectorHash(it->second->stickers);
	}
	return 0;
}

} // namespace

int32 CountStickersHash(
		not_null<Main::Session*> session,
		bool checkOutdatedInfo) {
	using Flag = Data::StickersSetFlag;
	auto result = HashInit();
	bool foundOutdated = false;
	const auto &sets = session->data().stickers().sets();
	const auto &order = session->data().stickers().setsOrder();
	for (auto i = order.cbegin(), e = order.cend(); i != e; ++i) {
		auto it = sets.find(*i);
		if (it != sets.cend()) {
			const auto set = it->second.get();
			if (set->id == Data::Stickers::DefaultSetId) {
				foundOutdated = true;
			} else if (!(set->flags & Flag::Special)
				&& !(set->flags & Flag::Archived)) {
				HashUpdate(result, set->hash);
			}
		}
	}
	return (!checkOutdatedInfo || !foundOutdated)
		? HashFinalize(result)
		: 0;
}

int32 CountRecentStickersHash(
		not_null<Main::Session*> session,
		bool attached) {
	return CountSpecialStickerSetHash(
		session,
		attached
			? Data::Stickers::CloudRecentAttachedSetId
			: Data::Stickers::CloudRecentSetId);
}

int32 CountFavedStickersHash(not_null<Main::Session*> session) {
	return CountSpecialStickerSetHash(session, Data::Stickers::FavedSetId);
}

int32 CountFeaturedStickersHash(not_null<Main::Session*> session) {
	auto result = HashInit();
	const auto &sets = session->data().stickers().sets();
	const auto &featured = session->data().stickers().featuredSetsOrder();
	for (const auto setId : featured) {
		HashUpdate(result, setId);

		const auto it = sets.find(setId);
		if (it != sets.cend()
			&& (it->second->flags & Data::StickersSetFlag::Unread)) {
			HashUpdate(result, 1);
		}
	}
	return HashFinalize(result);
}

int32 CountSavedGifsHash(not_null<Main::Session*> session) {
	return CountDocumentVectorHash(session->data().stickers().savedGifs());
}

} // namespace Api

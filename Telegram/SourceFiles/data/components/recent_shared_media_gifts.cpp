/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_shared_media_gifts.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kReloadThreshold = 60 * crl::time(1000);
constexpr auto kMaxGifts = 3;

} // namespace

RecentSharedMediaGifts::RecentSharedMediaGifts(
	not_null<Main::Session*> session)
: _session(session) {
}

RecentSharedMediaGifts::~RecentSharedMediaGifts() = default;

void RecentSharedMediaGifts::request(
		not_null<PeerData*> peer,
		Fn<void(std::vector<DocumentId>)> done) {
	const auto it = _recent.find(peer->id);
	if (it != _recent.end()) {
		auto &entry = it->second;
		if (entry.lastRequestTime
			&& entry.lastRequestTime + kReloadThreshold > crl::now()) {
			done(std::vector<DocumentId>(entry.ids.begin(), entry.ids.end()));
			return;
		}
		if (entry.requestId) {
			peer->session().api().request(entry.requestId).cancel();
		}
	}

	_recent[peer->id].requestId = peer->session().api().request(
		MTPpayments_GetSavedStarGifts(
			MTP_flags(0),
			peer->input,
			MTP_string(QString()),
			MTP_int(kMaxGifts)
	)).done([=](const MTPpayments_SavedStarGifts &result) {
		const auto &data = result.data();
		const auto owner = &peer->owner();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());
		auto &entry = _recent[peer->id];
		entry.lastRequestTime = crl::now();
		entry.requestId = 0;

		auto conter = 0;
		for (const auto &gift : data.vgifts().v) {
			if (auto parsed = Api::FromTL(peer, gift)) {
				entry.ids.push_front(parsed->info.document->id);
				if (entry.ids.size() > kMaxGifts) {
					entry.ids.pop_back();
				}
				if (++conter >= kMaxGifts) {
					break;
				}
			}
		}
		done(std::vector<DocumentId>(entry.ids.begin(), entry.ids.end()));
	}).send();
}

} // namespace Data

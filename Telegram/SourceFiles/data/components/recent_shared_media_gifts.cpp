/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/recent_shared_media_gifts.h"

#include "api/api_credits.h" // InputSavedStarGiftId
#include "api/api_premium.h"
#include "apiwrap.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"

namespace Data {
namespace {

constexpr auto kReloadThreshold = 60 * crl::time(1000);
constexpr auto kMaxGifts = 3;
constexpr auto kMaxPinnedGifts = 6;

} // namespace

RecentSharedMediaGifts::RecentSharedMediaGifts(
	not_null<Main::Session*> session)
: _session(session) {
}

RecentSharedMediaGifts::~RecentSharedMediaGifts() = default;

std::vector<Data::SavedStarGift> RecentSharedMediaGifts::filterGifts(
		const std::deque<Data::SavedStarGift> &gifts,
		bool onlyPinnedToTop) {
	auto result = std::vector<Data::SavedStarGift>();
	const auto maxCount = onlyPinnedToTop ? kMaxPinnedGifts : kMaxGifts;
	for (const auto &gift : gifts) {
		if (!onlyPinnedToTop || gift.pinned) {
			result.push_back(gift);
			if (result.size() >= maxCount) {
				break;
			}
		}
	}
	return result;
}

void RecentSharedMediaGifts::request(
		not_null<PeerData*> peer,
		Fn<void(std::vector<SavedStarGift>)> done,
		bool onlyPinnedToTop) {
	const auto it = _recent.find(peer->id);
	if (it != _recent.end()) {
		auto &entry = it->second;
		if (entry.lastRequestTime
			&& entry.lastRequestTime + kReloadThreshold > crl::now()) {
			done(filterGifts(entry.gifts, onlyPinnedToTop));
			return;
		}
		if (entry.requestId) {
			entry.pendingCallbacks.push_back([=] {
				const auto it = _recent.find(peer->id);
				if (it != _recent.end()) {
					done(filterGifts(it->second.gifts, onlyPinnedToTop));
				}
			});
			return;
		}
	}

	_recent[peer->id].requestId = peer->session().api().request(
		MTPpayments_GetSavedStarGifts(
			MTP_flags(0),
			peer->input(),
			MTP_int(0), // collection_id
			MTP_string(QString()),
			MTP_int(kMaxPinnedGifts)
	)).done([=](const MTPpayments_SavedStarGifts &result) {
		const auto &data = result.data();
		const auto owner = &peer->owner();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());
		auto &entry = _recent[peer->id];
		entry.lastRequestTime = crl::now();
		entry.requestId = 0;
		entry.gifts.clear();

		for (const auto &gift : data.vgifts().v) {
			if (auto parsed = Api::FromTL(peer, gift)) {
				entry.gifts.push_back(std::move(*parsed));
			}
		}

		done(filterGifts(entry.gifts, onlyPinnedToTop));
		for (const auto &callback : entry.pendingCallbacks) {
			callback();
		}
		entry.pendingCallbacks.clear();
	}).send();
}

void RecentSharedMediaGifts::clearLastRequestTime(
		not_null<PeerData*> peer) {
	const auto it = _recent.find(peer->id);
	if (it != _recent.end()) {
		it->second.lastRequestTime = 0;
	}
}

void RecentSharedMediaGifts::updatePinnedOrder(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		const std::vector<SavedStarGift> &gifts,
		const std::vector<Data::SavedStarGiftId> &manageIds,
		Fn<void()> done) {
	auto inputs = QVector<MTPInputSavedStarGift>();
	inputs.reserve(manageIds.size());
	for (const auto &id : manageIds) {
		inputs.push_back(Api::InputSavedStarGiftId(id));
	}

	_session->api().request(MTPpayments_ToggleStarGiftsPinnedToTop(
		peer->input(),
		MTP_vector<MTPInputSavedStarGift>(std::move(inputs))
	)).done([=] {
		auto result = std::deque<SavedStarGift>();
		for (const auto &id : manageIds) {
			for (const auto &gift : gifts) {
				if (gift.manageId == id) {
					result.push_back(gift);
					break;
				}
			}
		}
		_recent[peer->id].gifts = std::move(result);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		show->showToast(error.type());
	}).send();
}

void RecentSharedMediaGifts::togglePinned(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		const Data::SavedStarGiftId &manageId,
		bool pinned,
		std::shared_ptr<Data::UniqueGift> uniqueData,
		std::shared_ptr<Data::UniqueGift> replacingData) {
	const auto performToggle = [=](const std::vector<SavedStarGift> &gifts) {
		const auto limit = _session->appConfig().pinnedGiftsLimit();
		auto manageIds = std::vector<Data::SavedStarGiftId>();

		if (pinned) {
			for (const auto &gift : gifts) {
				if (gift.pinned && gift.manageId != manageId) {
					manageIds.push_back(gift.manageId);
					if (manageIds.size() >= limit - 1) {
						break;
					}
				}
			}
			manageIds.push_back(manageId);
		} else {
			for (const auto &gift : gifts) {
				if (gift.pinned && gift.manageId != manageId) {
					manageIds.push_back(gift.manageId);
				}
			}
		}

		const auto updateLocal = [=] {
				using GiftAction = Data::GiftUpdate::Action;
				_session->data().notifyGiftUpdate({
					.id = manageId,
					.action = (pinned ? GiftAction::Pin : GiftAction::Unpin),
				});
				if (pinned) {
					show->showToast({
						.title = (uniqueData
							? tr::lng_gift_pinned_done_title(
								tr::now,
								lt_gift,
								Data::UniqueGiftName(*uniqueData))
							: QString()),
						.text = (replacingData
							? tr::lng_gift_pinned_done_replaced(
								tr::now,
								lt_gift,
								TextWithEntities{
									Data::UniqueGiftName(*replacingData),
								},
								tr::marked)
							: tr::lng_gift_pinned_done(
								tr::now,
								tr::marked)),
						.duration = Ui::Toast::kDefaultDuration * 2,
					});
				}
			};

			if (!pinned) {
				updatePinnedOrder(show, peer, gifts, manageIds, updateLocal);
			} else {
				_session->api().request(MTPpayments_GetSavedStarGift(
					MTP_vector<MTPInputSavedStarGift>(
						1,
						Api::InputSavedStarGiftId(manageId))
				)).done([=](const MTPpayments_SavedStarGifts &result) {
					const auto &tlGift = result.data().vgifts().v.front();
					if (auto parsed = Api::FromTL(peer, tlGift)) {
						auto updatedGifts = std::vector<SavedStarGift>();
						for (const auto &gift : gifts) {
							if (gift.pinned && gift.manageId != manageId) {
								updatedGifts.push_back(gift);
							}
						}
						parsed->pinned = true;
						updatedGifts.push_back(*parsed);
						updatePinnedOrder(
							show,
							peer,
							updatedGifts,
							manageIds,
							updateLocal);
					}
				}).send();
			}
	};

	request(peer, performToggle, true);
}

void RecentSharedMediaGifts::reorderPinned(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		int oldPosition,
		int newPosition) {
	const auto performReorder = [=](const std::vector<SavedStarGift> &gifts) {
		if (oldPosition < 0 || oldPosition >= gifts.size()
			|| newPosition < 0 || newPosition >= gifts.size()
			|| oldPosition == newPosition) {
			return;
		}

		auto manageIds = std::vector<Data::SavedStarGiftId>();
		manageIds.reserve(gifts.size());
		for (const auto &gift : gifts) {
			manageIds.push_back(gift.manageId);
		}
		base::reorder(manageIds, oldPosition, newPosition);

		updatePinnedOrder(show, peer, gifts, manageIds, nullptr);
	};

	request(peer, performReorder, true);
}

} // namespace Data

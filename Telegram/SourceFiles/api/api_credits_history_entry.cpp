/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits_history_entry.h"

#include "api/api_premium.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_credits.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Api {

Data::CreditsHistoryEntry CreditsHistoryEntryFromTL(
		const MTPStarsTransaction &tl,
		not_null<PeerData*> peer) {
	using HistoryPeerTL = MTPDstarsTransactionPeer;
	using namespace Data;
	const auto owner = &peer->owner();
	const auto photo = tl.data().vphoto()
		? owner->photoFromWeb(*tl.data().vphoto(), ImageLocation())
		: nullptr;
	auto extended = std::vector<CreditsHistoryMedia>();
	if (const auto list = tl.data().vextended_media()) {
		extended.reserve(list->v.size());
		for (const auto &media : list->v) {
			media.match([&](const MTPDmessageMediaPhoto &data) {
				if (const auto inner = data.vphoto()) {
					const auto photo = owner->processPhoto(*inner);
					if (!photo->isNull()) {
						extended.push_back(CreditsHistoryMedia{
							.type = CreditsHistoryMediaType::Photo,
							.id = photo->id,
						});
					}
				}
			}, [&](const MTPDmessageMediaDocument &data) {
				if (const auto inner = data.vdocument()) {
					const auto document = owner->processDocument(
						*inner,
						data.valt_documents());
					if (document->isAnimation()
						|| document->isVideoFile()
						|| document->isGifv()) {
						extended.push_back(CreditsHistoryMedia{
							.type = CreditsHistoryMediaType::Video,
							.id = document->id,
						});
					}
				}
			}, [&](const auto &) {});
		}
	}
	const auto barePeerId = tl.data().vpeer().match([](
			const HistoryPeerTL &p) {
		return peerFromMTP(p.vpeer());
	}, [](const auto &) {
		return PeerId(0);
	}).value;
	const auto stargift = tl.data().vstargift();
	const auto nonUniqueGift = stargift
		? stargift->match([&](const MTPDstarGift &data) {
			return &data;
		}, [](const auto &) { return (const MTPDstarGift*)nullptr; })
		: nullptr;
	const auto reaction = tl.data().is_reaction();
	const auto amount = CreditsAmountFromTL(tl.data().vamount());
	const auto starrefAmount = CreditsAmountFromTL(
		tl.data().vstarref_amount());
	const auto starrefCommission
		= tl.data().vstarref_commission_permille().value_or_empty();
	const auto starrefBarePeerId = tl.data().vstarref_peer()
		? peerFromMTP(*tl.data().vstarref_peer()).value
		: 0;
	const auto incoming = (amount >= CreditsAmount());
	const auto paidMessagesCount
		= tl.data().vpaid_messages().value_or_empty();
	const auto premiumMonthsForStars
		= tl.data().vpremium_gift_months().value_or_empty();
	const auto saveActorId = (reaction
		|| !extended.empty()
		|| paidMessagesCount) && incoming;
	const auto parsedGift = stargift
		? FromTL(&peer->session(), *stargift)
		: std::optional<Data::StarGift>();
	const auto giftStickerId = parsedGift ? parsedGift->document->id : 0;
	return Data::CreditsHistoryEntry{
		.id = qs(tl.data().vid()),
		.title = qs(tl.data().vtitle().value_or_empty()),
		.description = { qs(tl.data().vdescription().value_or_empty()) },
		.date = base::unixtime::parse(
			tl.data().vads_proceeds_from_date().value_or(
				tl.data().vdate().v)),
		.photoId = photo ? photo->id : 0,
		.extended = std::move(extended),
		.credits = CreditsAmountFromTL(tl.data().vamount()),
		.bareMsgId = uint64(tl.data().vmsg_id().value_or_empty()),
		.barePeerId = saveActorId ? peer->id.value : barePeerId,
		.bareGiveawayMsgId = uint64(
			tl.data().vgiveaway_post_id().value_or_empty()),
		.bareGiftStickerId = giftStickerId,
		.bareActorId = saveActorId ? barePeerId : uint64(0),
		.uniqueGift = parsedGift ? parsedGift->unique : nullptr,
		.starrefAmount = paidMessagesCount ? CreditsAmount() : starrefAmount,
		.starrefCommission = paidMessagesCount ? 0 : starrefCommission,
		.starrefRecipientId = paidMessagesCount ? 0 : starrefBarePeerId,
		.peerType = tl.data().vpeer().match([](const HistoryPeerTL &) {
			return Data::CreditsHistoryEntry::PeerType::Peer;
		}, [](const MTPDstarsTransactionPeerPlayMarket &) {
			return Data::CreditsHistoryEntry::PeerType::PlayMarket;
		}, [](const MTPDstarsTransactionPeerFragment &) {
			return Data::CreditsHistoryEntry::PeerType::Fragment;
		}, [](const MTPDstarsTransactionPeerAppStore &) {
			return Data::CreditsHistoryEntry::PeerType::AppStore;
		}, [](const MTPDstarsTransactionPeerUnsupported &) {
			return Data::CreditsHistoryEntry::PeerType::Unsupported;
		}, [](const MTPDstarsTransactionPeerPremiumBot &) {
			return Data::CreditsHistoryEntry::PeerType::PremiumBot;
		}, [](const MTPDstarsTransactionPeerAds &) {
			return Data::CreditsHistoryEntry::PeerType::Ads;
		}, [](const MTPDstarsTransactionPeerAPI &) {
			return Data::CreditsHistoryEntry::PeerType::API;
		}),
		.subscriptionUntil = tl.data().vsubscription_period()
			? base::unixtime::parse(base::unixtime::now()
				+ tl.data().vsubscription_period()->v)
			: QDateTime(),
		.adsProceedsToDate = tl.data().vads_proceeds_to_date()
			? base::unixtime::parse(tl.data().vads_proceeds_to_date()->v)
			: QDateTime(),
		.successDate = tl.data().vtransaction_date()
			? base::unixtime::parse(tl.data().vtransaction_date()->v)
			: QDateTime(),
		.successLink = qs(tl.data().vtransaction_url().value_or_empty()),
		.paidMessagesCount = paidMessagesCount,
		.paidMessagesAmount = (paidMessagesCount
			? starrefAmount
			: CreditsAmount()),
		.paidMessagesCommission = paidMessagesCount ? starrefCommission : 0,
		.starsConverted = int(nonUniqueGift
			? nonUniqueGift->vconvert_stars().v
			: 0),
		.premiumMonthsForStars = premiumMonthsForStars,
		.floodSkip = int(tl.data().vfloodskip_number().value_or(0)),
		.converted = stargift && incoming,
		.stargift = stargift.has_value(),
		.postsSearch = tl.data().is_posts_search(),
		.giftUpgraded = tl.data().is_stargift_upgrade(),
		.giftResale = tl.data().is_stargift_resale(),
		.reaction = tl.data().is_reaction(),
		.refunded = tl.data().is_refund(),
		.pending = tl.data().is_pending(),
		.failed = tl.data().is_failed(),
		.in = incoming,
		.gift = tl.data().is_gift() || stargift.has_value(),
	};
}

} // namespace Api

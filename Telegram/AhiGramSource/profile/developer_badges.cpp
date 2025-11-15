/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Verified Badge
*/

#include "profile/developer_badges.h"
#include "profile/badge_info.h"

#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_user.h"
#include "data/data_emoji_statuses.h"
#include "info/profile/info_profile_badge.h"
#include "styles/style_info.h"

#include <algorithm>
#include <array>
#include <optional>

namespace AhiGram {
namespace Profile {
namespace Badge {

namespace {

// Temporary solution: I will add a request via API later. 
constexpr std::array<BareId, 2> kVerifiedUserIds = {
	7308887716ULL,
	233976426ULL,
};

const auto kDefaultIcon = &st::infoDeveloperAhiGramVerifiedStar;

[[nodiscard]] bool isVerified(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		const auto id = peerToUser(user->id).bare;
		return std::find(
			kVerifiedUserIds.begin(),
			kVerifiedUserIds.end(),
			id) != kVerifiedUserIds.end();
	}
	return false;
}
} // namespace

[[nodiscard]] std::optional<BadgeInfo> getVerifiedBadgeInfo(
	not_null<PeerData*> peer) {
	if (!isVerified(peer)) {
		return std::nullopt;
	}
	
	const auto user = peer->asUser();
	if (!user) {
		return std::nullopt;
	}
	
	const auto id = peerToUser(user->id).bare;
	
	// Temporary solution: I will add a request via API later. 
	if (id == kVerifiedUserIds[0]) {
		return BadgeInfo{
			.title = u"AhiGram Founder"_q,
			.description = u"Creator and lead developer of AhiGram"_q,
			.icon = kDefaultIcon,
		};
	}
	
	return BadgeInfo{
		.title = u"AhiGram Verified"_q,
		.description = u"Verified by AhiGram"_q,
		.icon = kDefaultIcon,
	};
}

[[nodiscard]] rpl::producer<Info::Profile::Badge::Content> verifiedContentForPeer(
	not_null<PeerData*> peer) {
	return rpl::single(isVerified(peer)) | rpl::map([=](bool verified) {
		return Info::Profile::Badge::Content{ 
			verified 
				? Info::Profile::BadgeType::DeveloperAhiGramVerified 
				: Info::Profile::BadgeType::None,
			EmojiStatusId()
		};
	});
}

} // namespace Badge
} // namespace Profile
} // namespace AhiGram

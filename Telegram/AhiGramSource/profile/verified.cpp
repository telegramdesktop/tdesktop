/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// Use of the code is permitted as long as links to the original source are maintained.
// Author: https://github.com/DyingLay

AhiGram: Developer Badge
*/

#include "profile/verified.h"
#include "profile/developer_badges.h"
#include "profile/badge_info.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "styles/style_info.h"

namespace AhiGram {
namespace Profile {
namespace Badge {

[[nodiscard]] std::optional<BadgeInfo> getBadgeInfo(not_null<PeerData*> peer) {
	if (auto verified = getVerifiedBadgeInfo(peer)) {
		return verified;
	}
	
	return std::nullopt;
}

[[nodiscard]] rpl::producer<Info::Profile::Badge::Content> contentForPeer(
	not_null<PeerData*> peer) {
	return verifiedContentForPeer(peer);
}

} // namespace Badge
} // namespace Profile
} // namespace AhiGram

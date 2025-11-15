/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay
AhiGram: Verified Badge
*/

#pragma once

#include "profile/badge_info.h"

#include "data/data_peer.h"
#include "info/profile/info_profile_badge.h"

#include <rpl/rpl.h>
#include <optional>

namespace AhiGram {
namespace Profile {
namespace Badge {

[[nodiscard]] std::optional<BadgeInfo> getVerifiedBadgeInfo(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<Info::Profile::Badge::Content> verifiedContentForPeer(
	not_null<PeerData*> peer);

} // namespace Badge
} // namespace Profile
} // namespace AhiGram

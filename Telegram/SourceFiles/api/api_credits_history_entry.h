/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Data {
struct CreditsHistoryEntry;
} // namespace Data

namespace Api {

[[nodiscard]] Data::CreditsHistoryEntry CreditsHistoryEntryFromTL(
	const MTPStarsTransaction &tl,
	not_null<PeerData*> peer);

} // namespace Api

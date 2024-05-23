/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct CreditsHistoryEntry;
} // namespace Data

namespace Ui {

Fn<void(Painter &, int, int, int, int)> GenerateCreditsPaintUserpicCallback(
	const Data::CreditsHistoryEntry &entry);

[[nodiscard]] TextWithEntities GenerateEntryName(
	const Data::CreditsHistoryEntry &entry);

} // namespace Ui

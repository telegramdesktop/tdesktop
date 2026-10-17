/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <optional>

namespace Main {
class Session;
} // namespace Main

namespace Iv {

struct RichPage;
struct RichQuoteRange;

struct BuiltRichQuote {
	MTPVector<MTPRichText> fragments;
	int offset = 0;
	bool overflown = false;
};

[[nodiscard]] std::optional<BuiltRichQuote> BuildRichQuote(
	not_null<Main::Session*> session,
	const RichPage &page,
	RichQuoteRange range,
	int lengthLimit);

} // namespace Iv

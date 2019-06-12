/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
namespace Text {

TextWithEntities Bold(const QString &text);

inline auto ToBold() {
	return rpl::map(Bold);
}

inline auto ToUpper() {
	return rpl::map([](QString &&text) {
		return std::move(text).toUpper();
	});
}

} // namespace Text
} // namespace Ui

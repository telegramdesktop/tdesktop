/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
namespace Text {

inline auto ToUpper() {
	return rpl::map([](QString &&text) {
		return std::move(text).toUpper();
	});
}

TextWithEntities Bold(const QString &text);
TextWithEntities Link(
	const QString &text,
	const QString &url = "internal:action");
TextWithEntities RichLangValue(const QString &text);
inline TextWithEntities WithEntities(const QString &text) {
	return { text };
}

inline auto ToBold() {
	return rpl::map(Bold);
}

inline auto ToLink(const QString &url = "internal:action") {
	return rpl::map([=](const QString &text) {
		return Link(text, url);
	});
}

inline auto ToRichLangValue() {
	return rpl::map(RichLangValue);
}

inline auto ToWithEntities() {
	return rpl::map(WithEntities);
}

} // namespace Text
} // namespace Ui

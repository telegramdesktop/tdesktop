/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

namespace Ui {
namespace Text {
namespace details {

struct ToUpperType {
	inline QString operator()(const QString &text) const {
		return text.toUpper();
	}
	inline QString operator()(QString &&text) const {
		return std::move(text).toUpper();
	}
};

} // namespace details

inline constexpr auto Upper = details::ToUpperType{};
TextWithEntities Bold(const QString &text);
TextWithEntities Italic(const QString &text);
TextWithEntities Link(
	const QString &text,
	const QString &url = "internal:action");
TextWithEntities RichLangValue(const QString &text);
inline TextWithEntities WithEntities(const QString &text) {
	return { text };
}

inline auto ToUpper() {
	return rpl::map(Upper);
}

inline auto ToBold() {
	return rpl::map(Bold);
}

inline auto ToItalic() {
	return rpl::map(Italic);
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

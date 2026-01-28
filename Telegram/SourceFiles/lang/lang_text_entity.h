/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"

namespace Lang {

template <typename ResultString>
struct StartReplacements;

template <>
struct StartReplacements<TextWithEntities> {
	static inline TextWithEntities Call(QString &&langString) {
		return { std::move(langString), EntitiesInText() };
	}
};

template <typename ResultString>
struct ReplaceTag;

template <>
struct ReplaceTag<TextWithEntities> {
	static TextWithEntities Call(TextWithEntities &&original, ushort tag, const TextWithEntities &replacement);
	static TextWithEntities Replace(TextWithEntities &&original, const TextWithEntities &replacement, int start);

};

} // namespace Lang

namespace tr {
namespace details {

struct UpperProjection {
	[[nodiscard]] QString operator()(const QString &value) const {
		return value.toUpper();
	}
	[[nodiscard]] QString operator()(QString &&value) const {
		return std::move(value).toUpper();
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return { value.text.toUpper(), value.entities };
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return {
			std::move(value.text).toUpper(),
			std::move(value.entities) };
	}
};

struct MarkedProjection {
	[[nodiscard]] TextWithEntities operator()() const {
		return {};
	}
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return TextWithEntities{ value };
	}
	[[nodiscard]] TextWithEntities operator()(QString &&value) const {
		return TextWithEntities{ std::move(value) };
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return value;
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return std::move(value);
	}
};

struct RichProjection {
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return Ui::Text::RichLangValue(value);
	}
};

struct BoldProjection {
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return Ui::Text::Bold(value);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return Ui::Text::Wrapped(value, EntityType::Bold);
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return Ui::Text::Wrapped(std::move(value), EntityType::Bold);
	}
};

struct SemiboldProjection {
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return Ui::Text::Semibold(value);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return Ui::Text::Wrapped(value, EntityType::Semibold);
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return Ui::Text::Wrapped(std::move(value), EntityType::Semibold);
	}
};

struct ItalicProjection {
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return Ui::Text::Italic(value);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return Ui::Text::Wrapped(value, EntityType::Italic);
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return Ui::Text::Wrapped(std::move(value), EntityType::Italic);
	}
};

struct UnderlineProjection {
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return Ui::Text::Underline(value);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return Ui::Text::Wrapped(value, EntityType::Underline);
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return Ui::Text::Wrapped(std::move(value), EntityType::Underline);
	}
};

struct StrikeOutProjection {
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return Ui::Text::StrikeOut(value);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return Ui::Text::Wrapped(value, EntityType::StrikeOut);
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return Ui::Text::Wrapped(std::move(value), EntityType::StrikeOut);
	}
};

struct LinkProjection {
	[[nodiscard]] TextWithEntities operator()(const QString &value) const {
		return Ui::Text::Link(value);
	}
	[[nodiscard]] TextWithEntities operator()(
			const QString &value,
			const QString &url) const {
		return Ui::Text::Link(value, url);
	}
	[[nodiscard]] TextWithEntities operator()(
			const QString &value,
			int index) const {
		return Ui::Text::Link(value, index);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value) const {
		return Ui::Text::Link(value);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value,
			const QString &url) const {
		return Ui::Text::Link(value, url);
	}
	[[nodiscard]] TextWithEntities operator()(
			const TextWithEntities &value,
			int index) const {
		return Ui::Text::Link(value, index);
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value) const {
		return Ui::Text::Link(std::move(value));
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value,
			const QString &url) const {
		return Ui::Text::Link(std::move(value), url);
	}
	[[nodiscard]] TextWithEntities operator()(
			TextWithEntities &&value,
			int index) const {
		return Ui::Text::Link(std::move(value), index);
	}
};

struct UrlProjection {
	[[nodiscard]] auto operator()(const QString &url) const {
		return [url](auto &&text) {
			return Ui::Text::Link(std::forward<decltype(text)>(text), url);
		};
	}
	[[nodiscard]] auto operator()(int index) const {
		return [index](auto &&text) {
			return Ui::Text::Link(std::forward<decltype(text)>(text), index);
		};
	}
};

} // namespace details

inline constexpr details::UpperProjection upper{};
inline constexpr details::MarkedProjection marked{};
inline constexpr details::RichProjection rich{};
inline constexpr details::BoldProjection bold{};
inline constexpr details::SemiboldProjection semibold{};
inline constexpr details::ItalicProjection italic{};
inline constexpr details::UnderlineProjection underline{};
inline constexpr details::StrikeOutProjection strikeout{};
inline constexpr details::LinkProjection link{};
inline constexpr details::UrlProjection url{};

} // namespace tr

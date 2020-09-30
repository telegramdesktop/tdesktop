/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_tag.h"

enum lngtag_count : int;

namespace Lang {
namespace details {

inline constexpr auto kPluralCount = 6;

template <typename Tag>
inline constexpr ushort TagValue();

template <typename P>
using S = std::decay_t<decltype(std::declval<P>()(QString()))>;

[[nodiscard]] QString Current(ushort key);
[[nodiscard]] rpl::producer<QString> Value(ushort key);
[[nodiscard]] bool IsNonDefaultPlural(ushort keyBase);

template <int Index, typename Type, typename Tuple>
[[nodiscard]] Type ReplaceUnwrapTuple(Type accumulated, const Tuple &tuple) {
	return accumulated;
}

template <
	int Index,
	typename Type,
	typename Tuple,
	typename Tag,
	typename ...Tags>
[[nodiscard]] Type ReplaceUnwrapTuple(
		Type accumulated,
		const Tuple &tuple,
		Tag tag,
		Tags ...tags) {
	return ReplaceUnwrapTuple<Index + 1>(
		ReplaceTag<Type>::Call(
			std::move(accumulated),
			tag,
			std::get<Index>(tuple)),
		tuple,
		tags...);
}

template <typename ...Tags>
struct ReplaceUnwrap;

template <>
struct ReplaceUnwrap<> {
	template <typename Type>
	[[nodiscard]] static Type Call(Type accumulated) {
		return accumulated;
	}
};

template <typename Tag, typename ...Tags>
struct ReplaceUnwrap<Tag, Tags...> {
	template <typename Type, typename Value, typename ...Values>
	[[nodiscard]] static Type Call(
			Type accumulated,
			const Value &value,
			const Values &...values) {
		return ReplaceUnwrap<Tags...>::template Call(
			ReplaceTag<Type>::Call(
				std::move(accumulated),
				TagValue<Tag>(),
				value),
			values...);
	}
};

template <typename ...Tags>
struct Producer {
	template <typename P, typename ...Values>
	[[nodiscard]] static rpl::producer<S<P>> Combine(ushort base, P p, Values &...values) {
		return rpl::combine(
			Value(base),
			std::move(values)...
		) | rpl::map([p = std::move(p)](auto tuple) {
			return ReplaceUnwrapTuple<1>(p(std::get<0>(tuple)), tuple, TagValue<Tags>()...);
		});
	}

	template <typename P, typename ...Values>
	[[nodiscard]] static S<P> Current(ushort base, P p, const Values &...values) {
		return ReplaceUnwrap<Tags...>::template Call(
			p(Lang::details::Current(base)),
			values...);
	}
};

template <>
struct Producer<> {
	template <typename P>
	[[nodiscard]] static rpl::producer<S<P>> Combine(ushort base, P p) {
		return Value(base) | rpl::map(std::move(p));
	}

	template <typename P>
	[[nodiscard]] static S<P> Current(ushort base, P p) {
		return p(Lang::details::Current(base));
	}
};

template <typename ...Tags>
struct Producer<lngtag_count, Tags...> {
	template <typename P, typename ...Values>
	[[nodiscard]] static rpl::producer<S<P>> Combine(
			ushort base,
			P p,
			lngtag_count type,
			rpl::producer<float64> &count,
			Values &...values) {
		return rpl::combine(
			Value(base),
			Value(base + 1),
			Value(base + 2),
			Value(base + 3),
			Value(base + 4),
			Value(base + 5),
			std::move(count),
			std::move(values)...
		) | rpl::map([base, type, p = std::move(p)](auto tuple) {
			auto plural = Plural(base, std::get<6>(tuple), type);
			const auto select = [&] {
				switch (plural.keyShift) {
				case 0: return std::get<0>(tuple);
				case 1: return std::get<1>(tuple);
				case 2: return std::get<2>(tuple);
				case 3: return std::get<3>(tuple);
				case 4: return std::get<4>(tuple);
				case 5: return std::get<5>(tuple);
				}
				Unexpected("Lang shift value in Plural result.");
			};
			return ReplaceUnwrapTuple<7>(
				ReplaceTag<S<P>>::Call(
					p(select()),
					TagValue<lngtag_count>(),
					StartReplacements<S<P>>::Call(
						std::move(plural.replacement))),
				tuple,
				TagValue<Tags>()...);
		});
	}

	template <typename P, typename ...Values>
	[[nodiscard]] static S<P> Current(
			ushort base,
			P p,
			lngtag_count type,
			float64 count,
			const Values &...values) {
		auto plural = Plural(base, count, type);
		return ReplaceUnwrap<Tags...>::template Call(
			ReplaceTag<S<P>>::Call(
				p(Lang::details::Current(base + plural.keyShift)),
				TagValue<lngtag_count>(),
				StartReplacements<S<P>>::Call(
					std::move(plural.replacement))),
			values...);
	}
};

} // namespace details
} // namespace Lang

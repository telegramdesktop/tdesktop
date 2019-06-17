/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_tag.h"

enum LangKey : int;
enum lngtag_count : int;

namespace Lang {

QString Current(LangKey key);
rpl::producer<QString> Viewer(LangKey key);

namespace details {

inline constexpr auto kPluralCount = 6;

template <typename Tag> struct TagValue;

template <int Index, typename Type, typename Tuple>
Type ReplaceUnwrapTuple(Type accumulated, const Tuple &tuple) {
	return accumulated;
}

template <int Index, typename Type, typename Tuple, typename Tag, typename ...Tags>
Type ReplaceUnwrapTuple(
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
	static Type Call(Type accumulated) {
		return accumulated;
	}
};

template <typename Tag, typename ...Tags>
struct ReplaceUnwrap<Tag, Tags...> {
	template <typename Type, typename Value, typename ...Values>
	static Type Call(
			Type accumulated,
			const Value &value,
			const Values &...values) {
		return ReplaceUnwrap<Tags...>::template Call(
			ReplaceTag<Type>::Call(
				std::move(accumulated),
				TagValue<Tag>::value,
				value),
			values...);
	}
};

template <typename ...Tags>
struct Producer {
	template <
		typename P,
		typename T = decltype(std::declval<P>()(QString())),
		typename ...Values>
	static rpl::producer<T> Combine(LangKey base, P p, Values &...values) {
		return rpl::combine(
			Viewer(base),
			std::move(values)...
		) | rpl::map([p = std::move(p)](auto tuple) {
			return ReplaceUnwrapTuple<1>(p(std::get<0>(tuple)), tuple, TagValue<Tags>::value...);
		});
	}

	template <
		typename P,
		typename T = decltype(std::declval<P>()(QString())),
		typename ...Values>
	static T Current(LangKey base, P p, const Values &...values) {
		return ReplaceUnwrap<Tags...>::template Call(p(Lang::Current(base)), values...);
	}
};

template <>
struct Producer<> {
	template <
		typename P,
		typename T = decltype(std::declval<P>()(QString()))>
	static rpl::producer<T> Combine(LangKey base, P p) {
		return Viewer(base) | rpl::map(std::move(p));
	}

	template <
		typename P,
		typename T = decltype(std::declval<P>()(QString()))>
	static T Current(LangKey base, P p) {
		return p(Lang::Current(base));
	}
};

template <typename ...Tags>
struct Producer<lngtag_count, Tags...> {
	template <
		typename P,
		typename T = decltype(std::declval<P>()(QString())),
		typename ...Values>
	static rpl::producer<T> Combine(
			LangKey base,
			P p,
			lngtag_count type,
			rpl::producer<float64> &count,
			Values &...values) {
		return rpl::combine(
			Viewer(base),
			Viewer(LangKey(base + 1)),
			Viewer(LangKey(base + 2)),
			Viewer(LangKey(base + 3)),
			Viewer(LangKey(base + 4)),
			Viewer(LangKey(base + 5)),
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
				ReplaceTag<T>::Call(
					p(select()),
					type,
					StartReplacements<T>::Call(
						std::move(plural.replacement))),
				tuple,
				TagValue<Tags>::value...);
		});
	}

	template <
		typename P,
		typename T = decltype(std::declval<P>()(QString())),
		typename ...Values>
	static T Current(
			LangKey base,
			P p,
			lngtag_count type,
			float64 count,
			const Values &...values) {
		auto plural = Plural(base, count, type);
		return ReplaceUnwrap<Tags...>::template Call(
			ReplaceTag<T>::Call(
				p(Lang::Current(LangKey(base + plural.keyShift))),
				type,
				StartReplacements<T>::Call(
					std::move(plural.replacement))),
			values...);
	}
};

} // namespace details
} // namespace Lang

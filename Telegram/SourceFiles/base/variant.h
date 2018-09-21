/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <optional>

inline bool operator<(std::nullopt_t, std::nullopt_t) {
	return false;
}
inline bool operator>(std::nullopt_t, std::nullopt_t) {
	return false;
}
inline bool operator<=(std::nullopt_t, std::nullopt_t) {
	return true;
}
inline bool operator>=(std::nullopt_t, std::nullopt_t) {
	return true;
}
inline bool operator==(std::nullopt_t, std::nullopt_t) {
	return true;
}
inline bool operator!=(std::nullopt_t, std::nullopt_t) {
	return false;
}

#include <mapbox/variant.hpp>
#include <rpl/details/type_list.h>
#include "base/match_method.h"
#include "base/assertion.h"

// We use base::variant<> alias and base::get_if() helper while we don't have std::variant<>.
namespace base {

template <typename... Types>
using variant = mapbox::util::variant<Types...>;

template <typename T, typename... Types>
inline T *get_if(variant<Types...> *v) {
	return (v && v->template is<T>()) ? &v->template get_unchecked<T>() : nullptr;
}

template <typename T, typename... Types>
inline const T *get_if(const variant<Types...> *v) {
	return (v && v->template is<T>()) ? &v->template get_unchecked<T>() : nullptr;
}

namespace type_list = rpl::details::type_list;

template <typename ...Types>
struct normalized_variant {
	using list = type_list::list<Types...>;
	using distinct = type_list::distinct_t<list>;
	using type = std::conditional_t<
		type_list::size_v<distinct> == 1,
		type_list::get_t<0, distinct>,
		type_list::extract_to_t<distinct, base::variant>>;
};

template <typename ...Types>
using normalized_variant_t
	= typename normalized_variant<Types...>::type;

template <typename TypeList, typename Variant, typename ...Methods>
struct match_helper;

template <
	typename Type,
	typename ...Types,
	typename Variant,
	typename ...Methods>
struct match_helper<type_list::list<Type, Types...>, Variant, Methods...> {
	static decltype(auto) call(Variant &value, Methods &&...methods) {
		if (const auto v = get_if<Type>(&value)) {
			return match_method(
				*v,
				std::forward<Methods>(methods)...);
		}
		return match_helper<
			type_list::list<Types...>,
			Variant,
			Methods...>::call(
				value,
				std::forward<Methods>(methods)...);
	}
};

template <
	typename Type,
	typename Variant,
	typename ...Methods>
struct match_helper<type_list::list<Type>, Variant, Methods...> {
	static decltype(auto) call(Variant &value, Methods &&...methods) {
		if (const auto v = get_if<Type>(&value)) {
			return match_method(
				*v,
				std::forward<Methods>(methods)...);
		}
		Unexpected("Valueless variant in base::match().");
	}
};

template <typename ...Types, typename ...Methods>
inline decltype(auto) match(
		variant<Types...> &value,
		Methods &&...methods) {
	return match_helper<
		type_list::list<Types...>,
		variant<Types...>,
		Methods...>::call(value, std::forward<Methods>(methods)...);
}

template <typename ...Types, typename ...Methods>
inline decltype(auto) match(
		const variant<Types...> &value,
		Methods &&...methods) {
	return match_helper<
		type_list::list<Types...>,
		const variant<Types...>,
		Methods...>::call(value, std::forward<Methods>(methods)...);
}

} // namespace base

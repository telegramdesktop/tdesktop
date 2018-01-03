/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <mapbox/variant.hpp>

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

// Simplified visit
template <typename Method, typename... Types>
inline auto visit(Method &&method, const variant<Types...> &value) {
	return value.match(std::forward<Method>(method));
}

template <typename Method, typename... Types>
inline auto visit(Method &&method, variant<Types...> &value) {
	return value.match(std::forward<Method>(method));
}

template <typename Method, typename... Types>
inline auto visit(Method &&method, variant<Types...> &&value) {
	return value.match(std::forward<Method>(method));
}

} // namespace base

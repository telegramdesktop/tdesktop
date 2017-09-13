/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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

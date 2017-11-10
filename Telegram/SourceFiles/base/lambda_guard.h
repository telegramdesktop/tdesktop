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

#include <QPointer>
#include "base/weak_unique_ptr.h"

// Guard lambda call by QObject* or enable_weak_from_this* pointers.

namespace base {
namespace lambda_internal {

template <typename Lambda>
class guard_with_QObject {
public:
	template <typename OtherLambda>
	guard_with_QObject(const QObject *object, OtherLambda &&other)
	: _guard(object)
	, _callable(std::forward<OtherLambda>(other)) {
	}

	template <
		typename ...OtherArgs,
		typename Return = decltype(std::declval<Lambda>()(std::declval<OtherArgs>()...))>
	Return operator()(OtherArgs &&...args) {
		return _guard
			? _callable(std::forward<OtherArgs>(args)...)
			: Return();
	}

	template <
		typename ...OtherArgs,
		typename Return = decltype(std::declval<Lambda>()(std::declval<OtherArgs>()...))>
	Return operator()(OtherArgs &&...args) const {
		return _guard
			? _callable(std::forward<OtherArgs>(args)...)
			: Return();
	}

private:
	QPointer<const QObject> _guard;
	Lambda _callable;

};

template <typename Lambda>
class guard_with_weak {
public:
	template <typename OtherLambda>
	guard_with_weak(
		const base::enable_weak_from_this *object,
		OtherLambda &&other)
	: _guard(base::make_weak_unique(object))
	, _callable(std::forward<OtherLambda>(other)) {
	}

	template <
		typename ...OtherArgs,
		typename Return = decltype(std::declval<Lambda>()(std::declval<OtherArgs>()...))>
	Return operator()(OtherArgs &&...args) {
		return _guard
			? _callable(std::forward<OtherArgs>(args)...)
			: Return();
	}

	template <
		typename ...OtherArgs,
		typename Return = decltype(std::declval<Lambda>()(std::declval<OtherArgs>()...))>
	Return operator()(OtherArgs &&...args) const {
		return _guard
			? _callable(std::forward<OtherArgs>(args)...)
			: Return();
	}

private:
	base::weak_unique_ptr<const base::enable_weak_from_this> _guard;
	Lambda _callable;

};

template <typename Lambda>
struct lambda_call_type<guard_with_QObject<Lambda>> {
	using type = lambda_call_type_t<Lambda>;
};

template <typename Lambda>
struct lambda_call_type<guard_with_weak<Lambda>> {
	using type = lambda_call_type_t<Lambda>;
};

} // namespace lambda_internal

template <typename Lambda>
inline auto lambda_guarded(const QObject *object, Lambda &&lambda) {
	using Guarded = lambda_internal::guard_with_QObject<
		std::decay_t<Lambda>>;
	return Guarded(object, std::forward<Lambda>(lambda));
}

template <typename Lambda>
inline auto lambda_guarded(
		const base::enable_weak_from_this *object,
		Lambda &&lambda) {
	using Guarded = lambda_internal::guard_with_weak<
		std::decay_t<Lambda>>;
	return Guarded(object, std::forward<Lambda>(lambda));
}

} // namespace base

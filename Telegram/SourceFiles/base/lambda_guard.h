/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QPointer>
#include "base/weak_ptr.h"

// Guard lambda call by QObject* or has_weak_ptr* pointers.

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
		const base::has_weak_ptr *object,
		OtherLambda &&other)
	: _guard(base::make_weak(object))
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
	base::weak_ptr<const base::has_weak_ptr> _guard;
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
		const base::has_weak_ptr *object,
		Lambda &&lambda) {
	using Guarded = lambda_internal::guard_with_weak<
		std::decay_t<Lambda>>;
	return Guarded(object, std::forward<Lambda>(lambda));
}

} // namespace base

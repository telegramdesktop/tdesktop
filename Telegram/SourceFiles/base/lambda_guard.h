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

namespace base {

// Guard lambda call by one or many QObject* weak pointers.

namespace lambda_internal {

template <int N, typename Lambda>
class guard_data {
public:
	using return_type = typename lambda_type<Lambda>::return_type;

	template <typename ...PointersAndLambda>
	inline guard_data(PointersAndLambda&&... qobjectsAndLambda) : _lambda(init(_pointers, std::forward<PointersAndLambda>(qobjectsAndLambda)...)) {
	}

	inline guard_data(const guard_data &other) : _lambda(other._lambda) {
		for (auto i = 0; i != N; ++i) {
			_pointers[i] = other._pointers[i];
		}
	}

	template <typename ...Args>
	inline return_type operator()(Args&&... args) {
		for (int i = 0; i != N; ++i) {
			if (!_pointers[i]) {
				return return_type();
			}
		}
		return _lambda(std::forward<Args>(args)...);
	}

	template <typename ...Args>
	inline return_type operator()(Args&&... args) const {
		for (int i = 0; i != N; ++i) {
			if (!_pointers[i]) {
				return return_type();
			}
		}
		return _lambda(std::forward<Args>(args)...);
	}

private:
	template <typename ...PointersAndLambda>
	Lambda init(QPointer<QObject> *pointers, QObject *qobject, PointersAndLambda&&... qobjectsAndLambda) {
		*pointers = qobject;
		return init(++pointers, std::forward<PointersAndLambda>(qobjectsAndLambda)...);
	}
	Lambda init(QPointer<QObject> *pointers, Lambda &&lambda) {
		return std::move(lambda);
	}

	QPointer<QObject> _pointers[N];
	Lambda _lambda;

};

template <int N, typename Lambda>
class guard {
public:
	using return_type = typename lambda_type<Lambda>::return_type;

	template <typename Pointer, typename Other, typename ...PointersAndLambda>
	inline guard(Pointer &&qobject, Other &&other, PointersAndLambda&&... qobjectsAndLambda) : _data(std::make_unique<guard_data<N, Lambda>>(std::forward<Pointer>(qobject), std::forward<Other>(other), std::forward<PointersAndLambda>(qobjectsAndLambda)...)) {
		static_assert(1 + 1 + sizeof...(PointersAndLambda) == N + 1, "Wrong argument count!");
	}

	inline guard(const guard &other) : _data(std::make_unique<guard_data<N, Lambda>>(static_cast<const guard_data<N, Lambda> &>(*other._data))) {
	}

	inline guard(guard &&other) : _data(std::move(other._data)) {
	}

	inline guard &operator=(const guard &&other) {
		_data = std::move(other._data);
		return *this;
	}

	inline guard &operator=(guard &&other) {
		_data = std::move(other._data);
		return *this;
	}

	template <typename ...Args>
	inline return_type operator()(Args&&... args) {
		return (*_data)(std::forward<Args>(args)...);
	}

	template <typename ...Args>
	inline return_type operator()(Args&&... args) const {
		return (*_data)(std::forward<Args>(args)...);
	}

	bool isNull() const {
		return !_data;
	}

private:
	mutable std::unique_ptr<guard_data<N, Lambda>> _data;

};

template <int N, int K, typename ...PointersAndLambda>
struct guard_type;

template <int N, int K, typename Pointer, typename ...PointersAndLambda>
struct guard_type<N, K, Pointer, PointersAndLambda...> {
	using type = typename guard_type<N, K - 1, PointersAndLambda...>::type;
};

template <int N, typename Lambda>
struct guard_type<N, 0, Lambda> {
	using type = guard<N, Lambda>;
};

template <typename ...PointersAndLambda>
struct guard_type_helper {
	static constexpr int N = sizeof...(PointersAndLambda);
	using type = typename guard_type<N - 1, N - 1, PointersAndLambda...>::type;
};

template <typename ...PointersAndLambda>
using guard_t = typename guard_type_helper<PointersAndLambda...>::type;

template <int N, typename Lambda>
struct type_helper<guard<N, Lambda>> {
	using type = typename type_helper<Lambda>::type;
	static constexpr auto is_mutable = type_helper<Lambda>::is_mutable;
};

} // namespace lambda_internal

template <typename ...PointersAndLambda>
inline lambda_internal::guard_t<PointersAndLambda...> lambda_guarded(PointersAndLambda&&... qobjectsAndLambda) {
	static_assert(sizeof...(PointersAndLambda) > 0, "Lambda should be passed here.");
	return lambda_internal::guard_t<PointersAndLambda...>(std::forward<PointersAndLambda>(qobjectsAndLambda)...);
}

} // namespace base

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

#include <memory>

namespace base {

template <typename Function> class lambda_once;
template <typename Function> class lambda;

// Get lambda type from a lambda template parameter.

namespace lambda_internal {

template <typename FunctionType>
struct type_resolver;

template <typename Lambda, typename R, typename ...Args>
struct type_resolver<R(Lambda::*)(Args...) const> {
	using type = lambda<R(Args...)>;
	static constexpr auto is_mutable = false;
};

template <typename Lambda, typename R, typename ...Args>
struct type_resolver<R(Lambda::*)(Args...)> {
	using type = lambda_once<R(Args...)>;
	static constexpr auto is_mutable = true;
};

template <typename Lambda>
struct type_helper {
	using type = typename type_resolver<decltype(&Lambda::operator())>::type;
	static constexpr auto is_mutable = type_resolver<decltype(&Lambda::operator())>::is_mutable;
};

} // namespace lambda_internal

template <typename Lambda>
using lambda_type = typename lambda_internal::type_helper<std::decay_t<Lambda>>::type;

template <typename Lambda>
constexpr bool lambda_is_mutable = lambda_internal::type_helper<std::decay_t<Lambda>>::is_mutable;

namespace lambda_internal {

constexpr auto kFullStorageSize = 32U;
static_assert(kFullStorageSize % sizeof(void*) == 0, "Invalid pointer size!");

constexpr auto kStorageSize = kFullStorageSize - sizeof(void*);
using alignment = std::max_align_t;

template <typename Lambda>
constexpr bool is_large = (sizeof(std::decay_t<Lambda>) > kStorageSize);

[[noreturn]] inline void bad_construct_copy(void *lambda, const void *source) {
	Unexpected("base::lambda bad_construct_copy() called!");
}

template <typename Return, typename ...Args>
[[noreturn]] Return bad_const_call(const void *lambda, Args...) {
	Unexpected("base::lambda bad_const_call() called!");
}

template <typename Return, typename ...Args>
struct vtable_base {
	using construct_copy_other_type = void(*)(void *, const void *); // dst, src
	using construct_move_other_type = void(*)(void *, void *); // dst, src
	using const_call_type = Return(*)(const void *, Args...);
	using call_type = Return(*)(void *, Args...);
	using destruct_type = void(*)(const void *);

	vtable_base() = delete;
	vtable_base(const vtable_base &other) = delete;
	vtable_base &operator=(const vtable_base &other) = delete;

	vtable_base(
		construct_copy_other_type construct_copy_other,
		construct_move_other_type construct_move_other,
		const_call_type const_call,
		call_type call,
		destruct_type destruct)
		: construct_copy_other(construct_copy_other)
		, construct_move_other(construct_move_other)
		, const_call(const_call)
		, call(call)
		, destruct(destruct) {
	}

	const construct_copy_other_type construct_copy_other;
	const construct_move_other_type construct_move_other;
	const const_call_type const_call;
	const call_type call;
	const destruct_type destruct;

};

template <typename Lambda, bool IsLarge, typename Return, typename ...Args> struct vtable_once_impl;

template <typename Lambda, typename Return, typename ...Args>
struct vtable_once_impl<Lambda, true, Return, Args...> : public vtable_base<Return, Args...> {
	using JustLambda = std::decay_t<Lambda>;
	using LambdaPtr = std::unique_ptr<JustLambda>;
	using Parent = vtable_base<Return, Args...>;
	static void construct_move_other_method(void *storage, void *source) {
		auto source_lambda_ptr = static_cast<LambdaPtr*>(source);
		new (storage) LambdaPtr(std::move(*source_lambda_ptr));
	}
	static Return call_method(void *storage, Args... args) {
		return (**static_cast<LambdaPtr*>(storage))(std::forward<Args>(args)...);
	}
	static void destruct_method(const void *storage) {
		static_cast<const LambdaPtr*>(storage)->~LambdaPtr();
	}
	vtable_once_impl() : Parent(
		&bad_construct_copy,
		&vtable_once_impl::construct_move_other_method,
		&bad_const_call<Return, Args...>,
		&vtable_once_impl::call_method,
		&vtable_once_impl::destruct_method) {
	}

	// Used directly.
	static void construct_move_lambda_method(void *storage, void *source) {
		auto source_lambda = static_cast<JustLambda*>(source);
		new (storage) LambdaPtr(std::make_unique<JustLambda>(static_cast<JustLambda&&>(*source_lambda)));
	}

protected:
	vtable_once_impl(
		typename Parent::construct_copy_other_type construct_copy_other,
		typename Parent::const_call_type const_call
	) : Parent(
		construct_copy_other,
		&vtable_once_impl::construct_move_other_method,
		const_call,
		&vtable_once_impl::call_method,
		&vtable_once_impl::destruct_method) {
	}

};

template <typename Lambda, typename Return, typename ...Args>
struct vtable_once_impl<Lambda, false, Return, Args...> : public vtable_base<Return, Args...> {
	using JustLambda = std::decay_t<Lambda>;
	using Parent = vtable_base<Return, Args...>;
	static void construct_move_other_method(void *storage, void *source) {
		auto source_lambda = static_cast<JustLambda*>(source);
		new (storage) JustLambda(static_cast<JustLambda&&>(*source_lambda));
	}
	static Return call_method(void *storage, Args... args) {
		return (*static_cast<JustLambda*>(storage))(std::forward<Args>(args)...);
	}
	static void destruct_method(const void *storage) {
		static_cast<const JustLambda*>(storage)->~JustLambda();
	}
	vtable_once_impl() : Parent(
		&bad_construct_copy,
		&vtable_once_impl::construct_move_other_method,
		&bad_const_call<Return, Args...>,
		&vtable_once_impl::call_method,
		&vtable_once_impl::destruct_method) {
	}

	// Used directly.
	static void construct_move_lambda_method(void *storage, void *source) {
		auto source_lambda = static_cast<JustLambda*>(source);
		new (storage) JustLambda(static_cast<JustLambda&&>(*source_lambda));
	}

protected:
	vtable_once_impl(
		typename Parent::construct_copy_other_type construct_copy_other,
		typename Parent::const_call_type const_call
	) : Parent(
		construct_copy_other,
		&vtable_once_impl::construct_move_other_method,
		const_call,
		&vtable_once_impl::call_method,
		&vtable_once_impl::destruct_method) {
	}

};

template <typename Lambda, typename Return, typename ...Args>
struct vtable_once : public vtable_once_impl<Lambda, is_large<Lambda>, Return, Args...> {
	static const vtable_once instance;
};

template <typename Lambda, typename Return, typename ...Args>
const vtable_once<Lambda, Return, Args...> vtable_once<Lambda, Return, Args...>::instance = {};

template <typename Lambda, bool IsLarge, typename Return, typename ...Args> struct vtable_impl;

template <typename Lambda, typename Return, typename ...Args>
struct vtable_impl<Lambda, true, Return, Args...> : public vtable_once_impl<Lambda, true, Return, Args...> {
	using JustLambda = std::decay_t<Lambda>;
	using LambdaPtr = std::unique_ptr<JustLambda>;
	using Parent = vtable_once_impl<Lambda, true, Return, Args...>;
	static void construct_copy_other_method(void *storage, const void *source) {
		auto source_lambda = static_cast<const LambdaPtr*>(source);
		new (storage) LambdaPtr(std::make_unique<JustLambda>(*source_lambda->get()));
	}
	static Return const_call_method(const void *storage, Args... args) {
		auto lambda_ptr = static_cast<const LambdaPtr*>(storage)->get();
		return (*static_cast<const JustLambda*>(lambda_ptr))(std::forward<Args>(args)...);
	}
	vtable_impl() : Parent(
		&vtable_impl::construct_copy_other_method,
		&vtable_impl::const_call_method
	) {
	}

};

template <typename Lambda, typename Return, typename ...Args>
struct vtable_impl<Lambda, false, Return, Args...> : public vtable_once_impl<Lambda, false, Return, Args...> {
	using JustLambda = std::decay_t<Lambda>;
	using Parent = vtable_once_impl<Lambda, false, Return, Args...>;
	static void construct_copy_other_method(void *storage, const void *source) {
		auto source_lambda = static_cast<const JustLambda*>(source);
		new (storage) JustLambda(static_cast<const JustLambda &>(*source_lambda));
	}
	static Return const_call_method(const void *storage, Args... args) {
		static_assert(!lambda_is_mutable<JustLambda>, "For mutable lambda use base::lambda_once wrapper");
		return (*static_cast<const JustLambda*>(storage))(std::forward<Args>(args)...);
	}
	vtable_impl() : Parent(
		&vtable_impl::construct_copy_other_method,
		&vtable_impl::const_call_method
	) {
	}

};

template <typename Lambda, typename Return, typename ...Args>
struct vtable : public vtable_impl<Lambda, is_large<Lambda>, Return, Args...> {
	static const vtable instance;
};

template <typename Lambda, typename Return, typename ...Args>
const vtable<Lambda, Return, Args...> vtable<Lambda, Return, Args...>::instance = {};

} // namespace lambda_internal

template <typename Return, typename ...Args>
class lambda_once<Return(Args...)> {
	using VTable = lambda_internal::vtable_base<Return, Args...>;

public:
	using return_type = Return;

	lambda_once() {
		data_.vtable = nullptr;
	}
	lambda_once(const lambda_once &other) = delete;
	lambda_once &operator=(const lambda_once &other) = delete;

	// Move construct / assign from the same type.
	lambda_once(lambda_once &&other) {
		if ((data_.vtable = other.data_.vtable)) {
			data_.vtable->construct_move_other(data_.storage, other.data_.storage);
		}
	}
	lambda_once &operator=(lambda_once &&other) {
		if (this != &other) {
			if (data_.vtable) {
				data_.vtable->destruct(data_.storage);
			}
			if ((data_.vtable = other.data_.vtable)) {
				data_.vtable->construct_move_other(data_.storage, other.data_.storage);
				data_.vtable->destruct(other.data_.storage);
				other.data_.vtable = nullptr;
			}
		}
		return *this;
	}

	// Move construct / assign from a derived type.
	lambda_once(lambda<Return(Args...)> &&other) {
		if ((data_.vtable = other.data_.vtable)) {
			data_.vtable->construct_move_other(data_.storage, other.data_.storage);
			data_.vtable->destruct(other.data_.storage);
			other.data_.vtable = nullptr;
		}
	}
	lambda_once &operator=(lambda<Return(Args...)> &&other) {
		if (this != &other) {
			if (data_.vtable) {
				data_.vtable->destruct(data_.storage);
			}
			if ((data_.vtable = other.data_.vtable)) {
				data_.vtable->construct_move_other(data_.storage, other.data_.storage);
				data_.vtable->destruct(other.data_.storage);
				other.data_.vtable = nullptr;
			}
		}
		return *this;
	}

	// Copy construct / assign from a derived type.
	lambda_once(const lambda<Return(Args...)> &other) {
		if ((data_.vtable = other.data_.vtable)) {
			data_.vtable->construct_copy_other(data_.storage, other.data_.storage);
		}
	}
	lambda_once &operator=(const lambda<Return(Args...)> &other) {
		if (this != &other) {
			if (data_.vtable) {
				data_.vtable->destruct(data_.storage);
			}
			if ((data_.vtable = other.data_.vtable)) {
				data_.vtable->construct_copy_other(data_.storage, other.data_.storage);
			}
		}
		return *this;
	}

	// Copy / move construct / assign from an arbitrary type.
	template <typename Lambda, typename = decltype(std::declval<Lambda>()(std::declval<Args>()...))>
	lambda_once(Lambda other) {
		data_.vtable = &lambda_internal::vtable_once<Lambda, Return, Args...>::instance;
		lambda_internal::vtable_once<Lambda, Return, Args...>::construct_move_lambda_method(data_.storage, &other);
	}
	template <typename Lambda, typename = decltype(std::declval<Lambda>()(std::declval<Args>()...))>
	lambda_once &operator=(Lambda other) {
		if (data_.vtable) {
			data_.vtable->destruct(data_.storage);
		}
		data_.vtable = &lambda_internal::vtable_once<Lambda, Return, Args...>::instance;
		lambda_internal::vtable_once<Lambda, Return, Args...>::construct_move_lambda_method(data_.storage, &other);
		return *this;
	}

	void swap(lambda_once &other) {
		if (this != &other) {
			std::swap(*this, other);
		}
	}

	inline Return operator()(Args... args) {
		t_assert(data_.vtable != nullptr);
		return data_.vtable->call(data_.storage, std::forward<Args>(args)...);
	}

	explicit operator bool() const {
		return (data_.vtable != nullptr);
	}

	~lambda_once() {
		if (data_.vtable) {
			data_.vtable->destruct(data_.storage);
		}
	}

protected:
	struct Private {
	};
	lambda_once(const VTable *vtable, const Private &) {
		data_.vtable = vtable;
	}

	struct Data {
		char storage[lambda_internal::kStorageSize];
		const VTable *vtable;
	};
	union {
		lambda_internal::alignment alignment_;
		char raw_[lambda_internal::kFullStorageSize];
		Data data_;
	};

};

template <typename Return, typename ...Args>
class lambda<Return(Args...)> final : public lambda_once<Return(Args...)> {
	using Parent = lambda_once<Return(Args...)>;

public:
	lambda() = default;

	// Move construct / assign from the same type.
	lambda(lambda<Return(Args...)> &&other) : Parent(std::move(other)) {
	}
	lambda &operator=(lambda<Return(Args...)> &&other) {
		Parent::operator=(std::move(other));
		return *this;
	}

	// Copy construct / assign from the same type.
	lambda(const lambda<Return(Args...)> &other) : Parent(other) {
	}
	lambda &operator=(const lambda<Return(Args...)> &other) {
		Parent::operator=(other);
		return *this;
	}

	// Copy / move construct / assign from an arbitrary type.
	template <typename Lambda, typename = decltype(std::declval<Lambda>()(std::declval<Args>()...))>
	lambda(Lambda other) : Parent(&lambda_internal::vtable<Lambda, Return, Args...>::instance, typename Parent::Private()) {
		lambda_internal::vtable<Lambda, Return, Args...>::construct_move_lambda_method(this->data_.storage, &other);
	}
	template <typename Lambda, typename = decltype(std::declval<Lambda>()(std::declval<Args>()...))>
	lambda &operator=(Lambda other) {
		if (this->data_.vtable) {
			this->data_.vtable->destruct(this->data_.storage);
		}
		this->data_.vtable = &lambda_internal::vtable<Lambda, Return, Args...>::instance;
		lambda_internal::vtable<Lambda, Return, Args...>::construct_move_lambda_method(this->data_.storage, &other);
		return *this;
	}

	inline Return operator()(Args... args) const {
		t_assert(this->data_.vtable != nullptr);
		return this->data_.vtable->const_call(this->data_.storage, std::forward<Args>(args)...);
	}

	void swap(lambda &other) {
		if (this != &other) {
			std::swap(*this, other);
		}
	}

};

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

// Pass lambda instead of a Qt void() slot.

class lambda_slot_wrap : public QObject {
	Q_OBJECT

public:
	lambda_slot_wrap(QObject *parent, lambda<void()> lambda) : QObject(parent), _lambda(std::move(lambda)) {
	}

public slots :
	void action() {
		_lambda();
	}

private:
	lambda<void()> _lambda;

};

inline lambda_slot_wrap *lambda_slot(QObject *parent, lambda<void()> lambda) {
	return new lambda_slot_wrap(parent, std::move(lambda));
}

class lambda_slot_once_wrap : public QObject {
	Q_OBJECT

public:
	lambda_slot_once_wrap(QObject *parent, lambda_once<void()> lambda) : QObject(parent), _lambda(std::move(lambda)) {
	}

	public slots :
		void action() {
		_lambda();
		delete this;
	}

private:
	lambda_once<void()> _lambda;

};

inline lambda_slot_once_wrap *lambda_slot_once(QObject *parent, lambda_once<void()> lambda) {
	return new lambda_slot_once_wrap(parent, std::move(lambda));
}

} // namespace base

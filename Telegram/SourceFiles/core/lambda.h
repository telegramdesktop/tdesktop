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

#include "core/stl_subset.h"

namespace base {
namespace internal {

	template <typename Return, typename ...Args>
	struct lambda_wrap_helper_base {
		using construct_copy_other_type = void(*)(void *, const void *); // dst, src
		using construct_move_other_type = void(*)(void *, void *); // dst, src
		using call_type = Return(*)(const void *, Args...);
		using destruct_type = void(*)(const void *);

		lambda_wrap_helper_base() = delete;
		lambda_wrap_helper_base(const lambda_wrap_helper_base &other) = delete;
		lambda_wrap_helper_base &operator=(const lambda_wrap_helper_base &other) = delete;

		lambda_wrap_helper_base(
			construct_copy_other_type construct_copy_other,
			construct_move_other_type construct_move_other,
			call_type call,
			destruct_type destruct)
			: construct_copy_other(construct_copy_other)
			, construct_move_other(construct_move_other)
			, call(call)
			, destruct(destruct) {
		}

		const construct_copy_other_type construct_copy_other;
		const construct_move_other_type construct_move_other;
		const call_type call;
		const destruct_type destruct;

		static constexpr size_t kFullStorageSize = 24U + sizeof(void*);
		static constexpr size_t kStorageSize = kFullStorageSize - sizeof(void*);
		using alignment = uint64;

		template <typename Lambda>
		using IsLarge = std_::integral_constant<bool, !(sizeof(std_::decay_simple_t<Lambda>) <= kStorageSize)>;

	protected:
		static void bad_construct_copy(void *lambda, const void *source) {
			t_assert(!"base::lambda bad_construct_copy() called!");
		}

	};

	template <typename Return, typename ...Args>
	struct lambda_wrap_empty : public lambda_wrap_helper_base<Return, Args...> {
		static void construct_copy_other_method(void *lambda, const void *source) {
		}
		static void construct_move_other_method(void *lambda, void *source) {
		}
		static Return call_method(const void *lambda, Args... args) {
			t_assert(!"base::lambda empty call_method() called!");
			return Return();
		}
		static void destruct_method(const void *lambda) {
		}
		lambda_wrap_empty() : lambda_wrap_helper_base<Return, Args...>(
			&lambda_wrap_empty::construct_copy_other_method,
			&lambda_wrap_empty::construct_move_other_method,
			&lambda_wrap_empty::call_method,
			&lambda_wrap_empty::destruct_method) {
		}

		static const lambda_wrap_empty<Return, Args...> instance;

	};

	template <typename Return, typename ...Args>
	const lambda_wrap_empty<Return, Args...> lambda_wrap_empty<Return, Args...>::instance = {};

	template <typename Lambda, typename IsLarge, typename Return, typename ...Args> struct lambda_wrap_helper_move_impl;

	template <typename Lambda, typename Return, typename ...Args>
	struct lambda_wrap_helper_move_impl<Lambda, std_::true_type, Return, Args...> : public lambda_wrap_helper_base<Return, Args...> {
		using JustLambda = std_::decay_simple_t<Lambda>;
		using LambdaPtr = std_::unique_ptr<JustLambda>;
		using Parent = lambda_wrap_helper_base<Return, Args...>;
		static void construct_move_other_method(void *lambda, void *source) {
			auto source_lambda = static_cast<LambdaPtr*>(source);
			new (lambda) LambdaPtr(std_::move(*source_lambda));
		}
		static void construct_move_lambda_method(void *lambda, void *source) {
			auto source_lambda = static_cast<JustLambda*>(source);
			new (lambda) LambdaPtr(std_::make_unique<JustLambda>(static_cast<JustLambda&&>(*source_lambda)));
		}
		static Return call_method(const void *lambda, Args... args) {
			return (**static_cast<const LambdaPtr*>(lambda))(std_::forward<Args>(args)...);
		}
		static void destruct_method(const void *lambda) {
			static_cast<const LambdaPtr*>(lambda)->~LambdaPtr();
		}
		lambda_wrap_helper_move_impl() : Parent(
			&Parent::bad_construct_copy,
			&lambda_wrap_helper_move_impl::construct_move_other_method,
			&lambda_wrap_helper_move_impl::call_method,
			&lambda_wrap_helper_move_impl::destruct_method) {
		}

	protected:
		lambda_wrap_helper_move_impl(
			typename Parent::construct_copy_other_type construct_copy_other
		) : Parent(
			construct_copy_other,
			&lambda_wrap_helper_move_impl::construct_move_other_method,
			&lambda_wrap_helper_move_impl::call_method,
			&lambda_wrap_helper_move_impl::destruct_method) {
		}

	};

	template <typename Lambda, typename Return, typename ...Args>
	struct lambda_wrap_helper_move_impl<Lambda, std_::false_type, Return, Args...> : public lambda_wrap_helper_base<Return, Args...> {
		using JustLambda = std_::decay_simple_t<Lambda>;
		using Parent = lambda_wrap_helper_base<Return, Args...>;
		static void construct_move_other_method(void *lambda, void *source) {
			auto source_lambda = static_cast<JustLambda*>(source);
			new (lambda) JustLambda(static_cast<JustLambda&&>(*source_lambda));
		}
		static void construct_move_lambda_method(void *lambda, void *source) {
			static_assert(alignof(JustLambda) <= alignof(typename Parent::alignment), "Bad lambda alignment.");
			auto space = sizeof(JustLambda);
			auto aligned = std_::align(alignof(JustLambda), space, lambda, space);
			t_assert(aligned == lambda);
			auto source_lambda = static_cast<JustLambda*>(source);
			new (lambda) JustLambda(static_cast<JustLambda&&>(*source_lambda));
		}
		static Return call_method(const void *lambda, Args... args) {
			return (*static_cast<const JustLambda*>(lambda))(std_::forward<Args>(args)...);
		}
		static void destruct_method(const void *lambda) {
			static_cast<const JustLambda*>(lambda)->~JustLambda();
		}
		lambda_wrap_helper_move_impl() : Parent(
			&Parent::bad_construct_copy,
			&lambda_wrap_helper_move_impl::construct_move_other_method,
			&lambda_wrap_helper_move_impl::call_method,
			&lambda_wrap_helper_move_impl::destruct_method) {
		}

	protected:
		lambda_wrap_helper_move_impl(
			typename Parent::construct_copy_other_type construct_copy_other
		) : Parent(
			construct_copy_other,
			&lambda_wrap_helper_move_impl::construct_move_other_method,
			&lambda_wrap_helper_move_impl::call_method,
			&lambda_wrap_helper_move_impl::destruct_method) {
		}

	};

	template <typename Lambda, typename Return, typename ...Args>
	struct lambda_wrap_helper_move : public lambda_wrap_helper_move_impl<Lambda
		, typename lambda_wrap_helper_base<Return, Args...>::template IsLarge<Lambda>
		, Return, Args...> {
		static const lambda_wrap_helper_move instance;
	};

	template <typename Lambda, typename Return, typename ...Args>
	const lambda_wrap_helper_move<Lambda, Return, Args...> lambda_wrap_helper_move<Lambda, Return, Args...>::instance = {};

	template <typename Lambda, typename IsLarge, typename Return, typename ...Args> struct lambda_wrap_helper_copy_impl;

	template <typename Lambda, typename Return, typename ...Args>
	struct lambda_wrap_helper_copy_impl<Lambda, std_::true_type, Return, Args...> : public lambda_wrap_helper_move_impl<Lambda, std_::true_type, Return, Args...> {
		using JustLambda = std_::decay_simple_t<Lambda>;
		using LambdaPtr = std_::unique_ptr<JustLambda>;
		using Parent = lambda_wrap_helper_move_impl<Lambda, std_::true_type, Return, Args...>;
		static void construct_copy_other_method(void *lambda, const void *source) {
			auto source_lambda = static_cast<const LambdaPtr*>(source);
			new (lambda) LambdaPtr(std_::make_unique<JustLambda>(*source_lambda->get()));
		}
		static void construct_copy_lambda_method(void *lambda, const void *source) {
			auto source_lambda = static_cast<const JustLambda*>(source);
			new (lambda) LambdaPtr(std_::make_unique<JustLambda>(static_cast<const JustLambda &>(*source_lambda)));
		}
		lambda_wrap_helper_copy_impl() : Parent(&lambda_wrap_helper_copy_impl::construct_copy_other_method) {
		}

	};

	template <typename Lambda, typename Return, typename ...Args>
	struct lambda_wrap_helper_copy_impl<Lambda, std_::false_type, Return, Args...> : public lambda_wrap_helper_move_impl<Lambda, std_::false_type, Return, Args...> {
		using JustLambda = std_::decay_simple_t<Lambda>;
		using Parent = lambda_wrap_helper_move_impl<Lambda, std_::false_type, Return, Args...>;
		static void construct_copy_other_method(void *lambda, const void *source) {
			auto source_lambda = static_cast<const JustLambda*>(source);
			new (lambda) JustLambda(static_cast<const JustLambda &>(*source_lambda));
		}
		static void construct_copy_lambda_method(void *lambda, const void *source) {
			static_assert(alignof(JustLambda) <= alignof(typename Parent::alignment), "Bad lambda alignment.");
			auto space = sizeof(JustLambda);
			auto aligned = std_::align(alignof(JustLambda), space, lambda, space);
			t_assert(aligned == lambda);
			auto source_lambda = static_cast<const JustLambda*>(source);
			new (lambda) JustLambda(static_cast<const JustLambda &>(*source_lambda));
		}
		lambda_wrap_helper_copy_impl() : Parent(&lambda_wrap_helper_copy_impl::construct_copy_other_method) {
		}

	};

	template <typename Lambda, typename Return, typename ...Args>
	struct lambda_wrap_helper_copy : public lambda_wrap_helper_copy_impl<Lambda
		, typename lambda_wrap_helper_base<Return, Args...>::template IsLarge<Lambda>
		, Return, Args...> {
		static const lambda_wrap_helper_copy instance;
	};

	template <typename Lambda, typename Return, typename ...Args>
	const lambda_wrap_helper_copy<Lambda, Return, Args...> lambda_wrap_helper_copy<Lambda, Return, Args...>::instance = {};

} // namespace internal

template <typename Function> class lambda;
template <typename Function> class lambda_copy;

template <typename Return, typename ...Args>
class lambda<Return(Args...)> {
	using BaseHelper = internal::lambda_wrap_helper_base<Return, Args...>;
	using EmptyHelper = internal::lambda_wrap_empty<Return, Args...>;

	template <typename Lambda>
	using IsUnique = std_::is_same<lambda, std_::decay_simple_t<Lambda>>;
	template <typename Lambda>
	using IsWrap = std_::is_same<lambda_copy<Return(Args...)>, std_::decay_simple_t<Lambda>>;
	template <typename Lambda>
	using IsOther = std_::enable_if_t<!IsUnique<Lambda>::value && !IsWrap<Lambda>::value>;
	template <typename Lambda>
	using IsRvalue = std_::enable_if_t<std_::is_rvalue_reference<Lambda&&>::value>;

public:
	using return_type = Return;

	lambda() : helper_(&EmptyHelper::instance) {
	}

	lambda(const lambda &other) = delete;
	lambda &operator=(const lambda &other) = delete;

	lambda(lambda &&other) : helper_(other.helper_) {
		helper_->construct_move_other(storage_, other.storage_);
	}
	lambda &operator=(lambda &&other) {
		auto temp = std_::move(other);
		helper_->destruct(storage_);
		helper_ = temp.helper_;
		helper_->construct_move_other(storage_, temp.storage_);
		return *this;
	}

	void swap(lambda &other) {
		if (this != &other) std_::swap_moveable(*this, other);
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda(Lambda &&other) : helper_(&internal::lambda_wrap_helper_move<Lambda, Return, Args...>::instance) {
		internal::lambda_wrap_helper_move<Lambda, Return, Args...>::construct_move_lambda_method(storage_, &other);
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda &operator=(Lambda &&other) {
		auto temp = std_::move(other);
		helper_->destruct(storage_);
		helper_ = &internal::lambda_wrap_helper_move<Lambda, Return, Args...>::instance;
		internal::lambda_wrap_helper_move<Lambda, Return, Args...>::construct_move_lambda_method(storage_, &temp);
		return *this;
	}

	inline Return operator()(Args... args) const {
		return helper_->call(storage_, std_::forward<Args>(args)...);
	}

	explicit operator bool() const {
		return (helper_ != &EmptyHelper::instance);
	}

	~lambda() {
		helper_->destruct(storage_);
	}

protected:
	struct Private {
	};
	lambda(const BaseHelper *helper, const Private &) : helper_(helper) {
	}

	using alignment = typename BaseHelper::alignment;
	static_assert(BaseHelper::kStorageSize % sizeof(alignment) == 0, "Bad storage size.");
	alignas(typename BaseHelper::alignment) alignment storage_[BaseHelper::kStorageSize / sizeof(alignment)];
	const BaseHelper *helper_;

};

template <typename Return, typename ...Args>
class lambda_copy<Return(Args...)> : public lambda<Return(Args...)> {
	using BaseHelper = internal::lambda_wrap_helper_base<Return, Args...>;
	using Parent = lambda<Return(Args...)>;

	template <typename Lambda>
	using IsOther = std_::enable_if_t<!std_::is_same<lambda_copy, std_::decay_simple_t<Lambda>>::value>;
	template <typename Lambda>
	using IsRvalue = std_::enable_if_t<std_::is_rvalue_reference<Lambda&&>::value>;
	template <typename Lambda>
	using IsNotRvalue = std_::enable_if_t<!std_::is_rvalue_reference<Lambda&&>::value>;

public:
	lambda_copy() = default;

	lambda_copy(const lambda_copy &other) : Parent(other.helper_, typename Parent::Private()) {
		this->helper_->construct_copy_other(this->storage_, other.storage_);
	}
	lambda_copy &operator=(const lambda_copy &other) {
		auto temp = other;
		temp.swap(*this);
		return *this;
	}

	lambda_copy(lambda_copy &&other) = default;
	lambda_copy &operator=(lambda_copy &&other) = default;

	void swap(lambda_copy &other) {
		if (this != &other) std_::swap_moveable(*this, other);
	}

	lambda_copy clone() const {
		return *this;
	}

	template <typename Lambda, typename = IsOther<Lambda>>
	lambda_copy(const Lambda &other) : Parent(&internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance, typename Parent::Private()) {
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_copy_lambda_method(this->storage_, &other);
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda_copy(Lambda &&other) : Parent(&internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance, typename Parent::Private()) {
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_move_lambda_method(this->storage_, &other);
	}

	template <typename Lambda, typename = IsOther<Lambda>>
	lambda_copy &operator=(const Lambda &other) {
		auto temp = other;
		this->helper_->destruct(this->storage_);
		this->helper_ = &internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance;
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_copy_lambda_method(this->storage_, &other);
		return *this;
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda_copy &operator=(Lambda &&other) {
		auto temp = std_::move(other);
		this->helper_->destruct(this->storage_);
		this->helper_ = &internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance;
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_move_lambda_method(this->storage_, &other);
		return *this;
	}

};

// Get lambda type from a lambda template parameter.

namespace internal {

template <typename FunctionType>
struct lambda_type_resolver;

template <typename Lambda, typename R, typename ...Args>
struct lambda_type_resolver<R(Lambda::*)(Args...) const> {
	using type = lambda<R(Args...)>;
	static constexpr auto is_mutable = false;
};

template <typename Lambda, typename R, typename ...Args>
struct lambda_type_resolver<R(Lambda::*)(Args...)> {
	using type = lambda<R(Args...)>;
	static constexpr auto is_mutable = true;
};

template <typename FunctionType>
struct lambda_type_helper {
	using type = typename lambda_type_resolver<decltype(&FunctionType::operator())>::type;
};

} // namespace internal

template <typename FunctionType>
using lambda_type = typename internal::lambda_type_helper<FunctionType>::type;

// Guard lambda call by one or many QObject* weak pointers.

namespace internal {

template <int N>
class lambda_guard_creator;

template <int N, typename Lambda>
class lambda_guard_data {
public:
	using return_type = typename lambda_type<Lambda>::return_type;

	template <typename ...PointersAndLambda>
	inline lambda_guard_data(PointersAndLambda&&... qobjectsAndLambda) : _lambda(init(_pointers, std_::forward<PointersAndLambda>(qobjectsAndLambda)...)) {
	}

	inline lambda_guard_data(const lambda_guard_data &other) : _lambda(other._lambda) {
		for (auto i = 0; i != N; ++i) {
			_pointers[i] = other._pointers[i];
		}
	}

	template <typename ...Args>
	inline return_type operator()(Args&&... args) const {
		for (int i = 0; i != N; ++i) {
			if (!_pointers[i]) {
				return return_type();
			}
		}
		return _lambda(std_::forward<Args>(args)...);
	}

private:
	template <typename ...PointersAndLambda>
	Lambda init(QPointer<QObject> *pointers, QObject *qobject, PointersAndLambda&&... qobjectsAndLambda) {
		*pointers = qobject;
		return init(++pointers, std_::forward<PointersAndLambda>(qobjectsAndLambda)...);
	}
	Lambda init(QPointer<QObject> *pointers, Lambda &&lambda) {
		return std_::move(lambda);
	}

	QPointer<QObject> _pointers[N];
	Lambda _lambda;

};

template <int N, typename Lambda>
class lambda_guard {
public:
	using return_type = typename lambda_type<Lambda>::return_type;

	template <typename ...PointersAndLambda>
	inline lambda_guard(PointersAndLambda&&... qobjectsAndLambda) : _data(std_::make_unique<lambda_guard_data<N, Lambda>>(std_::forward<PointersAndLambda>(qobjectsAndLambda)...)) {
		static_assert(sizeof...(PointersAndLambda) == N + 1, "Wrong argument count!");
	}

	inline lambda_guard(const lambda_guard &&other) : _data(std_::move(other._data)) {
	}

	inline lambda_guard(lambda_guard &&other) : _data(std_::move(other._data)) {
	}

	inline lambda_guard &operator=(const lambda_guard &&other) {
		_data = std_::move(other._data);
		return *this;
	}

	inline lambda_guard &operator=(lambda_guard &&other) {
		_data = std_::move(other._data);
		return *this;
	}

	template <typename ...Args>
	inline return_type operator()(Args&&... args) const {
		return (*_data)(std_::forward<Args>(args)...);
	}

	bool isNull() const {
		return !_data;
	}

	lambda_guard clone() const {
		return lambda_guard(*this);
	}

private:
	inline lambda_guard(const lambda_guard &other) : _data(std_::make_unique<lambda_guard_data<N, Lambda>>(static_cast<const lambda_guard_data<N, Lambda> &>(*other._data))) {
	}

	mutable std_::unique_ptr<lambda_guard_data<N, Lambda>> _data;

};

template <int N, int K, typename ...PointersAndLambda>
struct lambda_guard_type;

template <int N, int K, typename Pointer, typename ...PointersAndLambda>
struct lambda_guard_type<N, K, Pointer, PointersAndLambda...> {
	using type = typename lambda_guard_type<N, K - 1, PointersAndLambda...>::type;
};

template <int N, typename Lambda>
struct lambda_guard_type<N, 0, Lambda> {
	using type = lambda_guard<N, Lambda>;
};

template <typename ...PointersAndLambda>
struct lambda_guard_type_helper {
	static constexpr int N = sizeof...(PointersAndLambda);
	using type = typename lambda_guard_type<N - 1, N - 1, PointersAndLambda...>::type;
};

template <typename ...PointersAndLambda>
using lambda_guard_t = typename lambda_guard_type_helper<PointersAndLambda...>::type;

template <int N, typename Lambda>
struct lambda_type_helper<lambda_guard<N, Lambda>> {
	using type = typename lambda_type_helper<Lambda>::type;
};

} // namespace internal

template <typename ...PointersAndLambda>
inline internal::lambda_guard_t<PointersAndLambda...> lambda_guarded(PointersAndLambda&&... qobjectsAndLambda) {
	static_assert(sizeof...(PointersAndLambda) > 0, "Lambda should be passed here.");
	return internal::lambda_guard_t<PointersAndLambda...>(std_::forward<PointersAndLambda>(qobjectsAndLambda)...);
}

// Pass lambda instead of a Qt void() slot.

class lambda_slot_wrap : public QObject {
	Q_OBJECT

public:
	lambda_slot_wrap(QObject *parent, lambda<void()> &&lambda) : QObject(parent), _lambda(std_::move(lambda)) {
	}

public slots:
	void action() {
		_lambda();
	}

private:
	lambda<void()> _lambda;

};

inline lambda_slot_wrap *lambda_slot(QObject *parent, lambda<void()> &&lambda) {
	return new lambda_slot_wrap(parent, std_::move(lambda));
}

class lambda_slot_once_wrap : public QObject {
	Q_OBJECT

public:
	lambda_slot_once_wrap(QObject *parent, lambda<void()> &&lambda) : QObject(parent), _lambda(std_::move(lambda)) {
	}

public slots :
	void action() {
		_lambda();
		delete this;
	}

private:
	lambda<void()> _lambda;

};

inline lambda_slot_once_wrap *lambda_slot_once(QObject *parent, lambda<void()> &&lambda) {
	return new lambda_slot_once_wrap(parent, std_::move(lambda));
}

} // namespace base

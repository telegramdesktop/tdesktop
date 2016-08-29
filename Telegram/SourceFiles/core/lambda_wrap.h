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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

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

	static constexpr size_t kFullStorageSize = 40U;
	static constexpr size_t kStorageSize = kFullStorageSize - sizeof(void*);

	template <typename Lambda>
	using IsLarge = std_::integral_constant<bool, !(sizeof(std_::decay_simple_t<Lambda>) <= kStorageSize)>;

protected:
	static void bad_construct_copy(void *lambda, const void *source) {
		throw std::exception();
	}

};

template <typename Return, typename ...Args>
struct lambda_wrap_empty : public lambda_wrap_helper_base<Return, Args...> {
	static void construct_copy_other_method(void *lambda, const void *source) {
	}
	static void construct_move_other_method(void *lambda, void *source) {
	}
	static Return call_method(const void *lambda, Args... args) {
		throw std::exception();
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
const lambda_wrap_empty<Return, Args...> lambda_wrap_empty<Return, Args...>::instance;

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

template <typename Function> class lambda_unique;
template <typename Function> class lambda_wrap;

template <typename Return, typename ...Args>
class lambda_unique<Return(Args...)> {
	using BaseHelper = internal::lambda_wrap_helper_base<Return, Args...>;
	using EmptyHelper = internal::lambda_wrap_empty<Return, Args...>;

	template <typename Lambda>
	using IsUnique = std_::is_same<lambda_unique, std_::decay_simple_t<Lambda>>;
	template <typename Lambda>
	using IsWrap = std_::is_same<lambda_wrap<Return(Args...)>, std_::decay_simple_t<Lambda>>;
	template <typename Lambda>
	using IsOther = std_::enable_if_t<!IsUnique<Lambda>::value && !IsWrap<Lambda>::value>;
	template <typename Lambda>
	using IsRvalue = std_::enable_if_t<std_::is_rvalue_reference<Lambda&&>::value>;

public:
	lambda_unique() : helper_(&EmptyHelper::instance) {
	}

	lambda_unique(const lambda_unique &other) = delete;
	lambda_unique &operator=(const lambda_unique &other) = delete;

	lambda_unique(lambda_unique &&other) : helper_(other.helper_) {
		helper_->construct_move_other(storage_, other.storage_);
	}
	lambda_unique &operator=(lambda_unique &&other) {
		auto temp = std_::move(other);
		helper_->destruct(storage_);
		helper_ = temp.helper_;
		helper_->construct_move_other(storage_, temp.storage_);
		return *this;
	}

	void swap(lambda_unique &other) {
		if (this != &other) {
			lambda_unique temp = std_::move(other);
			other = std_::move(*this);
			*this = std_::move(other);
		}
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda_unique(Lambda &&other) : helper_(&internal::lambda_wrap_helper_move<Lambda, Return, Args...>::instance) {
		internal::lambda_wrap_helper_move<Lambda, Return, Args...>::construct_move_lambda_method(storage_, &other);
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda_unique &operator=(Lambda &&other) {
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

	~lambda_unique() {
		helper_->destruct(storage_);
	}

protected:
	struct Private {
	};
	lambda_unique(const BaseHelper *helper, const Private &) : helper_(helper) {
	}

	const BaseHelper *helper_;

	static_assert(BaseHelper::kStorageSize % sizeof(void*) == 0, "Bad pointer size.");
	void *(storage_[BaseHelper::kStorageSize / sizeof(void*)]);

};

template <typename Return, typename ...Args>
class lambda_wrap<Return(Args...)> : public lambda_unique<Return(Args...)> {
	using BaseHelper = internal::lambda_wrap_helper_base<Return, Args...>;
	using Parent = lambda_unique<Return(Args...)>;

	template <typename Lambda>
	using IsOther = std_::enable_if_t<!std_::is_same<lambda_wrap, std_::decay_simple_t<Lambda>>::value>;
	template <typename Lambda>
	using IsRvalue = std_::enable_if_t<std_::is_rvalue_reference<Lambda&&>::value>;
	template <typename Lambda>
	using IsNotRvalue = std_::enable_if_t<!std_::is_rvalue_reference<Lambda&&>::value>;

public:
	lambda_wrap() = default;

	lambda_wrap(const lambda_wrap &other) : Parent(other.helper_, typename Parent::Private()) {
		this->helper_->construct_copy_other(this->storage_, other.storage_);
	}
	lambda_wrap &operator=(const lambda_wrap &other) {
		auto temp = other;
		temp.swap(*this);
		return *this;
	}

	lambda_wrap(lambda_wrap &&other) = default;
	lambda_wrap &operator=(lambda_wrap &&other) = default;

	void swap(lambda_wrap &other) {
		if (this != &other) {
			lambda_wrap temp = std_::move(other);
			other = std_::move(*this);
			*this = std_::move(other);
		}
	}

	template <typename Lambda, typename = IsOther<Lambda>>
	lambda_wrap(const Lambda &other) : Parent(&internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance, typename Parent::Private()) {
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_copy_lambda_method(this->storage_, &other);
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda_wrap(Lambda &&other) : Parent(&internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance, typename Parent::Private()) {
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_move_lambda_method(this->storage_, &other);
	}

	template <typename Lambda, typename = IsOther<Lambda>>
	lambda_wrap &operator=(const Lambda &other) {
		auto temp = other;
		this->helper_->destruct(this->storage_);
		this->helper_ = &internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance;
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_move_lambda_method(this->storage_, &other);
		return *this;
	}

	template <typename Lambda, typename = IsOther<Lambda>, typename = IsRvalue<Lambda>>
	lambda_wrap &operator=(Lambda &&other) {
		auto temp = std_::move(other);
		this->helper_->destruct(this->storage_);
		this->helper_ = &internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::instance;
		internal::lambda_wrap_helper_copy<Lambda, Return, Args...>::construct_move_lambda_method(this->storage_, &other);
		return *this;
	}

};

} // namespace base

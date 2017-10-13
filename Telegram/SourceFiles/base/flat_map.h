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

#include <deque>
#include "base/optional.h"
#include "base/algorithm.h"

namespace base {

template <
	typename Key,
	typename Type,
	typename Compare = std::less<>>
class flat_map;

template <
	typename Key,
	typename Type,
	typename Compare = std::less<>>
class flat_multi_map;

template <
	typename Key,
	typename Type,
	typename Compare,
	typename iterator_impl,
	typename pointer_impl,
	typename reference_impl>
class flat_multi_map_iterator_base_impl;

template <
	typename Key,
	typename Type,
	typename Compare,
	typename iterator_impl,
	typename pointer_impl,
	typename reference_impl>
class flat_multi_map_iterator_base_impl {
public:
	using iterator_category = typename iterator_impl::iterator_category;

	using value_type = typename flat_multi_map<Key, Type, Compare>::value_type;
	using difference_type = typename iterator_impl::difference_type;
	using pointer = pointer_impl;
	using const_pointer = typename flat_multi_map<Key, Type, Compare>::const_pointer;
	using reference = reference_impl;
	using const_reference = typename flat_multi_map<Key, Type, Compare>::const_reference;

	flat_multi_map_iterator_base_impl(iterator_impl impl = iterator_impl())
		: _impl(impl) {
	}

	reference operator*() {
		return *_impl;
	}
	const_reference operator*() const {
		return *_impl;
	}
	pointer operator->() {
		return std::addressof(**this);
	}
	const_pointer operator->() const {
		return std::addressof(**this);
	}
	flat_multi_map_iterator_base_impl &operator++() {
		++_impl;
		return *this;
	}
	flat_multi_map_iterator_base_impl operator++(int) {
		return _impl++;
	}
	flat_multi_map_iterator_base_impl &operator--() {
		--_impl;
		return *this;
	}
	flat_multi_map_iterator_base_impl operator--(int) {
		return _impl--;
	}
	flat_multi_map_iterator_base_impl &operator+=(difference_type offset) {
		_impl += offset;
		return *this;
	}
	flat_multi_map_iterator_base_impl operator+(difference_type offset) const {
		return _impl + offset;
	}
	flat_multi_map_iterator_base_impl &operator-=(difference_type offset) {
		_impl -= offset;
		return *this;
	}
	flat_multi_map_iterator_base_impl operator-(difference_type offset) const {
		return _impl - offset;
	}
	template <
		typename other_iterator_impl,
		typename other_pointer_impl,
		typename other_reference_impl>
	difference_type operator-(
			const flat_multi_map_iterator_base_impl<
				Key,
				Type,
				Compare,
				other_iterator_impl,
				other_pointer_impl,
				other_reference_impl> &right) const {
		return _impl - right._impl;
	}
	reference operator[](difference_type offset) {
		return _impl[offset];
	}
	const_reference operator[](difference_type offset) const {
		return _impl[offset];
	}

	template <
		typename other_iterator_impl,
		typename other_pointer_impl,
		typename other_reference_impl>
	bool operator==(
			const flat_multi_map_iterator_base_impl<
				Key,
				Type,
				Compare,
				other_iterator_impl,
				other_pointer_impl,
				other_reference_impl> &right) const {
		return _impl == right._impl;
	}
	template <
		typename other_iterator_impl,
		typename other_pointer_impl,
		typename other_reference_impl>
	bool operator!=(
			const flat_multi_map_iterator_base_impl<
				Key,
				Type,
				Compare,
				other_iterator_impl,
				other_pointer_impl,
				other_reference_impl> &right) const {
		return _impl != right._impl;
	}
	template <
		typename other_iterator_impl,
		typename other_pointer_impl,
		typename other_reference_impl>
	bool operator<(
			const flat_multi_map_iterator_base_impl<
				Key,
				Type,
				Compare,
				other_iterator_impl,
				other_pointer_impl,
				other_reference_impl> &right) const {
		return _impl < right._impl;
	}

private:
	iterator_impl _impl;
	friend class flat_multi_map<Key, Type, Compare>;

	template <
		typename OtherKey,
		typename OtherType,
		typename OtherCompare,
		typename other_iterator_impl,
		typename other_pointer_impl,
		typename other_reference_impl>
	friend class flat_multi_map_iterator_base_impl;

};

template <typename Key, typename Type, typename Compare>
class flat_multi_map {
	class key_const_wrap {
	public:
		constexpr key_const_wrap(const Key &value) : _value(value) {
		}
		constexpr key_const_wrap(Key &&value) : _value(std::move(value)) {
		}
		inline constexpr operator const Key&() const {
			return _value;
		}

	private:
		Key _value;

	};
	using pair_type = std::pair<key_const_wrap, Type>;

	class compare {
	public:
		template <
			typename OtherType1,
			typename OtherType2,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType1>, key_const_wrap> &&
				!std::is_same_v<std::decay_t<OtherType1>, pair_type> &&
				!std::is_same_v<std::decay_t<OtherType2>, key_const_wrap> &&
				!std::is_same_v<std::decay_t<OtherType2>, pair_type>>>
		inline constexpr auto operator()(
				OtherType1 &&a,
				OtherType2 &b) const {
			return Compare()(
				std::forward<OtherType1>(a),
				std::forward<OtherType2>(b));
		}
		inline constexpr auto operator()(
				const key_const_wrap &a,
				const key_const_wrap &b) const {
			return operator()(
				static_cast<const Key&>(a),
				static_cast<const Key&>(b));
		}
		template <
			typename OtherType,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType>, key_const_wrap> &&
				!std::is_same_v<std::decay_t<OtherType>, pair_type>>>
		inline constexpr auto operator()(
				const key_const_wrap &a,
				OtherType &&b) const {
			return operator()(
				static_cast<const Key&>(a),
				std::forward<OtherType>(b));
		}
		template <
			typename OtherType,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType>, key_const_wrap> &&
				!std::is_same_v<std::decay_t<OtherType>, pair_type>>>
		inline constexpr auto operator()(
				OtherType &&a,
				const key_const_wrap &b) const {
			return operator()(
				std::forward<OtherType>(a),
				static_cast<const Key&>(b));
		}
		inline constexpr auto operator()(
				const pair_type &a,
				const pair_type &b) const {
			return operator()(a.first, b.first);
		}
		template <
			typename OtherType,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType>, pair_type>>>
		inline constexpr auto operator()(
				const pair_type &a,
				OtherType &&b) const {
			return operator()(a.first, std::forward<OtherType>(b));
		}
		template <
			typename OtherType,
			typename = std::enable_if_t<
				!std::is_same_v<std::decay_t<OtherType>, pair_type>>>
		inline constexpr auto operator()(
				OtherType &&a,
				const pair_type &b) const {
			return operator()(std::forward<OtherType>(a), b.first);
		}

	};

	using impl = std::deque<pair_type>;

	using iterator_base = flat_multi_map_iterator_base_impl<
		Key,
		Type,
		Compare,
		typename impl::iterator,
		pair_type*,
		pair_type&>;
	using const_iterator_base = flat_multi_map_iterator_base_impl<
		Key,
		Type,
		Compare,
		typename impl::const_iterator,
		const pair_type*,
		const pair_type&>;
	using reverse_iterator_base = flat_multi_map_iterator_base_impl<
		Key,
		Type,
		Compare,
		typename impl::reverse_iterator,
		pair_type*,
		pair_type&>;
	using const_reverse_iterator_base = flat_multi_map_iterator_base_impl<
		Key,
		Type,
		Compare,
		typename impl::const_reverse_iterator,
		const pair_type*,
		const pair_type&>;

public:
	using value_type = pair_type;
	using size_type = typename impl::size_type;
	using difference_type = typename impl::difference_type;
	using pointer = pair_type*;
	using const_pointer = const pair_type*;
	using reference = pair_type&;
	using const_reference = const pair_type&;

	class const_iterator;
	class iterator : public iterator_base {
	public:
		using iterator_base::iterator_base;
		iterator(const iterator_base &other) : iterator_base(other) {
		}
		friend class const_iterator;

	};
	class const_iterator : public const_iterator_base {
	public:
		using const_iterator_base::const_iterator_base;
		const_iterator(const_iterator_base other) : const_iterator_base(other) {
		}
		const_iterator(const iterator &other) : const_iterator_base(other._impl) {
		}

	};
	class const_reverse_iterator;
	class reverse_iterator : public reverse_iterator_base {
	public:
		using reverse_iterator_base::reverse_iterator_base;
		reverse_iterator(reverse_iterator_base other) : reverse_iterator_base(other) {
		}
		friend class const_reverse_iterator;

	};
	class const_reverse_iterator : public const_reverse_iterator_base {
	public:
		using const_reverse_iterator_base::const_reverse_iterator_base;
		const_reverse_iterator(const_reverse_iterator_base other) : const_reverse_iterator_base(other) {
		}
		const_reverse_iterator(const reverse_iterator &other) : const_reverse_iterator_base(other._impl) {
		}

	};

	size_type size() const {
		return _impl.size();
	}
	bool empty() const {
		return _impl.empty();
	}
	void clear() {
		_impl.clear();
	}

	iterator begin() {
		return _impl.begin();
	}
	iterator end() {
		return _impl.end();
	}
	const_iterator begin() const {
		return _impl.begin();
	}
	const_iterator end() const {
		return _impl.end();
	}
	const_iterator cbegin() const {
		return _impl.cbegin();
	}
	const_iterator cend() const {
		return _impl.cend();
	}
	reverse_iterator rbegin() {
		return _impl.rbegin();
	}
	reverse_iterator rend() {
		return _impl.rend();
	}
	const_reverse_iterator rbegin() const {
		return _impl.rbegin();
	}
	const_reverse_iterator rend() const {
		return _impl.rend();
	}
	const_reverse_iterator crbegin() const {
		return _impl.crbegin();
	}
	const_reverse_iterator crend() const {
		return _impl.crend();
	}

	reference front() {
		return *begin();
	}
	const_reference front() const {
		return *begin();
	}
	reference back() {
		return *(end() - 1);
	}
	const_reference back() const {
		return *(end() - 1);
	}

	iterator insert(const value_type &value) {
		if (empty() || compare()(value.first, front().first)) {
			_impl.push_front(value);
			return begin();
		} else if (!compare()(value.first, back().first)) {
			_impl.push_back(value);
			return (end() - 1);
		}
		auto where = getUpperBound(value.first);
		return _impl.insert(where, value);
	}
	iterator insert(value_type &&value) {
		if (empty() || compare()(value.first, front().first)) {
			_impl.push_front(std::move(value));
			return begin();
		} else if (!compare()(value.first, back().first)) {
			_impl.push_back(std::move(value));
			return (end() - 1);
		}
		auto where = getUpperBound(value.first);
		return _impl.insert(where, std::move(value));
	}
	template <typename... Args>
	iterator emplace(Args&&... args) {
		return insert(value_type(std::forward<Args>(args)...));
	}

	bool removeOne(const Key &key) {
		if (empty()
			|| compare()(key, front().first)
			|| compare()(back().first, key)) {
			return false;
		}
		auto where = getLowerBound(key);
		if (compare()(key, where->first)) {
			return false;
		}
		_impl.erase(where);
		return true;
	}
	int removeAll(const Key &key) {
		if (empty()
			|| compare()(key, front().first)
			|| compare()(back().first, key)) {
			return 0;
		}
		auto range = getEqualRange(key);
		if (range.first == range.second) {
			return 0;
		}
		_impl.erase(range.first, range.second);
		return (range.second - range.first);
	}

	iterator erase(const_iterator where) {
		return _impl.erase(where._impl);
	}
	iterator erase(const_iterator from, const_iterator till) {
		return _impl.erase(from._impl, till._impl);
	}

	iterator findFirst(const Key &key) {
		if (empty()
			|| compare()(key, front().first)
			|| compare()(back().first, key)) {
			return end();
		}
		auto where = getLowerBound(key);
		return compare()(key, where->first) ? _impl.end() : where;
	}

	const_iterator findFirst(const Key &key) const {
		if (empty()
			|| compare()(key, front().first)
			|| compare()(back().first, key)) {
			return end();
		}
		auto where = getLowerBound(key);
		return compare()(key, where->first) ? _impl.end() : where;
	}

	bool contains(const Key &key) const {
		return findFirst(key) != end();
	}
	int count(const Key &key) const {
		if (empty()
			|| compare()(key, front().first)
			|| compare()(back().first, key)) {
			return 0;
		}
		auto range = getEqualRange(key);
		return (range.second - range.first);
	}

private:
	impl _impl;
	friend class flat_map<Key, Type, Compare>;

	typename impl::iterator getLowerBound(const Key &key) {
		return base::lower_bound(_impl, key, compare());
	}
	typename impl::const_iterator getLowerBound(const Key &key) const {
		return base::lower_bound(_impl, key, compare());
	}
	typename impl::iterator getUpperBound(const Key &key) {
		return base::upper_bound(_impl, key, compare());
	}
	typename impl::const_iterator getUpperBound(const Key &key) const {
		return base::upper_bound(_impl, key, compare());
	}
	std::pair<
		typename impl::iterator,
		typename impl::iterator
	> getEqualRange(const Key &key) {
		return base::equal_range(_impl, key, compare());
	}
	std::pair<
		typename impl::const_iterator,
		typename impl::const_iterator
	> getEqualRange(const Key &key) const {
		return base::equal_range(_impl, key, compare());
	}

};

template <typename Key, typename Type, typename Compare>
class flat_map : private flat_multi_map<Key, Type, Compare> {
	using parent = flat_multi_map<Key, Type, Compare>;
	using compare = typename parent::compare;
	using pair_type = typename parent::pair_type;

public:
	using value_type = typename parent::value_type;
	using size_type = typename parent::size_type;
	using difference_type = typename parent::difference_type;
	using pointer = typename parent::pointer;
	using const_pointer = typename parent::const_pointer;
	using reference = typename parent::reference;
	using const_reference = typename parent::const_reference;
	using iterator = typename parent::iterator;
	using const_iterator = typename parent::const_iterator;
	using reverse_iterator = typename parent::reverse_iterator;
	using const_reverse_iterator = typename parent::const_reverse_iterator;

	using parent::parent;
	using parent::size;
	using parent::empty;
	using parent::clear;
	using parent::begin;
	using parent::end;
	using parent::cbegin;
	using parent::cend;
	using parent::rbegin;
	using parent::rend;
	using parent::crbegin;
	using parent::crend;
	using parent::front;
	using parent::back;
	using parent::erase;
	using parent::contains;

	std::pair<iterator, bool> insert(const value_type &value) {
		if (this->empty() || compare()(value.first, this->front().first)) {
			this->_impl.push_front(value);
			return { this->begin(), true };
		} else if (compare()(this->back().first, value.first)) {
			this->_impl.push_back(value);
			return { this->end() - 1, true };
		}
		auto where = this->getLowerBound(value.first);
		if (compare()(value.first, where->first)) {
			return { this->_impl.insert(where, value), true };
		}
		return { where, false };
	}
	std::pair<iterator, bool> insert(value_type &&value) {
		if (this->empty() || compare()(value.first, this->front().first)) {
			this->_impl.push_front(std::move(value));
			return { this->begin(), true };
		} else if (compare()(this->back().first, value.first)) {
			this->_impl.push_back(std::move(value));
			return { this->end() - 1, true };
		}
		auto where = this->getLowerBound(value.first);
		if (compare()(value.first, where->first)) {
			return { this->_impl.insert(where, std::move(value)), true };
		}
		return { where, false };
	}
	template <typename... Args>
	std::pair<iterator, bool> emplace(
			const Key &key,
			Args&&... args) {
		return this->insert(value_type(
			key,
			Type(std::forward<Args>(args)...)));
	}
	template <typename... Args>
	std::pair<iterator, bool> try_emplace(
			const Key &key,
			Args&&... args) {
		if (this->empty() || compare()(key, this->front().first)) {
			this->_impl.push_front(value_type(
				key,
				Type(std::forward<Args>(args)...)));
			return { this->begin(), true };
		} else if (compare()(this->back().first, key)) {
			this->_impl.push_back(value_type(
				key,
				Type(std::forward<Args>(args)...)));
			return { this->end() - 1, true };
		}
		auto where = this->getLowerBound(key);
		if (compare()(key, where->first)) {
			return {
				this->_impl.insert(
					where,
					value_type(
						key,
						Type(std::forward<Args>(args)...))),
				true
			};
		}
		return { where, false };
	}

	bool remove(const Key &key) {
		return this->removeOne(key);
	}

	iterator find(const Key &key) {
		return this->findFirst(key);
	}
	const_iterator find(const Key &key) const {
		return this->findFirst(key);
	}

	Type &operator[](const Key &key) {
		if (this->empty() || compare()(key, this->front().first)) {
			this->_impl.push_front({ key, Type() });
			return this->front().second;
		} else if (compare()(this->back().first, key)) {
			this->_impl.push_back({ key, Type() });
			return this->back().second;
		}
		auto where = this->getLowerBound(key);
		if (compare()(key, where->first)) {
			return this->_impl.insert(where, { key, Type() })->second;
		}
		return where->second;
	}

	optional<Type> take(const Key &key) {
		auto it = find(key);
		if (it == this->end()) {
			return base::none;
		}
		auto result = std::move(it->second);
		this->erase(it);
		return std::move(result);
	}

};

} // namespace base

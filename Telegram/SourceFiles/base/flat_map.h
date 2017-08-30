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

namespace base {

template <typename Key, typename Type>
class flat_map;

template <typename Key, typename Type>
class flat_multi_map;

template <typename Key, typename Type, typename iterator_impl, typename pointer_impl, typename reference_impl>
class flat_multi_map_iterator_base_impl;

template <typename Key, typename Type, typename iterator_impl, typename pointer_impl, typename reference_impl>
class flat_multi_map_iterator_base_impl {
public:
	using iterator_category = typename iterator_impl::iterator_category;

	using value_type = typename flat_multi_map<Key, Type>::value_type;
	using difference_type = typename iterator_impl::difference_type;
	using pointer = pointer_impl;
	using const_pointer = typename flat_multi_map<Key, Type>::const_pointer;
	using reference = reference_impl;
	using const_reference = typename flat_multi_map<Key, Type>::const_reference;

	flat_multi_map_iterator_base_impl(iterator_impl impl = iterator_impl()) : _impl(impl) {
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
	difference_type operator-(const flat_multi_map_iterator_base_impl &right) const {
		return _impl - right._impl;
	}
	reference operator[](difference_type offset) {
		return _impl[offset];
	}
	const_reference operator[](difference_type offset) const {
		return _impl[offset];
	}

	bool operator==(const flat_multi_map_iterator_base_impl &right) const {
		return _impl == right._impl;
	}
	bool operator!=(const flat_multi_map_iterator_base_impl &right) const {
		return _impl != right._impl;
	}
	bool operator<(const flat_multi_map_iterator_base_impl &right) const {
		return _impl < right._impl;
	}

private:
	iterator_impl _impl;
	friend class flat_multi_map<Key, Type>;

};

template <typename Key, typename Type>
class flat_multi_map {
	using self = flat_multi_map<Key, Type>;
	class key_const_wrap {
	public:
		key_const_wrap(const Key &value) : _value(value) {
		}
		key_const_wrap(Key &&value) : _value(std::move(value)) {
		}
		inline operator const Key&() const {
			return _value;
		}

		friend inline bool operator<(const Key &a, const key_const_wrap &b) {
			return a < ((const Key&)b);
		}
		friend inline bool operator<(const key_const_wrap &a, const Key &b) {
			return ((const Key&)a) < b;
		}
		friend inline bool operator<(const key_const_wrap &a, const key_const_wrap &b) {
			return ((const Key&)a) < ((const Key&)b);
		}

	private:
		Key _value;

	};

	using pair_type = std::pair<key_const_wrap, Type>;
	using impl = std::deque<pair_type>;

	using iterator_base = flat_multi_map_iterator_base_impl<Key, Type, typename impl::iterator, pair_type*, pair_type&>;
	using const_iterator_base = flat_multi_map_iterator_base_impl<Key, Type, typename impl::const_iterator, const pair_type*, const pair_type&>;
	using reverse_iterator_base = flat_multi_map_iterator_base_impl<Key, Type, typename impl::reverse_iterator, pair_type*, pair_type&>;
	using const_reverse_iterator_base = flat_multi_map_iterator_base_impl<Key, Type, typename impl::const_reverse_iterator, const pair_type*, const pair_type&>;

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
		if (empty() || (value.first < front().first)) {
			_impl.push_front(value);
			return begin();
		} else if (!(value.first < back().first)) {
			_impl.push_back(value);
			return (end() - 1);
		}
		auto where = getUpperBound(value.first);
		return _impl.insert(where, value);
	}
	iterator insert(value_type &&value) {
		if (empty() || (value.first < front().first)) {
			_impl.push_front(std::move(value));
			return begin();
		} else if (!(value.first < back().first)) {
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
		if (empty() || (key < front().first) || (back().first < key)) {
			return false;
		}
		auto where = getLowerBound(key);
		if (key < where->first) {
			return false;
		}
		_impl.erase(where);
		return true;
	}
	int removeAll(const Key &key) {
		if (empty() || (key < front().first) || (back().first < key)) {
			return 0;
		}
		auto range = getEqualRange(key);
		if (range.first == range.second) {
			return 0;
		}
		_impl.erase(range.first, range.second);
		return (range.second - range.first);
	}

	iterator erase(iterator where) {
		return _impl.erase(where._impl);
	}
	iterator erase(iterator from, iterator till) {
		return _impl.erase(from._impl, till._impl);
	}

	iterator findFirst(const Key &key) {
		if (empty() || (key < front().first) || (back().first < key)) {
			return end();
		}
		auto where = getLowerBound(key);
		return (key < where->first) ? _impl.end() : where;
	}

	const_iterator findFirst(const Key &key) const {
		if (empty() || (key < front().first) || (back().first < key)) {
			return end();
		}
		auto where = getLowerBound(key);
		return (key < where->first) ? _impl.end() : where;
	}

	bool contains(const Key &key) const {
		return findFirst(key) != end();
	}
	int count(const Key &key) const {
		if (empty() || (key < front().first) || (back().first < key)) {
			return 0;
		}
		auto range = getEqualRange(key);
		return (range.second - range.first);
	}

private:
	impl _impl;
	friend class flat_map<Key, Type>;

	struct Comparator {
		inline bool operator()(const pair_type &a, const Key &b) {
			return a.first < b;
		}
		inline bool operator()(const Key &a, const pair_type &b) {
			return a < b.first;
		}
	};
	typename impl::iterator getLowerBound(const Key &key) {
		return std::lower_bound(_impl.begin(), _impl.end(), key, Comparator());
	}
	typename impl::const_iterator getLowerBound(const Key &key) const {
		return std::lower_bound(_impl.begin(), _impl.end(), key, Comparator());
	}
	typename impl::iterator getUpperBound(const Key &key) {
		return std::upper_bound(_impl.begin(), _impl.end(), key, Comparator());
	}
	typename impl::const_iterator getUpperBound(const Key &key) const {
		return std::upper_bound(_impl.begin(), _impl.end(), key, Comparator());
	}
	std::pair<typename impl::iterator, typename impl::iterator> getEqualRange(const Key &key) {
		return std::equal_range(_impl.begin(), _impl.end(), key, Comparator());
	}
	std::pair<typename impl::const_iterator, typename impl::const_iterator> getEqualRange(const Key &key) const {
		return std::equal_range(_impl.begin(), _impl.end(), key, Comparator());
	}

};

template <typename Key, typename Type>
class flat_map : public flat_multi_map<Key, Type> {
	using parent = flat_multi_map<Key, Type>;
	using pair_type = typename parent::pair_type;

public:
	using parent::parent;
	using iterator = typename parent::iterator;
	using const_iterator = typename parent::const_iterator;
	using value_type = typename parent::value_type;

	iterator insert(const value_type &value) {
		if (this->empty() || (value.first < this->front().first)) {
			this->_impl.push_front(value);
			return this->begin();
		} else if (this->back().first < value.first) {
			this->_impl.push_back(value);
			return (this->end() - 1);
		}
		auto where = this->getLowerBound(value.first);
		if (value.first < where->first) {
			return this->_impl.insert(where, value);
		}
		return this->end();
	}
	iterator insert(value_type &&value) {
		if (this->empty() || (value.first < this->front().first)) {
			this->_impl.push_front(std::move(value));
			return this->begin();
		} else if (this->back().first < value.first) {
			this->_impl.push_back(std::move(value));
			return (this->end() - 1);
		}
		auto where = this->getLowerBound(value.first);
		if (value.first < where->first) {
			return this->_impl.insert(where, std::move(value));
		}
		return this->end();
	}
	template <typename... Args>
	iterator emplace(Args&&... args) {
		return this->insert(value_type(std::forward<Args>(args)...));
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
		if (this->empty() || (key < this->front().first)) {
			this->_impl.push_front({ key, Type() });
			return this->front().second;
		} else if (this->back().first < key) {
			this->_impl.push_back({ key, Type() });
			return this->back().second;
		}
		auto where = this->getLowerBound(key);
		if (key < where->first) {
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

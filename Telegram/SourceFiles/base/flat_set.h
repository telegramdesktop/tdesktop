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

namespace base {

template <typename Type>
class flat_set;

template <typename Type>
class flat_multi_set;

template <typename Type, typename iterator_impl>
class flat_multi_set_iterator_base_impl;

template <typename Type, typename iterator_impl>
class flat_multi_set_iterator_base_impl {
public:
	using iterator_category = typename iterator_impl::iterator_category;

	using value_type = typename flat_multi_set<Type>::value_type;
	using difference_type = typename iterator_impl::difference_type;
	using pointer = typename flat_multi_set<Type>::pointer;
	using reference = typename flat_multi_set<Type>::reference;

	flat_multi_set_iterator_base_impl(iterator_impl impl = iterator_impl()) : _impl(impl) {
	}

	reference operator*() const {
		return *_impl;
	}
	pointer operator->() const {
		return std::addressof(**this);
	}
	flat_multi_set_iterator_base_impl &operator++() {
		++_impl;
		return *this;
	}
	flat_multi_set_iterator_base_impl operator++(int) {
		return _impl++;
	}
	flat_multi_set_iterator_base_impl &operator--() {
		--_impl;
		return *this;
	}
	flat_multi_set_iterator_base_impl operator--(int) {
		return _impl--;
	}
	flat_multi_set_iterator_base_impl &operator+=(difference_type offset) {
		_impl += offset;
		return *this;
	}
	flat_multi_set_iterator_base_impl operator+(difference_type offset) const {
		return _impl + offset;
	}
	flat_multi_set_iterator_base_impl &operator-=(difference_type offset) {
		_impl -= offset;
		return *this;
	}
	flat_multi_set_iterator_base_impl operator-(difference_type offset) const {
		return _impl - offset;
	}
	difference_type operator-(const flat_multi_set_iterator_base_impl &right) const {
		return _impl - right._impl;
	}
	reference operator[](difference_type offset) const {
		return _impl[offset];
	}

	bool operator==(const flat_multi_set_iterator_base_impl &right) const {
		return _impl == right._impl;
	}
	bool operator!=(const flat_multi_set_iterator_base_impl &right) const {
		return _impl != right._impl;
	}
	bool operator<(const flat_multi_set_iterator_base_impl &right) const {
		return _impl < right._impl;
	}

private:
	iterator_impl _impl;
	friend class flat_multi_set<Type>;

};

template <typename Type>
class flat_multi_set {
	using self = flat_multi_set<Type>;
	class const_wrap {
	public:
		const_wrap(const Type &value) : _value(value) {
		}
		const_wrap(Type &&value) : _value(std::move(value)) {
		}
		inline operator const Type&() const {
			return _value;
		}

		friend inline bool operator<(const Type &a, const const_wrap &b) {
			return a < ((const Type&)b);
		}
		friend inline bool operator<(const const_wrap &a, const Type &b) {
			return ((const Type&)a) < b;
		}
		friend inline bool operator<(const const_wrap &a, const const_wrap &b) {
			return ((const Type&)a) < ((const Type&)b);
		}

	private:
		Type _value;

	};

	using impl = std::deque<const_wrap>;

	using iterator_base = flat_multi_set_iterator_base_impl<Type, typename impl::iterator>;
	using const_iterator_base = flat_multi_set_iterator_base_impl<Type, typename impl::const_iterator>;
	using reverse_iterator_base = flat_multi_set_iterator_base_impl<Type, typename impl::reverse_iterator>;
	using const_reverse_iterator_base = flat_multi_set_iterator_base_impl<Type, typename impl::const_reverse_iterator>;

public:
	using value_type = Type;
	using size_type = typename impl::size_type;
	using difference_type = typename impl::difference_type;
	using pointer = const Type*;
	using reference = const Type&;

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

	flat_multi_set() = default;

	template <typename Iterator, typename = typename std::iterator_traits<Iterator>::iterator_category>
	flat_multi_set(Iterator first, Iterator last) : _impl(first, last) {
		std::sort(_impl.begin(), _impl.end());
	}

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

	reference front() const {
		return *begin();
	}
	reference back() const {
		return *(end() - 1);
	}

	iterator insert(const Type &value) {
		if (empty() || (value < front())) {
			_impl.push_front(value);
			return begin();
		} else if (!(value < back())) {
			_impl.push_back(value);
			return (end() - 1);
		}
		auto where = getUpperBound(value);
		return _impl.insert(where, value);
	}
	iterator insert(Type &&value) {
		if (empty() || (value < front())) {
			_impl.push_front(std::move(value));
			return begin();
		} else if (!(value < back())) {
			_impl.push_back(std::move(value));
			return (end() - 1);
		}
		auto where = getUpperBound(value);
		return _impl.insert(where, std::move(value));
	}
	template <typename... Args>
	iterator emplace(Args&&... args) {
		return insert(Type(std::forward<Args>(args)...));
	}

	bool removeOne(const Type &value) {
		if (empty() || (value < front()) || (back() < value)) {
			return false;
		}
		auto where = getLowerBound(value);
		if (value < *where) {
			return false;
		}
		_impl.erase(where);
		return true;
	}
	int removeAll(const Type &value) {
		if (empty() || (value < front()) || (back() < value)) {
			return 0;
		}
		auto range = getEqualRange(value);
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

	iterator findFirst(const Type &value) {
		if (empty() || (value < front()) || (back() < value)) {
			return end();
		}
		auto where = getLowerBound(value);
		return (value < *where) ? _impl.end() : where;
	}

	const_iterator findFirst(const Type &value) const {
		if (empty() || (value < front()) || (back() < value)) {
			return end();
		}
		auto where = getLowerBound(value);
		return (value < *where) ? _impl.end() : where;
	}

	bool contains(const Type &value) const {
		return findFirst(value) != end();
	}
	int count(const Type &value) const {
		if (empty() || (value < front()) || (back() < value)) {
			return 0;
		}
		auto range = getEqualRange(value);
		return (range.second - range.first);
	}

private:
	impl _impl;
	friend class flat_set<Type>;

	typename impl::iterator getLowerBound(const Type &value) {
		return std::lower_bound(_impl.begin(), _impl.end(), value);
	}
	typename impl::const_iterator getLowerBound(const Type &value) const {
		return std::lower_bound(_impl.begin(), _impl.end(), value);
	}
	typename impl::iterator getUpperBound(const Type &value) {
		return std::upper_bound(_impl.begin(), _impl.end(), value);
	}
	typename impl::const_iterator getUpperBound(const Type &value) const {
		return std::upper_bound(_impl.begin(), _impl.end(), value);
	}
	std::pair<typename impl::iterator, typename impl::iterator> getEqualRange(const Type &value) {
		return std::equal_range(_impl.begin(), _impl.end(), value);
	}
	std::pair<typename impl::const_iterator, typename impl::const_iterator> getEqualRange(const Type &value) const {
		return std::equal_range(_impl.begin(), _impl.end(), value);
	}

};

template <typename Type>
class flat_set : public flat_multi_set<Type> {
	using parent = flat_multi_set<Type>;

public:
	using parent::parent;
	using iterator = typename parent::iterator;
	using const_iterator = typename parent::const_iterator;

	flat_set() = default;

	template <typename Iterator, typename = typename std::iterator_traits<Iterator>::iterator_category>
	flat_set(Iterator first, Iterator last) : parent(first, last) {
		this->_impl.erase(std::unique(this->_impl.begin(), this->_impl.end(), [](auto &&a, auto &&b) {
			return !(a < b);
		}), this->_impl.end());
	}

	iterator insert(const Type &value) {
		if (this->empty() || (value < this->front())) {
			this->_impl.push_front(value);
			return this->begin();
		} else if (this->back() < value) {
			this->_impl.push_back(value);
			return (this->end() - 1);
		}
		auto where = this->getLowerBound(value);
		if (value < *where) {
			return this->_impl.insert(where, value);
		}
		return this->end();
	}
	iterator insert(Type &&value) {
		if (this->empty() || (value < this->front())) {
			this->_impl.push_front(std::move(value));
			return this->begin();
		} else if (this->back() < value) {
			this->_impl.push_back(std::move(value));
			return (this->end() - 1);
		}
		auto where = this->getLowerBound(value);
		if (value < *where) {
			return this->_impl.insert(where, std::move(value));
		}
		return this->end();
	}
	template <typename... Args>
	iterator emplace(Args&&... args) {
		return this->insert(Type(std::forward<Args>(args)...));
	}

	bool remove(const Type &value) {
		return this->removeOne(value);
	}

	iterator find(const Type &value) {
		return this->findFirst(value);
	}
	const_iterator find(const Type &value) const {
		return this->findFirst(value);
	}

};

} // namespace base

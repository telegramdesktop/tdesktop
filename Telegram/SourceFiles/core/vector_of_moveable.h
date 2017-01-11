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

// some minimal implementation of std::vector() for moveable (but not copiable) types.
namespace std_ {

template <typename T>
class vector_of_moveable {
	typedef vector_of_moveable<T> Self;
	int _size = 0, _capacity = 0;
	void *_plaindata = nullptr;

public:
	vector_of_moveable() = default;
	vector_of_moveable(const vector_of_moveable &other) = delete;
	vector_of_moveable &operator=(const vector_of_moveable &other) = delete;
	vector_of_moveable(vector_of_moveable &&other)
		: _size(base::take(other._size))
		, _capacity(base::take(other._capacity))
		, _plaindata(base::take(other._plaindata)) {
	}
	vector_of_moveable &operator=(vector_of_moveable &&other) {
		std_::swap_moveable(_size, other._size);
		std_::swap_moveable(_capacity, other._capacity);
		std_::swap_moveable(_plaindata, other._plaindata);
		return *this;
	}

	inline T *data() {
		return reinterpret_cast<T*>(_plaindata);
	}
	inline const T *data() const {
		return reinterpret_cast<const T*>(_plaindata);
	}

	inline bool operator==(const Self &other) const {
		if (this == &other) return true;
		if (_size != other._size) return false;
		for (int i = 0; i < _size; ++i) {
			if (data()[i] != other.data()[i]) {
				return false;
			}
		}
		return true;
	}
	inline bool operator!=(const Self &other) const { return !(*this == other); }
	inline int size() const { return _size; }
	inline bool isEmpty() const { return _size == 0; }
	inline void clear() {
		for (int i = 0; i < _size; ++i) {
			data()[i].~T();
		}
		_size = 0;

		operator delete[](_plaindata);
		_plaindata = nullptr;
		_capacity = 0;
	}

	typedef T *iterator;
	typedef const T *const_iterator;

	// STL style
	inline iterator begin() { return data(); }
	inline const_iterator begin() const { return data(); }
	inline const_iterator cbegin() const { return data(); }
	inline iterator end() { return data() + _size; }
	inline const_iterator end() const { return data() + _size; }
	inline const_iterator cend() const { return data() + _size; }
	inline iterator erase(iterator it) {
		T tmp = std_::move(*it);
		for (auto next = it + 1, e = end(); next != e; ++next) {
			auto prev = next - 1;
			*prev = std_::move(*next);
		}
		--_size;
		end()->~T();
		return it;
	}

	inline iterator insert(const_iterator pos, T &&value) {
		int insertAtIndex = pos - begin();
		if (_size + 1 > _capacity) {
			reallocate(_capacity + (_capacity > 1 ? _capacity / 2 : 1));
		}
		auto insertAt = begin() + insertAtIndex, e = end();
		if (insertAt == e) {
			new (&(*insertAt)) T(std_::move(value));
		} else {
			auto prev = e - 1;
			new (&(*e)) T(std_::move(*prev));
			for (auto it = prev; it != insertAt; --it) {
				*it = std_::move(*--prev);
			}
			*insertAt = std_::move(value);
		}
		++_size;
		return insertAt;
	}
	inline void push_back(T &&value) {
		insert(end(), std_::move(value));
	}
	inline void pop_back() {
		erase(end() - 1);
	}
	inline T &front() {
		return *begin();
	}
	inline const T &front() const {
		return *begin();
	}
	inline T &back() {
		return *(end() - 1);
	}
	inline const T &back() const {
		return *(end() - 1);
	}
	inline bool empty() const { return _size == 0; }

	inline T &operator[](int index) {
		return data()[index];
	}
	inline const T &operator[](int index) const {
		return data()[index];
	}
	inline const T &at(int index) const {
		if (index < 0 || index >= _size) {
#ifndef OS_MAC_OLD
			throw std::out_of_range("");
#else // OS_MAC_OLD
			throw std::exception();
#endif // OS_MAC_OLD
		}
		return data()[index];
	}

	void reserve(int newCapacity) {
		if (newCapacity > _capacity) {
			reallocate(newCapacity);
		}
	}

	inline ~vector_of_moveable() {
		clear();
	}

private:
	void reallocate(int newCapacity) {
		auto newPlainData = operator new[](newCapacity * sizeof(T));
		for (int i = 0; i < _size; ++i) {
			auto oldLocation = data() + i;
			auto newLocation = reinterpret_cast<T*>(newPlainData) + i;
			new (newLocation) T(std_::move(*oldLocation));
			oldLocation->~T();
		}
		std_::swap_moveable(_plaindata, newPlainData);
		_capacity = newCapacity;
		operator delete[](newPlainData);
	}

};

} // namespace std_

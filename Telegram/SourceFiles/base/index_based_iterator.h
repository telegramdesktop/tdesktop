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

#include "base/assertion.h"

namespace base {

template <typename Container>
class index_based_iterator {
public:
	using iterator_category = std::random_access_iterator_tag;

	using value_type = typename Container::value_type;
	using difference_type = typename Container::difference_type;
	using pointer = typename Container::pointer;
	using reference = typename Container::reference;

	index_based_iterator(
		Container *container,
		typename Container::iterator impl)
		: _container(container)
		, _index(impl - _container->begin()) {
	}

	reference operator*() const {
		return *(_container->begin() + _index);
	}
	pointer operator->() const {
		return std::addressof(**this);
	}
	index_based_iterator &operator++() {
		++_index;
		return *this;
	}
	index_based_iterator operator++(int) {
		auto copy = *this;
		++*this;
		return copy;
	}
	index_based_iterator &operator--() {
		--_index;
		return *this;
	}
	index_based_iterator operator--(int) {
		auto copy = *this;
		--*this;
		return copy;
	}
	index_based_iterator &operator+=(difference_type offset) {
		_index += offset;
		return *this;
	}
	index_based_iterator operator+(difference_type offset) const {
		auto copy = *this;
		copy += offset;
		return copy;
	}
	index_based_iterator &operator-=(difference_type offset) {
		_index -= offset;
		return *this;
	}
	index_based_iterator operator-(difference_type offset) const {
		auto copy = *this;
		copy -= offset;
		return copy;
	}
	difference_type operator-(const index_based_iterator &other) const {
		return _index - other._index;
	}
	reference operator[](difference_type offset) const {
		return *(*this + offset);
	}

	bool operator==(const index_based_iterator &other) const {
		Expects(_container == other._container);
		return _index == other._index;
	}
	bool operator!=(const index_based_iterator &other) const {
		Expects(_container == other._container);
		return _index != other._index;
	}
	bool operator<(const index_based_iterator &other) const {
		Expects(_container == other._container);
		return _index < other._index;
	}
	bool operator>(const index_based_iterator &other) const {
		return other < *this;
	}
	bool operator<=(const index_based_iterator &other) const {
		return !(other < *this);
	}
	bool operator>=(const index_based_iterator &other) const {
		return !(*this < other);
	}

	typename Container::iterator base() const {
		return _container->begin() + _index;
	}

private:
	Container *_container = nullptr;
	difference_type _index = 0;

};

template <typename Container>
index_based_iterator<Container> index_based_begin(Container &container) {
	return { &container, std::begin(container) };
}

template <typename Container>
index_based_iterator<Container> index_based_end(Container &container) {
	return { &container, std::end(container) };
}

} // namespace base

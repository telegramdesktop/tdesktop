/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QMap>

// ordered set template based on QMap
template <typename T>
class OrderedSet {
	struct NullType {
	};
	using Self = OrderedSet<T>;
	using Impl = QMap<T, NullType>;
	using IteratorImpl = typename Impl::iterator;
	using ConstIteratorImpl = typename Impl::const_iterator;
	Impl impl_;

public:
	OrderedSet() = default;
	OrderedSet(const OrderedSet &other) = default;
	OrderedSet(OrderedSet &&other) = default;
	OrderedSet &operator=(const OrderedSet &other) = default;
	OrderedSet &operator=(OrderedSet &&other) = default;
	~OrderedSet() = default;

	inline bool operator==(const Self &other) const { return impl_ == other.impl_; }
	inline bool operator!=(const Self &other) const { return impl_ != other.impl_; }
	inline int size() const { return impl_.size(); }
	inline bool isEmpty() const { return impl_.isEmpty(); }
	inline void detach() { return impl_.detach(); }
	inline bool isDetached() const { return impl_.isDetached(); }
	inline void clear() { return impl_.clear(); }
	inline QList<T> values() const { return impl_.keys(); }
	inline const T &first() const { return impl_.firstKey(); }
	inline const T &last() const { return impl_.lastKey(); }

	class const_iterator;
	class iterator {
	public:
		typedef typename IteratorImpl::iterator_category iterator_category;
		typedef typename IteratorImpl::difference_type difference_type;
		typedef T value_type;
		typedef T *pointer;
		typedef T &reference;

		iterator() = default;
		iterator(const iterator &other) = default;
		iterator &operator=(const iterator &other) = default;
		inline const T &operator*() const { return impl_.key(); }
		inline const T *operator->() const { return &impl_.key(); }
		inline bool operator==(const iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const iterator &other) const { return impl_ != other.impl_; }
		inline iterator &operator++() { ++impl_; return *this; }
		inline iterator operator++(int) { return iterator(impl_++); }
		inline iterator &operator--() { --impl_; return *this; }
		inline iterator operator--(int) { return iterator(impl_--); }
		inline iterator operator+(int j) const { return iterator(impl_ + j); }
		inline iterator operator-(int j) const { return iterator(impl_ - j); }
		inline iterator &operator+=(int j) { impl_ += j; return *this; }
		inline iterator &operator-=(int j) { impl_ -= j; return *this; }

		friend class const_iterator;
		inline bool operator==(const const_iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const const_iterator &other) const { return impl_ != other.impl_; }

	private:
		explicit iterator(const IteratorImpl &impl) : impl_(impl) {
		}
		IteratorImpl impl_;
		friend class OrderedSet<T>;

	};
	friend class iterator;

	class const_iterator {
	public:
		typedef typename IteratorImpl::iterator_category iterator_category;
		typedef typename IteratorImpl::difference_type difference_type;
		typedef T value_type;
		typedef T *pointer;
		typedef T &reference;

		const_iterator() = default;
		const_iterator(const const_iterator &other) = default;
		const_iterator &operator=(const const_iterator &other) = default;
		const_iterator(const iterator &other) : impl_(other.impl_) {
		}
		const_iterator &operator=(const iterator &other) {
			impl_ = other.impl_;
			return *this;
		}
		inline const T &operator*() const { return impl_.key(); }
		inline const T *operator->() const { return &impl_.key(); }
		inline bool operator==(const const_iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const const_iterator &other) const { return impl_ != other.impl_; }
		inline const_iterator &operator++() { ++impl_; return *this; }
		inline const_iterator operator++(int) { return const_iterator(impl_++); }
		inline const_iterator &operator--() { --impl_; return *this; }
		inline const_iterator operator--(int) { return const_iterator(impl_--); }
		inline const_iterator operator+(int j) const { return const_iterator(impl_ + j); }
		inline const_iterator operator-(int j) const { return const_iterator(impl_ - j); }
		inline const_iterator &operator+=(int j) { impl_ += j; return *this; }
		inline const_iterator &operator-=(int j) { impl_ -= j; return *this; }

		friend class iterator;
		inline bool operator==(const iterator &other) const { return impl_ == other.impl_; }
		inline bool operator!=(const iterator &other) const { return impl_ != other.impl_; }

	private:
		explicit const_iterator(const ConstIteratorImpl &impl) : impl_(impl) {
		}
		ConstIteratorImpl impl_;
		friend class OrderedSet<T>;

	};
	friend class const_iterator;

	// STL style
	inline iterator begin() { return iterator(impl_.begin()); }
	inline const_iterator begin() const { return const_iterator(impl_.cbegin()); }
	inline const_iterator constBegin() const { return const_iterator(impl_.cbegin()); }
	inline const_iterator cbegin() const { return const_iterator(impl_.cbegin()); }
	inline iterator end() { detach(); return iterator(impl_.end()); }
	inline const_iterator end() const { return const_iterator(impl_.cend()); }
	inline const_iterator constEnd() const { return const_iterator(impl_.cend()); }
	inline const_iterator cend() const { return const_iterator(impl_.cend()); }
	inline iterator erase(iterator it) { return iterator(impl_.erase(it.impl_)); }

	inline iterator insert(const T &value) { return iterator(impl_.insert(value, NullType())); }
	inline iterator insert(const_iterator pos, const T &value) { return iterator(impl_.insert(pos.impl_, value, NullType())); }
	inline int remove(const T &value) { return impl_.remove(value); }
	inline bool contains(const T &value) const { return impl_.contains(value); }

	// more Qt
	typedef iterator Iterator;
	typedef const_iterator ConstIterator;
	inline int count() const { return impl_.count(); }
	inline iterator find(const T &value) { return iterator(impl_.find(value)); }
	inline const_iterator find(const T &value) const { return const_iterator(impl_.constFind(value)); }
	inline const_iterator constFind(const T &value) const { return const_iterator(impl_.constFind(value)); }
	inline Self &unite(const Self &other) { impl_.unite(other.impl_); return *this; }

	// STL compatibility
	typedef typename Impl::difference_type difference_type;
	typedef typename Impl::size_type size_type;
	inline bool empty() const { return impl_.empty(); }

};

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/segment_tree.h"

namespace Statistic {
namespace {

constexpr auto kMinArraySize = size_t(30);

} // namespace

SegmentTree::SegmentTree(std::vector<int> array)
: _array(std::move(array)) {
	if (_array.size() < kMinArraySize) {
		return;
	}

	// The max size of this array is about 2 * 2 ^ log2(n) + 1.
	const auto size = 2 * std::pow(
		2.,
		std::floor((std::log(_array.size()) / std::log(2.)) + 1));
	_heap.resize(int(size));
	build(1, 0, _array.size());
}

void SegmentTree::build(int v, int from, int size) {
	_heap[v].from = from;
	_heap[v].to = (from + size - 1);

	if (size == 1) {
		_heap[v].sum = _array[from];
		_heap[v].max = _array[from];
		_heap[v].min = _array[from];
	} else {
		// Build children.
		build(2 * v, from, size / 2);
		build(2 * v + 1, from + size / 2, size - size / 2);

		_heap[v].sum = _heap[2 * v].sum + _heap[2 * v + 1].sum;
		// max = max of the children.
		_heap[v].max = std::max(_heap[2 * v].max, _heap[2 * v + 1].max);
		_heap[v].min = std::min(_heap[2 * v].min, _heap[2 * v + 1].min);
	}
}

int SegmentTree::rMaxQ(int from, int to) {
	if (_array.size() < kMinArraySize) {
		auto max = std::numeric_limits<int>::min();
		from = std::max(from, 0);
		to = std::min(to, int(_array.size() - 1));
		for (auto i = from; i <= to; i++) {
			max = std::max(max, _array[i]);
		}
		return max;
	}
	return rMaxQ(1, from, to);
}

int SegmentTree::rMaxQ(int v, int from, int to) {
	const auto &n = _heap[v];
	// If you did a range update that contained this node,
	// you can infer the Min value without going down the tree.
	if (n.pendingVal && contains(n.from, n.to, from, to)) {
		return n.pendingVal.value;
	}

	if (contains(from, to, n.from, n.to)) {
		return _heap[v].max;
	}

	if (intersects(from, to, n.from, n.to)) {
		propagate(v);
		const auto leftMin = rMaxQ(2 * v, from, to);
		const auto rightMin = rMaxQ(2 * v + 1, from, to);

		return std::max(leftMin, rightMin);
	}

	return 0;
}

int SegmentTree::rMinQ(int from, int to) {
	if (_array.size() < kMinArraySize) {
		auto min = std::numeric_limits<int>::max();
		from = std::max(from, 0);
		to = std::min(to, int(_array.size() - 1));
		for (auto i = from; i <= to; i++) {
			min = std::min(min, _array[i]);
		}
		return min;
	}
	return rMinQ(1, from, to);
}

int SegmentTree::rMinQ(int v, int from, int to) {
	const auto &n = _heap[v];
	// If you did a range update that contained this node,
	// you can infer the Min value without going down the tree.
	if (n.pendingVal && contains(n.from, n.to, from, to)) {
		return n.pendingVal.value;
	}

	if (contains(from, to, n.from, n.to)) {
		return _heap[v].min;
	}

	if (intersects(from, to, n.from, n.to)) {
		propagate(v);
		const auto leftMin = rMinQ(2 * v, from, to);
		const auto rightMin = rMinQ(2 * v + 1, from, to);

		return std::min(leftMin, rightMin);
	}

	return std::numeric_limits<int>::max();
}

void SegmentTree::propagate(int v) {
	auto &n = _heap[v];

	if (n.pendingVal) {
		const auto value = n.pendingVal.value;
		n.pendingVal = {};
		change(_heap[2 * v], value);
		change(_heap[2 * v + 1], value);
	}
}

void SegmentTree::change(SegmentTree::Node &n, int value) {
	n.pendingVal = { value, true };
	n.sum = n.size() * value;
	n.max = value;
	n.min = value;
	_array[n.from] = value;
}

bool SegmentTree::contains(int from1, int to1, int from2, int to2) const {
	return (from2 >= from1) && (to2 <= to1);
}

bool SegmentTree::intersects(int from1, int to1, int from2, int to2) const {
	return ((from1 <= from2) && (to1 >= from2)) // (.[..)..] or (.[...]..)
		|| ((from1 >= from2) && (from1 <= to2)); // [.(..]..) or [..(..)..
}

} // namespace Statistic

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "statistics/statistics_types.h"

namespace Statistic {

class SegmentTree final {
public:
	SegmentTree() = default;
	SegmentTree(std::vector<ChartValue> array);

	[[nodiscard]] bool empty() const {
		return _array.empty();
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

	[[nodiscard]] ChartValue rMaxQ(int from, int to);
	[[nodiscard]] ChartValue rMinQ(int from, int to);

private:
	struct Node final {
		ChartValue sum = 0;
		ChartValue max = 0;
		ChartValue min = 0;

		struct PendingVal {
			[[nodiscard]] explicit operator bool() const {
				return available;
			}
			ChartValue value = 0;
			bool available = false;
		};
		PendingVal pendingVal;

		int from = 0;
		int to = 0;

		[[nodiscard]] int size() {
			return to - from + 1;
		}
	};

	void build(ChartValue v, int from, int size);
	void propagate(ChartValue v);
	void change(Node &n, ChartValue value);

	[[nodiscard]] ChartValue rMaxQ(ChartValue v, int from, int to);
	[[nodiscard]] ChartValue rMinQ(ChartValue v, int from, int to);

	[[nodiscard]] bool contains(int from1, int to1, int from2, int to2) const;
	[[nodiscard]] bool intersects(
		int from1,
		int to1,
		int from2,
		int to2) const;

	std::vector<ChartValue> _array;
	std::vector<Node> _heap;

};

} // namespace Statistic

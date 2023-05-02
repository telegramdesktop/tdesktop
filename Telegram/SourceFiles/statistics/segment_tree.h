/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Statistic {

class SegmentTree final {
public:
	SegmentTree() = default;
	SegmentTree(std::vector<int> array);

	[[nodiscard]] bool empty() const {
		return _array.empty();
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}

	[[nodiscard]] int rMaxQ(int from, int to);
	[[nodiscard]] int rMinQ(int from, int to);

private:
	struct Node final {
		int sum = 0;
		int max = 0;
		int min = 0;

		struct PendingVal {
			[[nodiscard]] explicit operator bool() const {
				return available;
			}
			int value = 0;
			bool available = false;
		};
		PendingVal pendingVal;

		int from = 0;
		int to = 0;

		[[nodiscard]] int size() {
			return to - from + 1;
		}
	};

	void build(int v, int from, int size);
	void propagate(int v);
	void change(Node &n, int value);

	[[nodiscard]] int rMaxQ(int v, int from, int to);
	[[nodiscard]] int rMinQ(int v, int from, int to);

	[[nodiscard]] bool contains(int from1, int to1, int from2, int to2) const;
	[[nodiscard]] bool intersects(
		int from1,
		int to1,
		int from2,
		int to2) const;

	std::vector<int> _array;
	std::vector<Node> _heap;

};

} // namespace Statistic

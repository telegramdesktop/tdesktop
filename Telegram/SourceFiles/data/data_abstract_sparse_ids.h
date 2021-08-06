/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename IdsContainer>
class AbstractSparseIds {
public:
	using Id = typename IdsContainer::value_type;

	AbstractSparseIds() = default;
	AbstractSparseIds(
		const IdsContainer &ids,
		std::optional<int> fullCount,
		std::optional<int> skippedBefore,
		std::optional<int> skippedAfter)
	: _ids(ids)
	, _fullCount(fullCount)
	, _skippedBefore(skippedBefore)
	, _skippedAfter(skippedAfter) {
	}

	std::optional<int> fullCount() const {
		return _fullCount;
	}
	std::optional<int> skippedBefore() const {
		return _skippedBefore;
	}
	std::optional<int> skippedAfter() const {
		return _skippedAfter;
	}
	std::optional<int> indexOf(Id id) const {
		const auto it = ranges::find(_ids, id);
		if (it != _ids.end()) {
			return (it - _ids.begin());
		}
		return std::nullopt;
	}
	int size() const {
		return _ids.size();
	}
	Id operator[](int index) const {
		Expects(index >= 0 && index < size());

		return *(_ids.begin() + index);
	}
	std::optional<int> distance(Id a, Id b) const {
		if (const auto i = indexOf(a)) {
			if (const auto j = indexOf(b)) {
				return *j - *i;
			}
		}
		return std::nullopt;
	}
	std::optional<Id> nearest(Id id) const {
		if (const auto it = ranges::lower_bound(_ids, id); it != _ids.end()) {
			return *it;
		} else if (_ids.empty()) {
			return std::nullopt;
		}
		return _ids.back();
	}
	void reverse() {
		ranges::reverse(_ids);
		std::swap(_skippedBefore, _skippedAfter);
	}

private:
	IdsContainer _ids;
	std::optional<int> _fullCount;
	std::optional<int> _skippedBefore;
	std::optional<int> _skippedAfter;

};

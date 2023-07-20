/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <statistics/statistics_common.h>

namespace Statistic {

class ChartLineViewContext final {
public:
	ChartLineViewContext() = default;

	struct CacheToken final {
		explicit CacheToken() = default;
		explicit CacheToken(
			Limits xIndices,
			Limits xPercentageLimits,
			Limits heightLimits,
			QSize rectSize)
		: xIndices(std::move(xIndices))
		, xPercentageLimits(std::move(xPercentageLimits))
		, heightLimits(std::move(heightLimits))
		, rectSize(std::move(rectSize)) {
		}

		bool operator==(const CacheToken &other) const {
			return (rectSize == other.rectSize)
				&& (xIndices.min == other.xIndices.min)
				&& (xIndices.max == other.xIndices.max)
				&& (xPercentageLimits.min == other.xPercentageLimits.min)
				&& (xPercentageLimits.max == other.xPercentageLimits.max)
				&& (heightLimits.min == other.heightLimits.min)
				&& (heightLimits.max == other.heightLimits.max);
		}

		bool operator!=(const CacheToken &other) const {
			return !(*this == other);
		}

		Limits xIndices;
		Limits xPercentageLimits;
		Limits heightLimits;
		QSize rectSize;
	};

	struct Cache final {
		QImage image;
		CacheToken lastToken;
		bool hq = false;
	};

	void setEnabled(int id, bool enabled, crl::time now);
	[[nodiscard]] bool isEnabled(int id) const;
	[[nodiscard]] bool isFinished() const;
	[[nodiscard]] float64 alpha(int id) const;

	void setCacheFooter(bool value) {
		_isFooter = value;
	}
	void setCacheImage(int id, QImage &&image);
	void setCacheLastToken(int id, CacheToken token);
	void setCacheHQ(int id, bool value);
	[[nodiscard]] const Cache &cache(int id);

	void tick(crl::time now);

	float64 factor = 1.;

private:
	struct Entry final {
		bool enabled = false;
		crl::time startedAt = 0;
		float64 alpha = 1.;
	};

	base::flat_map<int, Entry> _entries;
	base::flat_map<int, Cache> _caches;
	base::flat_map<int, Cache> _cachesFooter;
	bool _isFinished = true;

	bool _isFooter = false;

};

} // namespace Statistic

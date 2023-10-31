/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_statistics_chart.h"

#include <QtCore/QDateTime>
#include <QtCore/QLocale>

namespace Data {

void StatisticalChart::measure() {
	if (x.empty()) {
		return;
	}
	const auto n = x.size();
	const auto start = x.front();
	const auto end = x.back();

	xPercentage.clear();
	xPercentage.resize(n);
	if (n == 1) {
		xPercentage[0] = 1;
	} else {
		for (auto i = 0; i < n; i++) {
			xPercentage[i] = (x[i] - start) / float64(end - start);
		}
	}

	for (auto &line : lines) {
		if (line.maxValue > maxValue) {
			maxValue = line.maxValue;
		}
		if (line.minValue < minValue) {
			minValue = line.minValue;
		}
		line.segmentTree = Statistic::SegmentTree(line.y);
	}

	daysLookup.clear();
	const auto dateCount = int((end - start) / timeStep) + 10;
	daysLookup.reserve(dateCount);
	constexpr auto kOneDay = 3600 * 24 * 1000;
	const auto formatter = u"d MMM"_q;
	for (auto i = 0; i < dateCount; i++) {
		const auto r = (start + (i * timeStep)) / 1000;
		const auto dateTime = QDateTime::fromSecsSinceEpoch(r);
		if (timeStep == 1) {
			daysLookup.push_back(
				QString(((i < 10) ? u"0%1:00"_q : u"%1:00"_q).arg(i)));
		} else if (timeStep < kOneDay) {
			daysLookup.push_back(u"%1:%2"_q
				.arg(dateTime.time().hour(), 2, 10, QChar('0'))
				.arg(dateTime.time().minute(), 2, 10, QChar('0')));
		} else {
			const auto date = dateTime.date();
			daysLookup.push_back(QLocale().toString(date, formatter));
		}
	}

	oneDayPercentage = timeStep / float64(end - start);
}

QString StatisticalChart::getDayString(int i) const {
	return daysLookup[int((x[i] - x[0]) / timeStep)];
}

int StatisticalChart::findStartIndex(float64 v) const {
	if (!v) {
		return 0;
	}
	const auto n = int(xPercentage.size());

	if (n < 2) {
		return 0;
	}
	auto left = 0;
	auto right = n - 1;

	while (left <= right) {
		const auto middle = (right + left) >> 1;
		if (v < xPercentage[middle]
			&& (!middle || (v > xPercentage[middle - 1]))) {
			return middle;
		}
		if (v == xPercentage[middle]) {
			return middle;
		}
		if (v < xPercentage[middle]) {
			right = middle - 1;
		} else if (v > xPercentage[middle]) {
			left = middle + 1;
		}
	}
	return left;
}

int StatisticalChart::findEndIndex(int left, float64 v) const {
	const auto n = int(xPercentage.size());
	if (v == 1.) {
		return n - 1;
	}
	auto right = n - 1;

	while (left <= right) {
		const auto middle = (right + left) >> 1;
		if (v > xPercentage[middle]
			&& ((middle == n - 1) || (v < xPercentage[middle + 1]))) {
			return middle;
		}
		if (v == xPercentage[middle]) {
			return middle;
		}
		if (v < xPercentage[middle]) {
			right = middle - 1;
		} else if (v > xPercentage[middle]) {
			left = middle + 1;
		}
	}
	return right;
}


int StatisticalChart::findIndex(int left, int right, float64 v) const {
	const auto n = int(xPercentage.size());

	if (v <= xPercentage[left]) {
		return left;
	}
	if (v >= xPercentage[right]) {
		return right;
	}

	while (left <= right) {
		const auto middle = (right + left) >> 1;
		if (v > xPercentage[middle]
			&& ((middle == n - 1) || (v < xPercentage[middle + 1]))) {
			return middle;
		}

		if (v == xPercentage[middle]) {
			return middle;
		}
		if (v < xPercentage[middle]) {
			right = middle - 1;
		} else if (v > xPercentage[middle]) {
			left = middle + 1;
		}
	}
	return right;
}

} // namespace Data

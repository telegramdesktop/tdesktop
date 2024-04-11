/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/chart_rulers_data.h"

#include "lang/lang_tag.h"

namespace Statistic {
namespace {

constexpr auto kMinLines = ChartValue(2);
constexpr auto kMaxLines = ChartValue(6);
constexpr auto kStep = 5.;

[[nodiscard]] ChartValue Round(ChartValue maxValue) {
	const auto k = ChartValue(maxValue / kStep);
	return (k % 10 == 0) ? maxValue : ((maxValue / 10 + 1) * 10);
}

[[nodiscard]] QString Format(ChartValue absoluteValue) {
	constexpr auto kTooMuch = ChartValue(10'000);
	return (absoluteValue >= kTooMuch)
		? Lang::FormatCountToShort(absoluteValue).string
		: QString::number(absoluteValue);
}

} // namespace

ChartRulersData::ChartRulersData(
		ChartValue newMaxHeight,
		ChartValue newMinHeight,
		bool useMinHeight,
		float64 rightRatio,
		Fn<QString(float64)> leftCustomCaption,
		Fn<QString(float64)> rightCustomCaption) {
	if (!useMinHeight) {
		const auto v = (newMaxHeight > 100)
			? Round(newMaxHeight)
			: newMaxHeight;

		const auto step = std::max(
			ChartValue(1),
			ChartValue(std::ceil(v / kStep)));

		auto n = kMaxLines;
		if (v < kMaxLines) {
			n = std::max(2, int(v + 1));
		} else if (v / 2 < kMaxLines) {
			n = v / 2 + 1;
			if (v % 2 != 0) {
				n++;
			}
		}

		lines.resize(n);

		for (auto i = 1; i < n; i++) {
			auto &line = lines[i];
			line.absoluteValue = i * step;
			line.caption = Lang::FormatCountToShort(
				line.absoluteValue).string;
		}
	} else {
		auto n = int(0);
		const auto diff = newMaxHeight - newMinHeight;
		auto step = 0.;
		if (diff == 0) {
			newMinHeight--;
			n = kMaxLines / 2;
			step = 1.;
		} else if (diff < kMaxLines) {
			n = std::max(kMinLines, diff + 1);
			step = 1.;
		} else if (diff / 2 < kMaxLines) {
			n = diff / 2 + diff % 2 + 1;
			step = 2.;
		} else {
			step = (newMaxHeight - newMinHeight) / kStep;
			if (step <= 0) {
				step = 1;
				n = std::max(kMinLines, newMaxHeight - newMinHeight + 1);
			} else {
				n = 6;
			}
		}

		lines.resize(n);
		const auto diffAbsoluteValue = ChartValue((n - 1) * step);
		const auto skipFloatValues = (step / rightRatio) < 1;
		for (auto i = 0; i < n; i++) {
			auto &line = lines[i];
			const auto value = ChartValue(i * step);
			line.absoluteValue = newMinHeight + value;
			line.relativeValue = 1. - value / float64(diffAbsoluteValue);
			line.caption = leftCustomCaption
				? leftCustomCaption(line.absoluteValue)
				: Format(line.absoluteValue);
			if (rightRatio > 0 || rightCustomCaption) {
				const auto v = (newMinHeight + i * step) / rightRatio;
				line.scaledLineCaption = rightCustomCaption
					? rightCustomCaption(line.absoluteValue)
					: (!skipFloatValues)
					? Format(v)
					: ((v - ChartValue(v)) < 0.01)
					? Format(v)
					: QString();
			}
		}
	}
}

void ChartRulersData::computeRelative(
		ChartValue newMaxHeight,
		ChartValue newMinHeight) {
	for (auto &line : lines) {
		line.relativeValue = 1.
			- ((line.absoluteValue - newMinHeight)
				/ (newMaxHeight - newMinHeight));
	}
}

ChartValue ChartRulersData::LookupHeight(ChartValue maxValue) {
	const auto v = (maxValue > 100) ? Round(maxValue) : maxValue;

	const auto step = ChartValue(std::ceil(v / kStep));
	return step * kStep;
}

} // namespace Statistic

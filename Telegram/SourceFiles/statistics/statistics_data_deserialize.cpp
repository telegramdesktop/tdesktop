/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_data_deserialize.h"

#include "base/debug_log.h"
#include "data/data_statistics_chart.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace Statistic {

Data::StatisticalChart StatisticalChartFromJSON(const QByteArray &json) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("API Error: Bad stats graph json received."));
		return {};
	}
	const auto root = document.object();
	const auto columns = root.value(u"columns"_q).toArray();
	if (columns.empty()) {
		LOG(("API Error: Empty columns list from stats graph received."));
		return {};
	}

	const auto hiddenLinesRaw = root.value(u"hidden"_q).toArray();
	const auto hiddenLines = ranges::views::all(
		hiddenLinesRaw
	) | ranges::views::transform([](const auto &q) {
		return q.toString();
	}) | ranges::to_vector;

	auto result = Data::StatisticalChart();
	auto columnIdCount = 0;
	for (const auto &column : columns) {
		const auto array = column.toArray();
		if (array.empty()) {
			LOG(("API Error: Empty column from stats graph received."));
			return {};
		}
		const auto columnId = array.first().toString();
		if (columnId == u"x"_q) {
			const auto length = array.size() - 1;
			result.x.resize(length);
			for (auto i = 0; i < length; i++) {
				result.x[i] = array.at(i + 1).toDouble();
			}
		} else {
			auto line = Data::StatisticalChart::Line();
			const auto length = array.size() - 1;
			line.id = (++columnIdCount);
			line.idString = columnId;
			line.isHiddenOnStart = ranges::contains(hiddenLines, columnId);
			line.y.resize(length);
			for (auto i = 0; i < length; i++) {
				const auto value = array.at(i + 1).toInt();
				line.y[i] = value;
				if (value > line.maxValue) {
					line.maxValue = value;
				}
				if (value < line.minValue) {
					line.minValue = value;
				}
			}
			result.lines.push_back(std::move(line));
		}
		if (result.x.size() > 1) {
			result.timeStep = result.x[1] - result.x[0];
		} else {
			constexpr auto kOneDay = 3600 * 24 * 1000;
			result.timeStep = kOneDay;
		}
		result.measure();
	}
	if (result.maxValue == result.minValue) {
		if (result.minValue) {
			result.minValue = 0;
		} else {
			result.maxValue = 1;
		}
	}

	{
		const auto subchart = root.value(u"subchart"_q).toObject();
		const auto subchartShowIt = subchart.constFind(u"show"_q);
		if (subchartShowIt != subchart.constEnd()) {
			if (subchartShowIt->isBool()) {
				result.isFooterHidden = !(subchartShowIt->toBool());
			}
		}
		const auto defaultZoomIt = subchart.constFind(u"defaultZoom"_q);
		auto min = int(0);
		auto max = int(result.x.size() - 1);
		if (defaultZoomIt != subchart.constEnd()) {
			if (const auto array = defaultZoomIt->toArray(); !array.empty()) {
				const auto minValue = array.first().toDouble();
				const auto maxValue = array.last().toDouble();
				for (auto i = 0; i < result.x.size(); i++) {
					if (result.x[i] == minValue) {
						min = i;
					}
					if (result.x[i] == maxValue) {
						max = i;
					}
				}
			}
		}
		result.defaultZoomXIndex.min = std::min(min, max);
		result.defaultZoomXIndex.max = std::max(min, max);
	}
	{

		const auto percentageShowIt = root.constFind(u"percentage"_q);
		if (percentageShowIt != root.constEnd()) {
			if (percentageShowIt->isBool()) {
				result.hasPercentages = (percentageShowIt->toBool());
			}
		}
	}
	{
		const auto tooltipFormatIt = root.constFind(u"xTooltipFormatter"_q);
		if (tooltipFormatIt != root.constEnd()) {
			const auto tooltipFormat = tooltipFormatIt->toString();
			result.weekFormat = tooltipFormat.contains(u"'week'"_q);
		}
	}

	const auto colors = root.value(u"colors"_q).toObject();
	const auto names = root.value(u"names"_q).toObject();

	const auto colorPattern = u"(.*)(#.*)"_q;
	for (auto &line : result.lines) {
		const auto colorIt = colors.constFind(line.idString);
		if (colorIt != colors.constEnd() && (*colorIt).isString()) {
			const auto match = QRegularExpression(colorPattern).match(
				colorIt->toString());
			if (match.hasMatch()) {
				line.colorKey = match.captured(1);
				line.color = QColor(match.captured(2));
			}
		}
		const auto nameIt = names.constFind(line.idString);
		if (nameIt != names.constEnd() && (*nameIt).isString()) {
			line.name = nameIt->toString().replace('-', QChar(8212));
		}
	}

	return result;
}

} // namespace Statistic

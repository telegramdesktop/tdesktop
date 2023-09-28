/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_data_deserialize.h"

#include "data/data_statistics.h"

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

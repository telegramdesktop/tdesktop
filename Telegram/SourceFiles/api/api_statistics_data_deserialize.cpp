/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_statistics_data_deserialize.h"

#include "data/data_statistics_chart.h"
#include "statistics/statistics_data_deserialize.h"

namespace Api {

Data::StatisticalGraph StatisticalGraphFromTL(const MTPStatsGraph &tl) {
	return tl.match([&](const MTPDstatsGraph &d) {
		using namespace Statistic;
		const auto zoomToken = d.vzoom_token().has_value()
			? qs(*d.vzoom_token()).toUtf8()
			: QByteArray();
		return Data::StatisticalGraph{
			StatisticalChartFromJSON(qs(d.vjson().data().vdata()).toUtf8()),
			zoomToken,
		};
	}, [&](const MTPDstatsGraphAsync &data) {
		return Data::StatisticalGraph{
			.zoomToken = qs(data.vtoken()).toUtf8(),
		};
	}, [&](const MTPDstatsGraphError &data) {
		return Data::StatisticalGraph{ .error = qs(data.verror()) };
	});
}


} // namespace Api

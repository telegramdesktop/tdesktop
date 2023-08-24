/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_box.h"

#include "api/api_statistics.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "statistics/chart_widget.h"
#include "ui/toast/toast.h"
#include "ui/layers/generic_box.h"

namespace {
} // namespace

void StatisticsBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	const auto chartWidget = box->addRow(
		object_ptr<Statistic::ChartWidget>(box));
	const auto api = chartWidget->lifetime().make_state<Api::Statistics>(
		&peer->session().api());

	const auto processZoom = [=](
			not_null<Statistic::ChartWidget*> widget,
			const QString &zoomToken) {
		if (!zoomToken.isEmpty()) {
			widget->zoomRequests(
			) | rpl::start_with_next([=](float64 x) {
				api->requestZoom(
					peer,
					zoomToken,
					x
				) | rpl::start_with_next_error_done([=](
						const Data::StatisticalGraph &graph) {
					if (graph.chart) {
						widget->setZoomedChartData(graph.chart);
					} else if (!graph.error.isEmpty()) {
						Ui::Toast::Show(
							box->uiShow()->toastParent(),
							graph.error);
					}
				}, [=](const QString &error) {
				}, [=] {
				}, widget->lifetime());
			}, widget->lifetime());
		}
	};

	api->request(
		peer
	) | rpl::start_with_done([=] {
		if (const auto stats = api->channelStats()) {
			chartWidget->setChartData(stats.memberCountGraph.chart);
		}
	}, chartWidget->lifetime());
	box->setTitle(tr::lng_stats_title());
}

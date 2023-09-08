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
#include "statistics/statistics_common.h"
#include "ui/toast/toast.h"
#include "ui/layers/generic_box.h"

namespace {
} // namespace

void StatisticsBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	const auto chartWidget = box->addRow(
		object_ptr<Statistic::ChartWidget>(box));
	const auto chartWidget2 = box->addRow(
		object_ptr<Statistic::ChartWidget>(box));
	const auto chartWidget3 = box->addRow(
		object_ptr<Statistic::ChartWidget>(box));
	const auto chartWidget4 = box->addRow(
		object_ptr<Statistic::ChartWidget>(box));
	const auto chartWidget5 = box->addRow(
		object_ptr<Statistic::ChartWidget>(box));
	const auto api = chartWidget->lifetime().make_state<Api::Statistics>(
		&peer->session().api());

	const auto processZoom = [=](
			not_null<Statistic::ChartWidget*> widget,
			const QString &zoomToken,
			Statistic::ChartViewType type) {
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
						widget->setZoomedChartData(graph.chart, x, type);
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

	const auto processChart = [=](
			not_null<Statistic::ChartWidget*> widget,
			const Data::StatisticalGraph &graphData,
			rpl::producer<QString> &&title,
			Statistic::ChartViewType type) {
		if (graphData.chart) {
			widget->setChartData(graphData.chart, type);
			processZoom(widget, graphData.zoomToken, type);
			widget->setTitle(std::move(title));
		} else if (!graphData.zoomToken.isEmpty()) {
			api->requestZoom(
				peer,
				graphData.zoomToken,
				0
			) | rpl::start_with_next_error_done([=](
					const Data::StatisticalGraph &graph) {
				if (graph.chart) {
					widget->setChartData(graph.chart, type);
					processZoom(widget, graph.zoomToken, type);
					widget->setTitle(rpl::duplicate(title));
				} else if (!graph.error.isEmpty()) {
					Ui::Toast::Show(
						box->uiShow()->toastParent(),
						graph.error);
				}
			}, [=](const QString &error) {
			}, [=] {
			}, widget->lifetime());
		}
	};

	api->request(
		peer
	) | rpl::start_with_done([=] {
		const auto stats = api->channelStats();
		if (!stats) {
			return;
		}
		using Type = Statistic::ChartViewType;
		processChart(
			chartWidget,
			stats.memberCountGraph,
			tr::lng_chart_title_member_count(),
			Type::Linear);
		processChart(
			chartWidget2,
			stats.joinGraph,
			tr::lng_chart_title_join(),
			Type::Linear);
		processChart(
			chartWidget3,
			stats.muteGraph,
			tr::lng_chart_title_mute(),
			Type::Linear);
		processChart(
			chartWidget4,
			stats.viewCountBySourceGraph,
			tr::lng_chart_title_view_count_by_source(),
			Type::Stack);
		processChart(
			chartWidget5,
			stats.joinBySourceGraph,
			tr::lng_chart_title_join_by_source(),
			Type::Stack);
	}, chartWidget->lifetime());
	box->setTitle(tr::lng_stats_title());
}

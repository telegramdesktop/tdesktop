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
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "statistics/chart_header_widget.h"
#include "statistics/chart_widget.h"
#include "statistics/statistics_common.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/toast/toast.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

namespace {

struct Descriptor final {
	not_null<PeerData*> peer;
	not_null<Api::Statistics*> api;
	not_null<QWidget*> toastParent;
};

struct AnyStats final {
	Data::ChannelStatistics channel;
	Data::SupergroupStatistics supergroup;
};

void ProcessZoom(
		const Descriptor &d,
		not_null<Statistic::ChartWidget*> widget,
		const QString &zoomToken,
		Statistic::ChartViewType type) {
	if (zoomToken.isEmpty()) {
		return;
	}
	widget->zoomRequests(
	) | rpl::start_with_next([=](float64 x) {
		d.api->requestZoom(
			d.peer,
			zoomToken,
			x
		) | rpl::start_with_next_error_done([=](
				const Data::StatisticalGraph &graph) {
			if (graph.chart) {
				widget->setZoomedChartData(graph.chart, x, type);
			} else if (!graph.error.isEmpty()) {
				Ui::Toast::Show(d.toastParent, graph.error);
			}
		}, [=](const QString &error) {
		}, [=] {
		}, widget->lifetime());
	}, widget->lifetime());
}

void ProcessChart(
		const Descriptor &d,
		not_null<Ui::SlideWrap<Ui::VerticalLayout>*> wrap,
		not_null<Statistic::ChartWidget*> widget,
		const Data::StatisticalGraph &graphData,
		rpl::producer<QString> &&title,
		Statistic::ChartViewType type) {
	wrap->toggle(false, anim::type::instant);
	if (graphData.chart) {
		widget->setChartData(graphData.chart, type);
		wrap->toggle(true, anim::type::instant);
		ProcessZoom(d, widget, graphData.zoomToken, type);
		widget->setTitle(std::move(title));
	} else if (!graphData.zoomToken.isEmpty()) {
		d.api->requestZoom(
			d.peer,
			graphData.zoomToken,
			0
		) | rpl::start_with_next_error_done([=](
				const Data::StatisticalGraph &graph) {
			if (graph.chart) {
				widget->setChartData(graph.chart, type);
				wrap->toggle(true, anim::type::normal);
				ProcessZoom(d, widget, graph.zoomToken, type);
				widget->setTitle(rpl::duplicate(title));
			} else if (!graph.error.isEmpty()) {
				Ui::Toast::Show(d.toastParent, graph.error);
			}
		}, [=](const QString &error) {
		}, [=] {
		}, widget->lifetime());
	}
}

void FillStatistic(
		not_null<Ui::GenericBox*> box,
		const Descriptor &descriptor,
		const AnyStats &stats) {
	using Type = Statistic::ChartViewType;
	const auto &padding = st::statisticsChartEntryPadding;
	const auto &m = st::boxRowPadding;
	const auto addSkip = [&](not_null<Ui::VerticalLayout*> c) {
		Settings::AddSkip(c, padding.bottom());
		Settings::AddDivider(c);
		Settings::AddSkip(c, padding.top());
	};
	const auto addChart = [&](
			const Data::StatisticalGraph &graphData,
			rpl::producer<QString> &&title,
			Statistic::ChartViewType type) {
		const auto wrap = box->addRow(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				box,
				object_ptr<Ui::VerticalLayout>(box)),
			{});
		ProcessChart(
			descriptor,
			wrap,
			wrap->entity()->add(object_ptr<Statistic::ChartWidget>(box), m),
			graphData,
			std::move(title),
			type);
		addSkip(wrap->entity());
	};
	addSkip(box->verticalLayout());
	if (const auto s = stats.channel) {
		addChart(
			s.memberCountGraph,
			tr::lng_chart_title_member_count(),
			Type::Linear);
		addChart(
			s.joinGraph,
			tr::lng_chart_title_join(),
			Type::Linear);
		addChart(
			s.muteGraph,
			tr::lng_chart_title_mute(),
			Type::Linear);
		addChart(
			s.viewCountByHourGraph,
			tr::lng_chart_title_view_count_by_hour(),
			Type::Linear);
		addChart(
			s.viewCountBySourceGraph,
			tr::lng_chart_title_view_count_by_source(),
			Type::Stack);
		addChart(
			s.joinBySourceGraph,
			tr::lng_chart_title_join_by_source(),
			Type::Stack);
		addChart(
			s.languageGraph,
			tr::lng_chart_title_language(),
			Type::StackLinear);
		addChart(
			s.messageInteractionGraph,
			tr::lng_chart_title_message_interaction(),
			Type::DoubleLinear);
		addChart(
			s.instantViewInteractionGraph,
			tr::lng_chart_title_instant_view_interaction(),
			Type::DoubleLinear);
	} else if (const auto s = stats.supergroup) {
		addChart(
			s.memberCountGraph,
			tr::lng_chart_title_member_count(),
			Type::Linear);
		addChart(
			s.joinGraph,
			tr::lng_chart_title_group_join(),
			Type::Linear);
		addChart(
			s.joinBySourceGraph,
			tr::lng_chart_title_group_join_by_source(),
			Type::Stack);
		addChart(
			s.languageGraph,
			tr::lng_chart_title_group_language(),
			Type::StackLinear);
		addChart(
			s.messageContentGraph,
			tr::lng_chart_title_group_message_content(),
			Type::Stack);
		addChart(
			s.actionGraph,
			tr::lng_chart_title_group_action(),
			Type::DoubleLinear);
		addChart(
			s.dayGraph,
			tr::lng_chart_title_group_day(),
			Type::Linear);
		// addChart(
		// 	s.weekGraph,
		// 	tr::lng_chart_title_group_week(),
		// 	Type::StackLinear);
	}
}

void FillLoading(
		not_null<Ui::GenericBox*> box,
		rpl::producer<bool> toggleOn) {
	const auto emptyWrap = box->verticalLayout()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box->verticalLayout(),
			object_ptr<Ui::VerticalLayout>(box->verticalLayout())));
	emptyWrap->toggleOn(std::move(toggleOn), anim::type::instant);

	const auto content = emptyWrap->entity();
	auto icon = Settings::CreateLottieIcon(
		content,
		{ .name = u"stats"_q, .sizeOverride = Size(st::changePhoneIconSize) },
		st::settingsBlockedListIconPadding);
	content->add(std::move(icon.widget));

	box->setShowFinishedCallback([animate = std::move(icon.animate)] {
		animate(anim::repeat::loop);
	});

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_stats_loading(),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding + st::boxRowPadding);

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_stats_loading_subtext(),
				st::statisticsLoadingSubtext)),
		st::changePhoneDescriptionPadding + st::boxRowPadding);

	Settings::AddSkip(content, st::settingsBlockedListIconPadding.top());
}

void FillOverview(not_null<Ui::GenericBox*> box, const AnyStats &stats) {
	using Value = Data::StatisticalValue;

	const auto &channel = stats.channel;
	const auto &supergroup = stats.supergroup;
	const auto startDate = channel ? channel.startDate : supergroup.startDate;
	const auto endDate = channel ? channel.endDate : supergroup.endDate;

	Settings::AddSkip(box->verticalLayout());
	{
		const auto header = box->addRow(object_ptr<Statistic::Header>(box));
		header->resize(header->width(), st::statisticsChartHeaderHeight);
		header->setTitle(tr::lng_stats_overview_title(tr::now));
		const auto formatter = u"MMM d"_q;
		const auto from = QDateTime::fromSecsSinceEpoch(startDate);
		const auto to = QDateTime::fromSecsSinceEpoch(endDate);
		header->setRightInfo(QLocale().toString(from.date(), formatter)
			+ ' '
			+ QChar(8212)
			+ ' '
			+ QLocale().toString(to.date(), formatter));
	}
	Settings::AddSkip(box->verticalLayout());

	struct Second final {
		QColor color;
		QString text;
	};

	const auto parseSecond = [&](const Value &v) -> Second {
		const auto diff = v.value - v.previousValue;
		if (!diff) {
			return {};
		}
		return {
			(diff < 0 ? st::menuIconAttentionColor : st::settingsIconBg2)->c,
			QString("%1%2 (%3%)")
				.arg((diff < 0) ? QChar(0x2212) : QChar(0x002B))
				.arg(Lang::FormatCountToShort(std::abs(diff)).string)
				.arg(std::abs(std::round(v.growthRatePercentage * 10.) / 10.))
		};
	};

	const auto container = box->addRow(object_ptr<Ui::RpWidget>(box));

	const auto addPrimary = [&](const Value &v) {
		return Ui::CreateChild<Ui::FlatLabel>(
			container,
			Lang::FormatCountToShort(v.value).string,
			st::statisticsOverviewValue);
	};
	const auto addSub = [&](
			not_null<Ui::RpWidget*> primary,
			const Value &v,
			tr::phrase<> text) {
		const auto data = parseSecond(v);
		const auto second = Ui::CreateChild<Ui::FlatLabel>(
			container,
			data.text,
			st::statisticsOverviewSecondValue);
		second->setTextColorOverride(data.color);
		const auto sub = Ui::CreateChild<Ui::FlatLabel>(
			container,
			text(),
			st::statisticsOverviewSecondValue);

		primary->geometryValue(
		) | rpl::start_with_next([=](const QRect &g) {
			second->moveToLeft(
				rect::right(g) + st::statisticsOverviewSecondValueSkip,
				g.y() + st::statisticsOverviewSecondValueSkip);
			sub->moveToLeft(
				g.x(),
				rect::bottom(g));
		}, primary->lifetime());
	};

	auto height = 0;
	if (const auto &s = channel) {
		const auto memberCount = addPrimary(s.memberCount);
		const auto enabledNotifications = Ui::CreateChild<Ui::FlatLabel>(
			container,
			QString("%1%").arg(
				std::round(s.enabledNotificationsPercentage * 100.) / 100.),
			st::statisticsOverviewValue);
		const auto meanViewCount = addPrimary(s.meanViewCount);
		const auto meanShareCount = addPrimary(s.meanShareCount);

		addSub(
			memberCount,
			s.memberCount,
			tr::lng_stats_overview_member_count);
		addSub(
			enabledNotifications,
			{},
			tr::lng_stats_overview_enabled_notifications);
		addSub(
			meanViewCount,
			s.meanViewCount,
			tr::lng_stats_overview_mean_view_count);
		addSub(
			meanShareCount,
			s.meanShareCount,
			tr::lng_stats_overview_mean_share_count);

		container->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			const auto halfWidth = s.width() / 2;
			enabledNotifications->moveToLeft(halfWidth, 0);
			meanViewCount->moveToLeft(0, meanViewCount->height() * 3);
			meanShareCount->moveToLeft(halfWidth, meanViewCount->y());
		}, container->lifetime());

		height = memberCount->height() * 5;
	} else if (const auto &s = supergroup) {
		const auto memberCount = addPrimary(s.memberCount);
		const auto messageCount = addPrimary(s.messageCount);
		const auto viewerCount = addPrimary(s.viewerCount);
		const auto senderCount = addPrimary(s.senderCount);

		addSub(
			memberCount,
			s.memberCount,
			tr::lng_manage_peer_members);
		addSub(
			messageCount,
			s.messageCount,
			tr::lng_stats_overview_messages);
		addSub(
			viewerCount,
			s.viewerCount,
			tr::lng_stats_overview_group_mean_view_count);
		addSub(
			senderCount,
			s.senderCount,
			tr::lng_stats_overview_group_mean_post_count);

		container->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			const auto halfWidth = s.width() / 2;
			messageCount->moveToLeft(halfWidth, 0);
			viewerCount->moveToLeft(0, memberCount->height() * 3);
			senderCount->moveToLeft(halfWidth, viewerCount->y());
		}, container->lifetime());

		height = memberCount->height() * 5;
	}

	container->showChildren();
	container->resize(container->width(), height);
}

} // namespace

void StatisticsBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	box->setTitle(tr::lng_stats_title());
	const auto loaded = box->lifetime().make_state<rpl::event_stream<bool>>();
	FillLoading(
		box,
		loaded->events_starting_with(false) | rpl::map(!rpl::mappers::_1));

	const auto descriptor = Descriptor{
		peer,
		box->lifetime().make_state<Api::Statistics>(&peer->session().api()),
		box->uiShow()->toastParent(),
	};

	descriptor.api->request(
		descriptor.peer
	) | rpl::start_with_done([=] {
		const auto anyStats = AnyStats{
			descriptor.api->channelStats(),
			descriptor.api->supergroupStats(),
		};
		if (!anyStats.channel && !anyStats.supergroup) {
			return;
		}
		FillOverview(box, anyStats);
		FillStatistic(box, descriptor, anyStats);
		loaded->fire(true);
		box->verticalLayout()->resizeToWidth(box->width());
		box->showChildren();
	}, box->lifetime());
}

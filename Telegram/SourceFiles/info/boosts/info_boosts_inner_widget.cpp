/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/info_boosts_inner_widget.h"

#include "api/api_statistics.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/boxes/boost_box.h"
#include "ui/rect.h"
#include "ui/widgets/labels.h"
#include "styles/style_statistics.h"

namespace Info::Boosts {
namespace {

void AddHeader(
		not_null<Ui::VerticalLayout*> content,
		tr::phrase<> text) {
	const auto header = content->add(
		object_ptr<Statistic::Header>(content),
		st::statisticsLayerMargins + st::statisticsChartHeaderPadding);
	header->resizeToWidth(header->width());
	header->setTitle(text(tr::now));
	header->setSubTitle({});
}

void FillOverview(
		not_null<Ui::VerticalLayout*> content,
		const Data::BoostStatus &status) {
	const auto &stats = status.overview;

	::Settings::AddSkip(content, st::statisticsLayerOverviewMargins.top());
	AddHeader(content, tr::lng_stats_overview_title);
	::Settings::AddSkip(content);

	const auto diffBetweenHeaders = 0
		+ st::statisticsOverviewValue.style.font->height
		- st::statisticsHeaderTitleTextStyle.font->height;

	const auto container = content->add(
		object_ptr<Ui::RpWidget>(content),
		st::statisticsLayerMargins);

	const auto addPrimary = [&](float64 v) {
		return Ui::CreateChild<Ui::FlatLabel>(
			container,
			(v >= 0)
				? Lang::FormatCountToShort(v).string
				: QString(),
			st::statisticsOverviewValue);
	};
	const auto addSub = [&](
			not_null<Ui::RpWidget*> primary,
			float64 percentage,
			tr::phrase<> text) {
		const auto second = Ui::CreateChild<Ui::FlatLabel>(
			container,
			percentage
				? u"%1%"_q.arg(std::abs(std::round(percentage * 10.) / 10.))
				: QString(),
			st::statisticsOverviewSecondValue);
		second->setTextColorOverride(st::windowSubTextFg->c);
		const auto sub = Ui::CreateChild<Ui::FlatLabel>(
			container,
			text(),
			st::statisticsOverviewSubtext);
		sub->setTextColorOverride(st::windowSubTextFg->c);

		primary->geometryValue(
		) | rpl::start_with_next([=](const QRect &g) {
			const auto &padding = st::statisticsOverviewSecondValuePadding;
			second->moveToLeft(
				rect::right(g) + padding.left(),
				g.y() + padding.top());
			sub->moveToLeft(
				g.x(),
				st::statisticsChartHeaderHeight
					- st::statisticsOverviewSubtext.style.font->height
					+ g.y()
					+ diffBetweenHeaders);
		}, primary->lifetime());
	};


	const auto topLeftLabel = addPrimary(stats.level);
	const auto topRightLabel = addPrimary(stats.premiumMemberCount);
	const auto bottomLeftLabel = addPrimary(stats.boostCount);
	const auto bottomRightLabel = addPrimary(
		stats.nextLevelBoostCount - stats.boostCount);

	addSub(
		topLeftLabel,
		0,
		tr::lng_boosts_level);
	addSub(
		topRightLabel,
		stats.premiumMemberPercentage,
		tr::lng_boosts_premium_audience);
	addSub(
		bottomLeftLabel,
		0,
		tr::lng_boosts_existing);
	addSub(
		bottomRightLabel,
		0,
		tr::lng_boosts_next_level);

	container->showChildren();
	container->resize(container->width(), topLeftLabel->height() * 5);
	container->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto halfWidth = s.width() / 2;
		{
			const auto &p = st::statisticsOverviewValuePadding;
			topLeftLabel->moveToLeft(p.left(), p.top());
		}
		topRightLabel->moveToLeft(
			topLeftLabel->x() + halfWidth + st::statisticsOverviewRightSkip,
			topLeftLabel->y());
		bottomLeftLabel->moveToLeft(
			topLeftLabel->x(),
			topLeftLabel->y() + st::statisticsOverviewMidSkip);
		bottomRightLabel->moveToLeft(
			topRightLabel->x(),
			bottomLeftLabel->y());
	}, container->lifetime());
	::Settings::AddSkip(content, st::statisticsLayerOverviewMargins.bottom());
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: VerticalLayout(parent)
, _controller(controller)
, _peer(peer) {
	const auto api = lifetime().make_state<Api::Boosts>(peer);

	const auto fakeShowed = lifetime().make_state<rpl::event_stream<>>();

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		api->request(
		) | rpl::start_with_error_done([](const QString &error) {
		}, [=] {
			const auto status = api->boostStatus();

			Ui::FillBoostLimit(
				fakeShowed->events(),
				rpl::single(status.overview.isBoosted),
				this,
				Ui::BoostBoxData{
					.boost = Ui::BoostCounters{
						.level = status.overview.level,
						.boosts = status.overview.boostCount,
						.thisLevelBoosts
							= status.overview.currentLevelBoostCount,
						.nextLevelBoosts
							= status.overview.nextLevelBoostCount,
						.mine = status.overview.isBoosted,
					}
				});

			FillOverview(this, status);

			resizeToWidth(width());
			crl::on_main([=]{ fakeShowed->fire({}); });
		}, lifetime());
	}, lifetime());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

auto InnerWidget::showRequests() const -> rpl::producer<ShowRequest> {
	return _showRequests.events();
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

not_null<PeerData*> InnerWidget::peer() const {
	return _peer;
}

} // namespace Info::Boosts


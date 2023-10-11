/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/info_boosts_inner_widget.h"

#include "api/api_statistics.h"
#include "ui/boxes/boost_box.h"

namespace Info::Boosts {

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


/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/bot/earn/info_bot_earn_list.h"

#include "api/api_credits.h"
#include "api/api_filter_updates.h"
#include "base/unixtime.h"
#include "core/ui_integration.h"
#include "data/data_channel_earn.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/bot/earn/info_bot_earn_widget.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "info/info_controller.h"
#include "info/statistics/info_statistics_inner_widget.h" // FillLoading.
#include "info/statistics/info_statistics_list_controllers.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "statistics/chart_widget.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/effects/credits_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/slider_natural_width.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_credits.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

namespace Info::BotEarn {
namespace {

void AddHeader(
		not_null<Ui::VerticalLayout*> content,
		tr::phrase<> text) {
	Ui::AddSkip(content);
	const auto header = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			text(),
			st::channelEarnHeaderLabel),
		st::boxRowPadding);
	header->resizeToWidth(header->width());
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: VerticalLayout(parent)
, _controller(controller)
, _peer(peer)
, _show(controller->uiShow()) {
}

void InnerWidget::load() {
	const auto apiLifetime = lifetime().make_state<rpl::lifetime>();

	const auto request = [=](Fn<void(Data::CreditsEarnStatistics)> done) {
		const auto api = apiLifetime->make_state<Api::CreditsEarnStatistics>(
			_peer->asUser());
		api->request(
		) | rpl::start_with_error_done([show = _show](const QString &error) {
			show->showToast(error);
		}, [=] {
			done(api->data());
			apiLifetime->destroy();
		}, *apiLifetime);
	};

	Info::Statistics::FillLoading(
		this,
		Info::Statistics::LoadingType::Earn,
		_loaded.events_starting_with(false) | rpl::map(!rpl::mappers::_1),
		_showFinished.events());

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		request([=](Data::CreditsEarnStatistics state) {
			_state = state;
			_loaded.fire(true);
			fill();

			_peer->session().account().mtpUpdates(
			) | rpl::start_with_next([=](const MTPUpdates &updates) {
				using TL = MTPDupdateStarsRevenueStatus;
				Api::PerformForUpdate<TL>(updates, [&](const TL &d) {
					const auto peerId = peerFromMTP(d.vpeer());
					if (peerId == _peer->id) {
						request([=](Data::CreditsEarnStatistics state) {
							_state = state;
							_stateUpdated.fire({});
						});
					}
				});
			}, lifetime());
		});
	}, lifetime());
}

void InnerWidget::fill() {
	using namespace Info::ChannelEarn;
	const auto container = this;
	const auto &data = _state;
	const auto multiplier = data.usdRate * Data::kEarnMultiplier;

	auto availableBalanceValue = rpl::single(
		data.availableBalance
	) | rpl::then(
		_stateUpdated.events() | rpl::map([=] {
			return _state.availableBalance;
		})
	);
	auto valueToString = [](uint64 v) { return QString::number(v); };

	if (data.revenueGraph.chart) {
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		using Type = Statistic::ChartViewType;
		const auto widget = container->add(
			object_ptr<Statistic::ChartWidget>(container),
			st::statisticsLayerMargins);

		auto chart = data.revenueGraph.chart;
		chart.currencyRate = data.usdRate;

		widget->setChartData(chart, Type::StackBar);
		widget->setTitle(tr::lng_bot_earn_chart_revenue());
		Ui::AddSkip(container);
		Ui::AddDivider(container);
		Ui::AddSkip(container);
		Ui::AddSkip(container);
	}
	{
		AddHeader(container, tr::lng_bot_earn_overview_title);
		Ui::AddSkip(container, st::channelEarnOverviewTitleSkip);

		const auto addOverview = [&](
				rpl::producer<uint64> value,
				const tr::phrase<> &text) {
			const auto line = container->add(
				Ui::CreateSkipWidget(container, 0),
				st::boxRowPadding);
			const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				rpl::duplicate(value) | rpl::map(valueToString),
				st::channelEarnOverviewMajorLabel);
			const auto icon = Ui::CreateSingleStarWidget(
				line,
				majorLabel->height());
			const auto secondMinorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				std::move(
					value
				) | rpl::map([=](uint64 v) {
					return v ? ToUsd(v, multiplier) : QString();
				}),
				st::channelEarnOverviewSubMinorLabel);
			rpl::combine(
				line->widthValue(),
				majorLabel->sizeValue()
			) | rpl::start_with_next([=](int available, const QSize &size) {
				line->resize(line->width(), size.height());
				majorLabel->moveToLeft(
					icon->width() + st::channelEarnOverviewMinorLabelSkip,
					majorLabel->y());
				secondMinorLabel->resizeToWidth(available
					- size.width()
					- icon->width());
				secondMinorLabel->moveToLeft(
					rect::right(majorLabel)
						+ st::channelEarnOverviewSubMinorLabelPos.x(),
					st::channelEarnOverviewSubMinorLabelPos.y());
			}, majorLabel->lifetime());
			Ui::ToggleChildrenVisibility(line, true);

			Ui::AddSkip(container);
			container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					text(),
					st::channelEarnOverviewSubMinorLabel),
				st::boxRowPadding);
		};
		addOverview(
			rpl::duplicate(availableBalanceValue),
			tr::lng_bot_earn_available);
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		// addOverview(data.currentBalance, tr::lng_bot_earn_reward);
		// Ui::AddSkip(container);
		// Ui::AddSkip(container);
		addOverview(
			rpl::single(
				data.overallRevenue
			) | rpl::then(
				_stateUpdated.events() | rpl::map([=] {
					return _state.overallRevenue;
				})
			),
			tr::lng_bot_earn_total);
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		Ui::AddDividerText(container, tr::lng_bot_earn_balance_about());
		Ui::AddSkip(container);
	}
	{
		AddHeader(container, tr::lng_bot_earn_balance_title);
		auto dateValue = rpl::single(
			data.nextWithdrawalAt
		) | rpl::then(
			_stateUpdated.events() | rpl::map([=] {
				return _state.nextWithdrawalAt;
			})
		);
		::Settings::AddWithdrawalWidget(
			container,
			_controller->parentController(),
			_peer,
			rpl::single(
				data.buyAdsUrl
			) | rpl::then(
				_stateUpdated.events() | rpl::map([=] {
					return _state.buyAdsUrl;
				})
			),
			rpl::duplicate(availableBalanceValue),
			rpl::duplicate(dateValue),
			std::move(dateValue) | rpl::map([=](const QDateTime &dt) {
				return !dt.isNull() || (!_state.isWithdrawalEnabled);
			}),
			rpl::duplicate(availableBalanceValue) | rpl::map([=](uint64 v) {
				return v ? ToUsd(v, multiplier) : QString();
			}));
	}

	fillHistory();
}

void InnerWidget::fillHistory() {
	const auto container = this;
	Ui::AddSkip(container, st::settingsPremiumOptionsPadding.top());
	const auto history = container->add(
		object_ptr<Ui::VerticalLayout>(container));

	const auto sectionIndex = history->lifetime().make_state<int>(0);

	const auto fill = [=, peer = _peer](
			not_null<PeerData*> premiumBot,
			const Data::CreditsStatusSlice &fullSlice,
			const Data::CreditsStatusSlice &inSlice,
			const Data::CreditsStatusSlice &outSlice) {
		if (fullSlice.list.empty()) {
			return;
		}
		const auto inner = history->add(
			object_ptr<Ui::VerticalLayout>(history));
		const auto hasOneTab = inSlice.list.empty() && outSlice.list.empty();
		const auto hasIn = !inSlice.list.empty();
		const auto hasOut = !outSlice.list.empty();
		const auto fullTabText = tr::lng_credits_summary_history_tab_full(
			tr::now);
		const auto inTabText = tr::lng_credits_summary_history_tab_in(
			tr::now);
		const auto outTabText = tr::lng_credits_summary_history_tab_out(
			tr::now);
		if (hasOneTab) {
			Ui::AddSkip(inner);
			const auto header = inner->add(
				object_ptr<Statistic::Header>(inner),
				st::statisticsLayerMargins
					+ st::boostsChartHeaderPadding);
			header->resizeToWidth(header->width());
			header->setTitle(fullTabText);
			header->setSubTitle({});
		}

		const auto slider = inner->add(
			object_ptr<Ui::SlideWrap<Ui::CustomWidthSlider>>(
				inner,
				object_ptr<Ui::CustomWidthSlider>(
					inner,
					st::defaultTabsSlider)),
			st::boxRowPadding);
		slider->toggle(!hasOneTab, anim::type::instant);

		slider->entity()->addSection(fullTabText);
		if (hasIn) {
			slider->entity()->addSection(inTabText);
		}
		if (hasOut) {
			slider->entity()->addSection(outTabText);
		}

		slider->entity()->setActiveSectionFast(*sectionIndex);

		{
			const auto &st = st::defaultTabsSlider;
			slider->entity()->setNaturalWidth(0
				+ st.labelStyle.font->width(fullTabText)
				+ (hasIn ? st.labelStyle.font->width(inTabText) : 0)
				+ (hasOut ? st.labelStyle.font->width(outTabText) : 0)
				+ rect::m::sum::h(st::boxRowPadding));
		}

		const auto fullWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto inWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));
		const auto outWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));

		rpl::single(slider->entity()->activeSection()) | rpl::then(
			slider->entity()->sectionActivated()
		) | rpl::start_with_next([=](int index) {
			if (index == 0) {
				fullWrap->toggle(true, anim::type::instant);
				inWrap->toggle(false, anim::type::instant);
				outWrap->toggle(false, anim::type::instant);
			} else if (index == 1) {
				inWrap->toggle(true, anim::type::instant);
				fullWrap->toggle(false, anim::type::instant);
				outWrap->toggle(false, anim::type::instant);
			} else {
				outWrap->toggle(true, anim::type::instant);
				fullWrap->toggle(false, anim::type::instant);
				inWrap->toggle(false, anim::type::instant);
			}
			*sectionIndex = index;
		}, inner->lifetime());

		const auto controller = _controller->parentController();
		const auto entryClicked = [=](
				const Data::CreditsHistoryEntry &e,
				const Data::SubscriptionEntry &s) {
			controller->uiShow()->show(Box(
				::Settings::ReceiptCreditsBox,
				controller,
				e,
				s));
		};

		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			fullSlice,
			fullWrap->entity(),
			entryClicked,
			peer,
			true,
			true);
		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			inSlice,
			inWrap->entity(),
			entryClicked,
			peer,
			true,
			false);
		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			outSlice,
			outWrap->entity(),
			std::move(entryClicked),
			peer,
			false,
			true);

		Ui::AddSkip(inner);
		Ui::AddSkip(inner);
	};

	const auto apiLifetime = history->lifetime().make_state<rpl::lifetime>();
	rpl::single(rpl::empty) | rpl::then(
		_stateUpdated.events()
	) | rpl::start_with_next([=, peer = _peer] {
		using Api = Api::CreditsHistory;
		const auto apiFull = apiLifetime->make_state<Api>(peer, true, true);
		const auto apiIn = apiLifetime->make_state<Api>(peer, true, false);
		const auto apiOut = apiLifetime->make_state<Api>(peer, false, true);
		apiFull->request({}, [=](Data::CreditsStatusSlice fullSlice) {
			apiIn->request({}, [=](Data::CreditsStatusSlice inSlice) {
				apiOut->request({}, [=](Data::CreditsStatusSlice outSlice) {
					::Api::PremiumPeerBot(
						&_controller->session()
					) | rpl::start_with_next([=](not_null<PeerData*> bot) {
						fill(bot, fullSlice, inSlice, outSlice);
						container->resizeToWidth(container->width());
						while (history->count() > 1) {
							delete history->widgetAt(0);
						}
						apiLifetime->destroy();
					}, *apiLifetime);
				});
			});
		});
	}, history->lifetime());
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setState(base::take(_state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_state = memento->state();
	if (_state) {
		fill();
	} else {
		load();
	}
	Ui::RpWidget::resizeToWidth(width());
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

void InnerWidget::setInnerFocus() {
	_focusRequested.fire({});
}

not_null<PeerData*> InnerWidget::peer() const {
	return _peer;
}

} // namespace Info::BotEarn


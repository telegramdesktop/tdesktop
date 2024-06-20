/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/bot/earn/info_earn_inner_widget.h"

#include "api/api_credits.h"
#include "api/api_filter_updates.h"
#include "data/data_channel_earn.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/bot/earn/info_earn_widget.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "info/info_controller.h"
#include "info/statistics/info_statistics_inner_widget.h" // FillLoading.
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "statistics/chart_widget.h"
#include "ui/effects/credits_graphics.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
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

[[nodiscard]] not_null<Ui::RpWidget*> CreateIconWidget(
		not_null<Ui::RpWidget*> parent,
		QImage image) {
	const auto widget = Ui::CreateChild<Ui::RpWidget>(parent);
	widget->resize(image.size() / style::DevicePixelRatio());
	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		p.drawImage(0, 0, image);
	}, widget->lifetime());
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
	return widget;
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

	const auto request = [=](Fn<void(Data::BotEarnStatistics)> done) {
		const auto api = apiLifetime->make_state<Api::BotEarnStatistics>(
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
		_loaded.events_starting_with(false) | rpl::map(!rpl::mappers::_1),
		_showFinished.events());

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		request([=](Data::BotEarnStatistics state) {
			_state = state;
			_loaded.fire(true);
			fill();

			_peer->session().account().mtpUpdates(
			) | rpl::start_with_next([=](const MTPUpdates &updates) {
				using TL = MTPDupdateStarsRevenueStatus;
				Api::PerformForUpdate<TL>(updates, [&](const TL &d) {
					const auto peerId = peerFromMTP(d.vpeer());
					if (peerId == _peer->id) {
						request([=](Data::BotEarnStatistics state) {
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
	const auto session = &_peer->session();

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
			const auto icon = CreateIconWidget(
				line,
				Ui::GenerateStars(majorLabel->height(), 1));
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
			const auto sub = container->add(
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
		Ui::AddSkip(container);

		const auto labels = container->add(
			object_ptr<Ui::CenterWrap<Ui::RpWidget>>(
				container,
				object_ptr<Ui::RpWidget>(container)))->entity();

		const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
			labels,
			rpl::duplicate(availableBalanceValue) | rpl::map(valueToString),
			st::channelEarnBalanceMajorLabel);
		const auto icon = CreateIconWidget(
			labels,
			Ui::GenerateStars(majorLabel->height(), 1));
		majorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		majorLabel->sizeValue(
		) | rpl::start_with_next([=](const QSize &majorSize) {
			const auto skip = st::channelEarnBalanceMinorLabelSkip;
			labels->resize(
				majorSize.width() + icon->width() + skip,
				majorSize.height());
			majorLabel->moveToLeft(icon->width() + skip, 0);
		}, labels->lifetime());
		Ui::ToggleChildrenVisibility(labels, true);

		Ui::AddSkip(container);
		container->add(
			object_ptr<Ui::CenterWrap<>>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					rpl::duplicate(
						availableBalanceValue
					) | rpl::map([=](uint64 v) {
						return v ? ToUsd(v, multiplier) : QString();
					}),
					st::channelEarnOverviewSubMinorLabel)));

		Ui::AddSkip(container);

		{
			const auto &st = st::botEarnInputField;
			const auto inputContainer = container->add(
				Ui::CreateSkipWidget(container, st.heightMin));
			const auto currentValue = rpl::variable<uint64>(
				rpl::duplicate(availableBalanceValue));
			const auto input = Ui::CreateChild<Ui::NumberInput>(
				inputContainer,
				st,
				tr::lng_bot_earn_out_ph(),
				QString::number(currentValue.current()),
				currentValue.current());
			rpl::duplicate(
				availableBalanceValue
			) | rpl::start_with_next([=](uint64 v) {
				input->changeLimit(v);
				input->setText(QString::number(v));
			}, input->lifetime());
			const auto icon = CreateIconWidget(
				inputContainer,
				Ui::GenerateStars(st.style.font->height, 1));
			inputContainer->sizeValue(
			) | rpl::start_with_next([=](const QSize &size) {
				input->resize(
					size.width() - rect::m::sum::h(st::boxRowPadding),
					st.heightMin);
				input->moveToLeft(st::boxRowPadding.left(), 0);
				icon->moveToLeft(
					st::boxRowPadding.left(),
					st.textMargins.top());
			}, input->lifetime());
			Ui::ToggleChildrenVisibility(inputContainer, true);
		}

		Ui::AddSkip(container);
		Ui::AddSkip(container);

		const auto &stButton = st::defaultActiveButton;
		const auto button = container->add(
			object_ptr<Ui::RoundButton>(
				container,
				rpl::never<QString>(),
				stButton),
			st::boxRowPadding);

		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			button,
			tr::lng_channel_earn_balance_button(tr::now),
			st::channelEarnSemiboldLabel);
		label->setTextColorOverride(stButton.textFg->c);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		rpl::combine(
			button->sizeValue(),
			label->sizeValue()
		) | rpl::start_with_next([=](const QSize &b, const QSize &l) {
			label->moveToLeft(
				(b.width() - l.width()) / 2,
				(b.height() - l.height()) / 2);
		}, label->lifetime());

		// Api::HandleWithdrawalButton(_peer, button, _controller->uiShow());
		Ui::ToggleChildrenVisibility(button, true);

		Ui::AddSkip(container);
		Ui::AddSkip(container);

		const auto arrow = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::topicButtonArrow,
				st::channelEarnLearnArrowMargins,
				false));
		auto about = Ui::CreateLabelWithCustomEmoji(
			container,
			tr::lng_bot_earn_learn_credits_out_about(
				lt_link,
				tr::lng_channel_earn_about_link(
					lt_emoji,
					rpl::single(arrow),
					Ui::Text::RichLangValue
				) | rpl::map([](TextWithEntities text) {
					return Ui::Text::Link(
						std::move(text),
						tr::lng_bot_earn_balance_about_url(tr::now));
				}),
				Ui::Text::RichLangValue),
			{ .session = session },
			st::boxDividerLabel);
		Ui::AddSkip(container);
		container->add(object_ptr<Ui::DividerLabel>(
			container,
			std::move(about),
			st::defaultBoxDividerLabelPadding,
			RectPart::Top | RectPart::Bottom));

		Ui::AddSkip(container);
	}
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


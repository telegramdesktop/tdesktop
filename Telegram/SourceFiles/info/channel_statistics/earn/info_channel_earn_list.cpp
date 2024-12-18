/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/info_channel_earn_list.h"

#include "api/api_credits.h"
#include "api/api_earn.h"
#include "api/api_filter_updates.h"
#include "api/api_statistics.h"
#include "api/api_text_entities.h"
#include "api/api_updates.h"
#include "base/unixtime.h"
#include "boxes/peers/edit_peer_color_box.h" // AddLevelBadge.
#include "chat_helpers/stickers_emoji_pack.h"
#include "core/application.h"
#include "data/components/credits.h"
#include "data/data_channel.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "info/bot/starref/info_bot_starref_join_widget.h"
#include "info/bot/starref/info_bot_starref_setup_widget.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/channel_statistics/earn/info_channel_earn_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_values.h" // Info::Profile::NameValue.
#include "info/statistics/info_statistics_inner_widget.h" // FillLoading.
#include "info/statistics/info_statistics_list_controllers.h"
#include "iv/iv_instance.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "statistics/chart_widget.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/boost_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/peer_bubble.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/slider_natural_width.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"
#include "styles/style_credits.h"
#include "styles/style_window.h" // mainMenuToggleFourStrokes.

#include <QtWidgets/QApplication>

namespace Info::ChannelEarn {
namespace {

using EarnInt = Data::EarnInt;

[[nodiscard]] bool WithdrawalEnabled(not_null<Main::Session*> session) {
	const auto key = u"channel_revenue_withdrawal_enabled"_q;
	return session->appConfig().get<bool>(key, false);
}

void ShowMenu(not_null<Ui::GenericBox*> box, const QString &text) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(box.get());
	menu->addAction(tr::lng_context_copy_link(tr::now), [=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(text));
		box->uiShow()->showToast(tr::lng_background_link_copied(tr::now));
	});
	menu->popup(QCursor::pos());
}

[[nodiscard]] ClickHandlerPtr LearnMoreCurrencyLink(
		not_null<Window::SessionController*> controller,
		not_null<Ui::GenericBox*> box) {
	const auto url = tr::lng_channel_earn_learn_coin_link(tr::now);

	using Resolver = HistoryView::Controls::WebpageResolver;
	const auto resolver = box->lifetime().make_state<Resolver>(
		&controller->session());
	resolver->request(url);
	return std::make_shared<GenericClickHandler>([=](ClickContext context) {
		if (context.button != Qt::LeftButton) {
			return;
		}
		const auto data = resolver->lookup(url);
		const auto iv = data ? (*data)->iv.get() : nullptr;
		if (iv) {
			Core::App().iv().show(controller, iv, QString());
		} else {
			resolver->resolved(
			) | rpl::start_with_next([=](const QString &s) {
				if (s == url) {
					if (const auto d = resolver->lookup(url)) {
						if (const auto iv = (*d)->iv.get()) {
							Core::App().iv().show(controller, iv, QString());
						}
					}
				}
			}, box->lifetime());
			resolver->request(url);
		}
	});
}

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

void AddRecipient(not_null<Ui::GenericBox*> box, const TextWithEntities &t) {
	const auto wrap = box->addRow(
		object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
			box,
			object_ptr<Ui::RoundButton>(
				box,
				rpl::single(QString()),
				st::channelEarnHistoryRecipientButton)));
	const auto container = wrap->entity();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		container,
		rpl::single(t),
		st::channelEarnHistoryRecipientButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setBreakEverywhere(true);
	label->setTryMakeSimilarLines(true);
	label->resizeToWidth(container->width());
	label->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto padding = QMargins(
			st::chatGiveawayPeerPadding.right(),
			st::chatGiveawayPeerPadding.top(),
			st::chatGiveawayPeerPadding.right(),
			st::chatGiveawayPeerPadding.top());
		container->resize(
			container->width(),
			(Rect(s) + padding).height());
		label->moveToLeft(0, padding.top());
	}, container->lifetime());
	container->setClickedCallback([=] {
		QGuiApplication::clipboard()->setText(t.text);
		box->showToast(tr::lng_text_copied(tr::now));
	});
}

#if 0
[[nodiscard]] TextWithEntities EmojiCurrency(
		not_null<Main::Session*> session) {
	auto emoji = TextWithEntities{
		.text = (QString(QChar(0xD83D)) + QChar(0xDC8E)),
	};
	if (const auto e = Ui::Emoji::Find(emoji.text)) {
		const auto sticker = session->emojiStickersPack().stickerForEmoji(e);
		if (sticker.document) {
			emoji = Data::SingleCustomEmoji(sticker.document);
		}
	}
	return emoji;
}
#endif

[[nodiscard]] QString FormatDate(const QDateTime &date) {
	return tr::lng_group_call_starts_short_date(
		tr::now,
		lt_date,
		langDayOfMonth(date.date()),
		lt_time,
		QLocale().toString(date.time(), QLocale::ShortFormat));
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
	struct State final {
		State(not_null<PeerData*> peer)
		: api(peer)
		, apiCredits(peer)
		, apiCreditsHistory(peer, true, true) {
		}
		Api::EarnStatistics api;
		Api::CreditsEarnStatistics apiCredits;
		Api::CreditsHistory apiCreditsHistory;
		rpl::lifetime apiLifetime;
		rpl::lifetime apiCreditsLifetime;
		rpl::lifetime apiPremiumBotLifetime;
	};
	const auto state = lifetime().make_state<State>(_peer);
	using ChannelFlag = ChannelDataFlag;
	const auto canViewCredits = !_peer->isChannel()
		|| (_peer->asChannel()->flags() & ChannelFlag::CanViewCreditsRevenue);

	Info::Statistics::FillLoading(
		this,
		Info::Statistics::LoadingType::Earn,
		_loaded.events_starting_with(false) | rpl::map(!rpl::mappers::_1),
		_showFinished.events());

	const auto show = _controller->uiShow();
	const auto fail = [=](const QString &error) { show->showToast(error); };

	const auto finish = [=] {
		_loaded.fire(true);
		fill();
		state->apiLifetime.destroy();
		state->apiCreditsLifetime.destroy();

		_peer->session().account().mtpUpdates(
		) | rpl::start_with_next([=, peerId = _peer->id](
				const MTPUpdates &updates) {
			using TLCreditsUpdate = MTPDupdateStarsRevenueStatus;
			using TLCurrencyUpdate = MTPDupdateBroadcastRevenueTransactions;
			using TLNotificationUpdate = MTPDupdateServiceNotification;
			Api::PerformForUpdate<TLCreditsUpdate>(updates, [&](
					const TLCreditsUpdate &d) {
				if (peerId != peerFromMTP(d.vpeer())) {
					return;
				}
				const auto &data = d.vstatus().data();
				auto &e = _state.creditsEarn;
				e.currentBalance = Data::FromTL(data.vcurrent_balance());
				e.availableBalance = Data::FromTL(data.vavailable_balance());
				e.overallRevenue = Data::FromTL(data.voverall_revenue());
				e.isWithdrawalEnabled = data.is_withdrawal_enabled();
				e.nextWithdrawalAt = data.vnext_withdrawal_at()
					? base::unixtime::parse(
						data.vnext_withdrawal_at()->v)
					: QDateTime();
				state->apiCreditsHistory.request({}, [=](
						const Data::CreditsStatusSlice &data) {
					_state.creditsStatusSlice = data;
					_stateUpdated.fire({});
				});
			});
			Api::PerformForUpdate<TLCurrencyUpdate>(updates, [&](
					const TLCurrencyUpdate &d) {
				if (peerId == peerFromMTP(d.vpeer())) {
					const auto &data = d.vbalances().data();
					auto &e = _state.currencyEarn;
					e.currentBalance = data.vcurrent_balance().v;
					e.availableBalance = data.vavailable_balance().v;
					e.overallRevenue = data.voverall_revenue().v;
					_stateUpdated.fire({});
				}
			});
			Api::PerformForUpdate<TLNotificationUpdate>(updates, [&](
					const TLNotificationUpdate &d) {
				if (Api::IsWithdrawalNotification(d) && d.is_popup()) {
					show->show(Ui::MakeInformBox(TextWithEntities{
						qs(d.vmessage()),
						Api::EntitiesFromMTP(&
							_peer->session(),
							d.ventities().v),
					}));
				}
			});
		}, lifetime());
	};

	_showFinished.events(
	) | rpl::take(1) | rpl::start_with_next([=] {
		state->api.request(
		) | rpl::start_with_error_done(fail, [=] {
			_state.currencyEarn = state->api.data();
			state->apiCreditsHistory.request({}, [=](
					const Data::CreditsStatusSlice &data) {
				_state.creditsStatusSlice = data;
				::Api::PremiumPeerBot(
					&_peer->session()
				) | rpl::start_with_next([=](not_null<PeerData*> bot) {
					_state.premiumBotId = bot->id;
					state->apiCredits.request(
					) | rpl::start_with_error_done([=](const QString &error) {
						if (canViewCredits) {
							fail(error);
						} else {
							_state.creditsEarn = {};
						}
						finish();
					}, [=] {
						_state.creditsEarn = state->apiCredits.data();
						finish();
					}, state->apiCreditsLifetime);
					state->apiPremiumBotLifetime.destroy();
				}, state->apiPremiumBotLifetime);
			});
		}, state->apiLifetime);
	}, lifetime());
}

void InnerWidget::fill() {
	const auto container = this;
	const auto bot = (peerIsUser(_peer->id) && _peer->asUser()->botInfo)
		? _peer->asUser()
		: nullptr;
	const auto channel = _peer->asChannel();
	const auto canViewCurrencyEarn = channel
		? (channel->flags() & ChannelDataFlag::CanViewRevenue)
		: true;
	const auto &data = canViewCurrencyEarn
		? _state.currencyEarn
		: Data::EarnStatistics();
	const auto &creditsData = bot
		? Data::CreditsEarnStatistics()
		: _state.creditsEarn;

	auto currencyStateValue = rpl::single(
		data
	) | rpl::then(
		_stateUpdated.events() | rpl::map([=] {
			return _state.currencyEarn;
		})
	);

	auto creditsStateValue = bot
		? rpl::single(Data::CreditsEarnStatistics()) | rpl::type_erased()
		: rpl::single(creditsData) | rpl::then(
			_stateUpdated.events(
			) | rpl::map([this] { return _state.creditsEarn; })
		);

	constexpr auto kMinorLength = 3;
	constexpr auto kMinus = QChar(0x2212);
	//constexpr auto kApproximately = QChar(0x2248);
	const auto multiplier = data.usdRate;

	const auto creditsToUsdMap = [=](StarsAmount c) {
		const auto creditsMultiplier = _state.creditsEarn.usdRate
			* Data::kEarnMultiplier;
		return c ? ToUsd(c, creditsMultiplier, 0) : QString();
	};

	const auto session = &_peer->session();
	const auto withdrawalEnabled = WithdrawalEnabled(session);
	const auto makeContext = [=](not_null<Ui::FlatLabel*> l) {
		return Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { l->update(); },
		};
	};
	const auto addEmojiToMajor = [=](
			not_null<Ui::FlatLabel*> label,
			rpl::producer<EarnInt> value,
			std::optional<bool> isIn,
			std::optional<QMargins> margins) {
		const auto &st = label->st();
		auto icon = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				Ui::Earn::IconCurrencyColored(
					st.style.font,
					!isIn
						? st::activeButtonBg->c
						: (*isIn)
						? st::boxTextFgGood->c
						: st::menuIconAttentionColor->c),
				margins ? *margins : st::channelEarnCurrencyCommonMargins,
				false));
		const auto prepended = !isIn
			? TextWithEntities()
			: TextWithEntities::Simple((*isIn) ? QChar('+') : kMinus);
		std::move(
			value
		) | rpl::start_with_next([=](EarnInt v) {
			label->setMarkedText(
				base::duplicate(prepended).append(icon).append(MajorPart(v)),
				makeContext(label));
		}, label->lifetime());
	};

	const auto bigCurrencyIcon = Ui::Text::SingleCustomEmoji(
		session->data().customEmojiManager().registerInternalEmoji(
			Ui::Earn::IconCurrencyColored(
				st::boxTitle.style.font,
				st::activeButtonBg->c),
			st::channelEarnCurrencyLearnMargins,
			false));

	const auto arrow = Ui::Text::SingleCustomEmoji(
		session->data().customEmojiManager().registerInternalEmoji(
			st::topicButtonArrow,
			st::channelEarnLearnArrowMargins,
			true));
	const auto addAboutWithLearn = [&](const tr::phrase<lngtag_link> &text) {
		auto label = Ui::CreateLabelWithCustomEmoji(
			container,
			text(
				lt_link,
				tr::lng_channel_earn_about_link(
					lt_emoji,
					rpl::single(arrow),
					Ui::Text::RichLangValue
				) | rpl::map([](TextWithEntities text) {
					return Ui::Text::Link(std::move(text), 1);
				}),
				Ui::Text::RichLangValue),
			{ .session = session },
			st::boxDividerLabel);
		label->setLink(1, std::make_shared<LambdaClickHandler>([=] {
			_show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
				box->setNoContentMargin(true);

				const auto content = box->verticalLayout().get();

				Ui::AddSkip(content);
				Ui::AddSkip(content);
				Ui::AddSkip(content);
				{
					const auto &icon = st::channelEarnLearnTitleIcon;
					const auto rect = Rect(icon.size() * 1.4);
					auto owned = object_ptr<Ui::RpWidget>(content);
					owned->resize(rect.size());
					const auto widget = box->addRow(
						object_ptr<Ui::CenterWrap<>>(
							content,
							std::move(owned)))->entity();
					widget->paintRequest(
					) | rpl::start_with_next([=] {
						auto p = Painter(widget);
						auto hq = PainterHighQualityEnabler(p);
						p.setPen(Qt::NoPen);
						p.setBrush(st::activeButtonBg);
						p.drawEllipse(rect);
						icon.paintInCenter(p, rect);
					}, widget->lifetime());
				}
				Ui::AddSkip(content);
				Ui::AddSkip(content);
				box->addRow(object_ptr<Ui::CenterWrap<>>(
					content,
					object_ptr<Ui::FlatLabel>(
						content,
						bot
							? tr::lng_channel_earn_bot_learn_title()
							: tr::lng_channel_earn_learn_title(),
						st::boxTitle)));
				Ui::AddSkip(content);
				Ui::AddSkip(content);
				Ui::AddSkip(content);
				Ui::AddSkip(content);
				{
					const auto padding = QMargins(
						st::settingsButton.padding.left(),
						st::boxRowPadding.top(),
						st::boxRowPadding.right(),
						st::boxRowPadding.bottom());
					const auto addEntry = [&](
							rpl::producer<QString> title,
							rpl::producer<QString> about,
							const style::icon &icon) {
						const auto top = content->add(
							object_ptr<Ui::FlatLabel>(
								content,
								std::move(title),
								st::channelEarnSemiboldLabel),
							padding);
						Ui::AddSkip(content, st::channelEarnHistoryThreeSkip);
						content->add(
							object_ptr<Ui::FlatLabel>(
								content,
								std::move(about),
								st::channelEarnHistoryRecipientLabel),
							padding);
						const auto left = Ui::CreateChild<Ui::RpWidget>(
							box->verticalLayout().get());
						left->paintRequest(
						) | rpl::start_with_next([=] {
							auto p = Painter(left);
							icon.paint(p, 0, 0, left->width());
						}, left->lifetime());
						left->resize(icon.size());
						top->geometryValue(
						) | rpl::start_with_next([=](const QRect &g) {
							left->moveToLeft(
								(g.left() - left->width()) / 2,
								g.top() + st::channelEarnHistoryThreeSkip);
						}, left->lifetime());
					};
					addEntry(
						tr::lng_channel_earn_learn_in_subtitle(),
						bot
							? tr::lng_channel_earn_learn_bot_in_about()
							: tr::lng_channel_earn_learn_in_about(),
						st::channelEarnLearnChannelIcon);
					Ui::AddSkip(content);
					Ui::AddSkip(content);
					addEntry(
						tr::lng_channel_earn_learn_split_subtitle(),
						tr::lng_channel_earn_learn_split_about(),
						st::sponsoredAboutSplitIcon);
					Ui::AddSkip(content);
					Ui::AddSkip(content);
					addEntry(
						tr::lng_channel_earn_learn_out_subtitle(),
						tr::lng_channel_earn_learn_out_about(),
						st::channelEarnLearnWithdrawalsIcon);
					Ui::AddSkip(content);
					Ui::AddSkip(content);
				}
				Ui::AddSkip(content);
				Ui::AddSkip(content);
				{
					const auto l = box->addRow(
						object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
							content,
							Ui::CreateLabelWithCustomEmoji(
								content,
								tr::lng_channel_earn_learn_coin_title(
									lt_emoji,
									rpl::single(
										Ui::Text::Link(bigCurrencyIcon, 1)),
									Ui::Text::RichLangValue
								),
								{ .session = session },
								st::boxTitle)))->entity();
					const auto diamonds = l->lifetime().make_state<int>(0);
					l->setLink(1, std::make_shared<LambdaClickHandler>([=] {
						const auto count = (*diamonds);
						box->showToast((count == 100)
							? u"You are rich now!"_q
							: (u"You have earned "_q
								+ QString::number(++(*diamonds))
								+ (!count
									? u" diamond!"_q
									: u" diamonds!"_q)));
					}));
				}
				Ui::AddSkip(content);
				{
					const auto label = box->addRow(
						Ui::CreateLabelWithCustomEmoji(
							content,
							tr::lng_channel_earn_learn_coin_about(
								lt_link,
								tr::lng_channel_earn_about_link(
									lt_emoji,
									rpl::single(arrow),
									Ui::Text::RichLangValue
								) | rpl::map([](TextWithEntities text) {
									return Ui::Text::Link(std::move(text), 1);
								}),
								Ui::Text::RichLangValue
							),
							{ .session = session },
							st::channelEarnLearnDescription));
					label->resizeToWidth(box->width()
						- rect::m::sum::h(st::boxRowPadding));
					label->setLink(
						1,
						LearnMoreCurrencyLink(
							_controller->parentController(),
							box));
				}
				Ui::AddSkip(content);
				Ui::AddSkip(content);
				{
					const auto &st = st::premiumPreviewDoubledLimitsBox;
					box->setStyle(st);
					auto button = object_ptr<Ui::RoundButton>(
						container,
						tr::lng_channel_earn_learn_close(),
						st::defaultActiveButton);
					button->setTextTransform(
						Ui::RoundButton::TextTransform::NoTransform);
					button->resizeToWidth(box->width()
						- st.buttonPadding.left()
						- st.buttonPadding.left());
					button->setClickedCallback([=] { box->closeBox(); });
					box->addButton(std::move(button));
				}
			}));
		}));
		container->add(object_ptr<Ui::DividerLabel>(
			container,
			std::move(label),
			st::defaultBoxDividerLabelPadding,
			RectPart::Top | RectPart::Bottom));
	};
	addAboutWithLearn(bot
		? tr::lng_channel_earn_about_bot
		: tr::lng_channel_earn_about);
	{
		using Type = Statistic::ChartViewType;
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		auto hasPreviousChart = false;
		if (data.topHoursGraph.chart) {
			const auto widget = container->add(
				object_ptr<Statistic::ChartWidget>(container),
				st::statisticsLayerMargins);

			widget->setChartData(data.topHoursGraph.chart, Type::Bar);
			widget->setTitle(tr::lng_channel_earn_chart_top_hours());
			hasPreviousChart = true;
		}
		if (data.revenueGraph.chart) {
			if (hasPreviousChart) {
				Ui::AddSkip(container);
				Ui::AddDivider(container);
				Ui::AddSkip(container);
				Ui::AddSkip(container);
			}
			const auto widget = container->add(
				object_ptr<Statistic::ChartWidget>(container),
				st::statisticsLayerMargins);

			widget->setChartData([&] {
				auto chart = data.revenueGraph.chart;
				chart.currencyRate = multiplier;
				return chart;
			}(), Type::StackBar);
			widget->setTitle(tr::lng_channel_earn_chart_revenue());
			hasPreviousChart = true;
		}
		if (creditsData.revenueGraph.chart) {
			if (hasPreviousChart) {
				Ui::AddSkip(container);
				Ui::AddDivider(container);
				Ui::AddSkip(container);
				Ui::AddSkip(container);
			}
			const auto widget = container->add(
				object_ptr<Statistic::ChartWidget>(container),
				st::statisticsLayerMargins);

			widget->setChartData([&] {
				auto chart = creditsData.revenueGraph.chart;
				chart.currencyRate = creditsData.usdRate;
				return chart;
			}(), Type::StackBar);
			widget->setTitle(tr::lng_bot_earn_chart_revenue());
		}
	}
	if (data.topHoursGraph.chart
		|| data.revenueGraph.chart
		|| creditsData.revenueGraph.chart) {
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		Ui::AddDivider(container);
		Ui::AddSkip(container);
	}
	{
		AddHeader(container, tr::lng_channel_earn_overview_title);
		Ui::AddSkip(container, st::channelEarnOverviewTitleSkip);

		const auto addOverview = [&](
				rpl::producer<EarnInt> currencyValue,
				rpl::producer<StarsAmount> creditsValue,
				const tr::phrase<> &text,
				bool showCurrency,
				bool showCredits) {
			const auto line = container->add(
				Ui::CreateSkipWidget(container, 0),
				st::boxRowPadding);
			const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				st::channelEarnOverviewMajorLabel);
			addEmojiToMajor(
				majorLabel,
				rpl::duplicate(currencyValue),
				{},
				{});
			const auto minorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				rpl::duplicate(currencyValue) | rpl::map([=](EarnInt v) {
					return MinorPart(v).left(kMinorLength);
				}),
				st::channelEarnOverviewMinorLabel);
			const auto secondMinorLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				std::move(
					currencyValue
				) | rpl::map([=](EarnInt value) {
					return value
						? ToUsd(value, multiplier, kMinorLength)
						: QString();
				}),
				st::channelEarnOverviewSubMinorLabel);

			const auto creditsLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				rpl::duplicate(creditsValue) | rpl::map([](StarsAmount value) {
					return Lang::FormatStarsAmountDecimal(value);
				}),
				st::channelEarnOverviewMajorLabel);
			const auto icon = Ui::CreateSingleStarWidget(
				line,
				creditsLabel->height());
			const auto creditsSecondLabel = Ui::CreateChild<Ui::FlatLabel>(
				line,
				rpl::duplicate(creditsValue) | rpl::map(creditsToUsdMap),
				st::channelEarnOverviewSubMinorLabel);
			rpl::combine(
				line->widthValue(),
				majorLabel->sizeValue(),
				creditsLabel->sizeValue(),
				std::move(creditsValue)
			) | rpl::start_with_next([=](
					int available,
					const QSize &size,
					const QSize &creditsSize,
					StarsAmount credits) {
				const auto skip = st::channelEarnOverviewSubMinorLabelPos.x();
				line->resize(line->width(), size.height());
				minorLabel->moveToLeft(
					size.width(),
					st::channelEarnOverviewMinorLabelSkip);
				secondMinorLabel->resizeToWidth(
					(showCredits ? (available / 2) : available)
						- size.width()
						- minorLabel->width());
				secondMinorLabel->moveToLeft(
					rect::right(minorLabel) + skip,
					st::channelEarnOverviewSubMinorLabelPos.y());

				icon->moveToLeft(
					showCurrency
						? (available / 2 + st::boxRowPadding.left() / 2)
						: 0,
					0);
				creditsLabel->moveToLeft(rect::right(icon) + skip, 0);
				creditsSecondLabel->moveToLeft(
					rect::right(creditsLabel) + skip,
					st::channelEarnOverviewSubMinorLabelPos.y());
				creditsSecondLabel->resizeToWidth(
					available - creditsSecondLabel->pos().x());
				if (!showCredits) {
					const auto x = std::numeric_limits<int>::max();
					icon->moveToLeft(x, 0);
					creditsLabel->moveToLeft(x, 0);
					creditsSecondLabel->moveToLeft(x, 0);
				}
				if (!showCurrency) {
					const auto x = std::numeric_limits<int>::max();
					majorLabel->moveToLeft(x, 0);
					minorLabel->moveToLeft(x, 0);
					secondMinorLabel->moveToLeft(x, 0);
				}
			}, minorLabel->lifetime());
			Ui::ToggleChildrenVisibility(line, true);

			Ui::AddSkip(container);
			const auto sub = container->add(
				object_ptr<Ui::FlatLabel>(
					container,
					text(),
					st::channelEarnOverviewSubMinorLabel),
				st::boxRowPadding);
			sub->setTextColorOverride(st::windowSubTextFg->c);
		};
		auto availValueMap = [](const auto &v) { return v.availableBalance; };
		auto currentValueMap = [](const auto &v) { return v.currentBalance; };
		auto overallValueMap = [](const auto &v) { return v.overallRevenue; };
		const auto hasAnyCredits = creditsData.availableBalance
			|| creditsData.currentBalance
			|| creditsData.overallRevenue;
		addOverview(
			rpl::duplicate(currencyStateValue) | rpl::map(availValueMap),
			rpl::duplicate(creditsStateValue) | rpl::map(availValueMap),
			tr::lng_channel_earn_available,
			canViewCurrencyEarn,
			hasAnyCredits);
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		addOverview(
			rpl::duplicate(currencyStateValue) | rpl::map(currentValueMap),
			rpl::duplicate(creditsStateValue) | rpl::map(currentValueMap),
			tr::lng_channel_earn_reward,
			canViewCurrencyEarn,
			hasAnyCredits);
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		addOverview(
			rpl::duplicate(currencyStateValue) | rpl::map(overallValueMap),
			rpl::duplicate(creditsStateValue) | rpl::map(overallValueMap),
			tr::lng_channel_earn_total,
			canViewCurrencyEarn,
			hasAnyCredits);
		Ui::AddSkip(container);
	}
#ifndef _DEBUG
	if (channel && !channel->amCreator()) {
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		return;
	}
#endif
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	if (channel && data.availableBalance) {
		const auto value = data.availableBalance;
		AddHeader(container, tr::lng_channel_earn_balance_title);
		Ui::AddSkip(container);

		const auto labels = container->add(
			object_ptr<Ui::CenterWrap<Ui::RpWidget>>(
				container,
				object_ptr<Ui::RpWidget>(container)))->entity();

		const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
			labels,
			st::channelEarnBalanceMajorLabel);
		{
			const auto &m = st::channelEarnCurrencyCommonMargins;
			const auto p = QMargins(m.left(), 0, m.right(), m.bottom());
			addEmojiToMajor(majorLabel, rpl::single(value), {}, p);
		}
		majorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto minorLabel = Ui::CreateChild<Ui::FlatLabel>(
			labels,
			MinorPart(value),
			st::channelEarnBalanceMinorLabel);
		minorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		rpl::combine(
			majorLabel->sizeValue(),
			minorLabel->sizeValue()
		) | rpl::start_with_next([=](
				const QSize &majorSize,
				const QSize &minorSize) {
			labels->resize(
				majorSize.width() + minorSize.width(),
				majorSize.height());
			majorLabel->moveToLeft(0, 0);
			minorLabel->moveToRight(
				0,
				st::channelEarnBalanceMinorLabelSkip);
		}, labels->lifetime());
		Ui::ToggleChildrenVisibility(labels, true);

		Ui::AddSkip(container);
		container->add(
			object_ptr<Ui::CenterWrap<>>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					ToUsd(value, multiplier, 0),
					st::channelEarnOverviewSubMinorLabel)));

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

		const auto colorText = [=](float64 value) {
			label->setTextColorOverride(
				anim::with_alpha(
					stButton.textFg->c,
					anim::interpolateF(.5, 1., value)));
		};
		colorText(withdrawalEnabled ? 1. : 0.);
#ifndef _DEBUG
		button->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!withdrawalEnabled);
#endif

		Api::HandleWithdrawalButton(
			{ .currencyReceiver = channel },
			button,
			_controller->uiShow());
		Ui::ToggleChildrenVisibility(button, true);

		Ui::AddSkip(container);
		Ui::AddSkip(container);
		addAboutWithLearn(withdrawalEnabled
			? tr::lng_channel_earn_balance_about
			: tr::lng_channel_earn_balance_about_temp);
		Ui::AddSkip(container);
	}
	if (creditsData.availableBalance.value() > 0) {
		AddHeader(container, tr::lng_bot_earn_balance_title);
		auto availableBalanceValue = rpl::single(
			creditsData.availableBalance
		) | rpl::then(
			_stateUpdated.events() | rpl::map([=] {
				return _state.creditsEarn.availableBalance;
			})
		);
		auto dateValue = rpl::single(
			creditsData.nextWithdrawalAt
		) | rpl::then(
			_stateUpdated.events() | rpl::map([=] {
				return _state.creditsEarn.nextWithdrawalAt;
			})
		);
		::Settings::AddWithdrawalWidget(
			container,
			_controller->parentController(),
			_peer,
			rpl::single(
				creditsData.buyAdsUrl
			) | rpl::then(
				_stateUpdated.events() | rpl::map([=] {
					return _state.creditsEarn.buyAdsUrl;
				})
			),
			rpl::duplicate(availableBalanceValue),
			rpl::duplicate(dateValue),
			_state.creditsEarn.isWithdrawalEnabled,
			rpl::duplicate(
				availableBalanceValue
			) | rpl::map(creditsToUsdMap));
	}

	if (Info::BotStarRef::Join::Allowed(_peer)) {
		const auto button = Info::BotStarRef::AddViewListButton(
			container,
			tr::lng_credits_summary_earn_title(),
			tr::lng_credits_summary_earn_about(),
			true);
		button->setClickedCallback([=] {
			_controller->showSection(Info::BotStarRef::Join::Make(_peer));
		});
		Ui::AddSkip(container);
		Ui::AddDivider(container);
	}
	Ui::AddSkip(container);

	const auto sectionIndex = container->lifetime().make_state<int>(0);
	const auto rebuildLists = [=](
			const Memento::SavedState &data,
			not_null<Ui::VerticalLayout*> listsContainer) {
		const auto hasCurrencyTab
			= !data.currencyEarn.firstHistorySlice.list.empty();
		const auto hasCreditsTab = !data.creditsStatusSlice.list.empty();
		const auto hasOneTab = (hasCurrencyTab || hasCreditsTab)
			&& (hasCurrencyTab != hasCreditsTab);

		const auto currencyTabText = tr::lng_channel_earn_currency_history(
			tr::now);
		const auto creditsTabText = tr::lng_channel_earn_credits_history(
			tr::now);

		const auto slider = listsContainer->add(
			object_ptr<Ui::SlideWrap<Ui::CustomWidthSlider>>(
				listsContainer,
				object_ptr<Ui::CustomWidthSlider>(
					listsContainer,
					st::defaultTabsSlider)),
			st::boxRowPadding);
		slider->toggle(
			((hasCurrencyTab ? 1 : 0) + (hasCreditsTab ? 1 : 0) > 1),
			anim::type::instant);

		if (hasCurrencyTab) {
			slider->entity()->addSection(currencyTabText);
		}
		if (hasCreditsTab) {
			slider->entity()->addSection(creditsTabText);
		}

		{
			const auto &st = st::defaultTabsSlider;
			slider->entity()->setNaturalWidth(0
				+ (hasCurrencyTab
					? st.labelStyle.font->width(currencyTabText)
					: 0)
				+ (hasCreditsTab
					? st.labelStyle.font->width(creditsTabText)
					: 0)
				+ rect::m::sum::h(st::boxRowPadding));
		}

		if (hasOneTab) {
			if (hasCurrencyTab) {
				AddHeader(listsContainer, tr::lng_channel_earn_history_title);
				AddSkip(listsContainer);
			} else if (hasCreditsTab) {
				AddHeader(
					listsContainer,
					tr::lng_channel_earn_credits_history);
				slider->entity()->setActiveSectionFast(1);
			}
		} else {
			slider->entity()->setActiveSectionFast(*sectionIndex);
		}

		const auto tabCurrencyList = listsContainer->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				listsContainer,
				object_ptr<Ui::VerticalLayout>(listsContainer)));
		const auto tabCreditsList = listsContainer->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				listsContainer,
				object_ptr<Ui::VerticalLayout>(listsContainer)));

		rpl::single(slider->entity()->activeSection()) | rpl::then(
			slider->entity()->sectionActivated()
		) | rpl::start_with_next([=](int index) {
			if (index == 0) {
				tabCurrencyList->toggle(true, anim::type::instant);
				tabCreditsList->toggle(false, anim::type::instant);
			} else if (index == 1) {
				tabCurrencyList->toggle(false, anim::type::instant);
				tabCreditsList->toggle(true, anim::type::instant);
			}
			*sectionIndex = index;
		}, listsContainer->lifetime());

		if (hasCurrencyTab) {
			Ui::AddSkip(listsContainer);

			const auto historyList = tabCurrencyList->entity();
			const auto addHistoryEntry = [=](
					const Data::EarnHistoryEntry &entry,
					const tr::phrase<> &text) {
				const auto wrap = historyList->add(
					object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
						historyList,
						object_ptr<Ui::VerticalLayout>(historyList),
						QMargins()));
				const auto inner = wrap->entity();
				inner->setAttribute(Qt::WA_TransparentForMouseEvents);
				inner->add(object_ptr<Ui::FlatLabel>(
					inner,
					text(),
					st::channelEarnSemiboldLabel));

				const auto isIn
					= (entry.type == Data::EarnHistoryEntry::Type::In);
				const auto recipient = Ui::Text::Wrapped(
					{ entry.provider },
					EntityType::Code);
				if (!recipient.text.isEmpty()) {
					Ui::AddSkip(inner, st::channelEarnHistoryThreeSkip);
					const auto label = inner->add(object_ptr<Ui::FlatLabel>(
						inner,
						rpl::single(recipient),
						st::channelEarnHistoryRecipientLabel));
					label->setBreakEverywhere(true);
					label->setTryMakeSimilarLines(true);
					Ui::AddSkip(inner, st::channelEarnHistoryThreeSkip);
				} else {
					Ui::AddSkip(inner, st::channelEarnHistoryTwoSkip);
				}

				const auto isFailed = entry.status
					== Data::EarnHistoryEntry::Status::Failed;
				const auto isPending = entry.status
					== Data::EarnHistoryEntry::Status::Pending;
				const auto dateText = (!entry.dateTo.isNull() || isFailed)
					? (FormatDate(entry.date)
						+ ' '
						+ QChar(8212)
						+ ' '
						+ (isFailed
							? tr::lng_channel_earn_history_out_failed(tr::now)
							: FormatDate(entry.dateTo)))
					: isPending
					? tr::lng_channel_earn_history_pending(tr::now)
					: FormatDate(entry.date);
				inner->add(object_ptr<Ui::FlatLabel>(
					inner,
					dateText,
					st::channelEarnHistorySubLabel)
				)->setTextColorOverride(isFailed
					? std::make_optional<QColor>(
						st::menuIconAttentionColor->c)
					: std::nullopt);

				const auto color = (isIn
					? st::boxTextFgGood
					: st::menuIconAttentionColor)->c;
				const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
					wrap,
					st::channelEarnHistoryMajorLabel);
				addEmojiToMajor(
					majorLabel,
					rpl::single(entry.amount),
					isIn,
					{});
				majorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
				majorLabel->setTextColorOverride(color);
				const auto minorText = MinorPart(entry.amount);
				const auto minorLabel = Ui::CreateChild<Ui::FlatLabel>(
					wrap,
					rpl::single(minorText),
					st::channelEarnHistoryMinorLabel);
				minorLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
				minorLabel->setTextColorOverride(color);
				const auto button = Ui::CreateChild<Ui::SettingsButton>(
					wrap,
					rpl::single(QString()));
				Ui::ToggleChildrenVisibility(wrap, true);

				const auto detailsBox = [=, peer = _peer](
						not_null<Ui::GenericBox*> box) {
					box->addTopButton(
						st::boxTitleClose,
						[=] { box->closeBox(); });
					Ui::AddSkip(box->verticalLayout());
					Ui::AddSkip(box->verticalLayout());
					const auto labels = box->addRow(
						object_ptr<Ui::CenterWrap<Ui::RpWidget>>(
							box,
							object_ptr<Ui::RpWidget>(box)))->entity();

					const auto majorLabel = Ui::CreateChild<Ui::FlatLabel>(
						labels,
						st::channelEarnOverviewMajorLabel);
					addEmojiToMajor(
						majorLabel,
						rpl::single(entry.amount),
						isIn,
						{});
					majorLabel->setAttribute(
						Qt::WA_TransparentForMouseEvents);
					majorLabel->setTextColorOverride(color);
					const auto minorLabel = Ui::CreateChild<Ui::FlatLabel>(
						labels,
						minorText,
						st::channelEarnOverviewMinorLabel);
					minorLabel->setAttribute(
						Qt::WA_TransparentForMouseEvents);
					minorLabel->setTextColorOverride(color);
					rpl::combine(
						majorLabel->sizeValue(),
						minorLabel->sizeValue()
					) | rpl::start_with_next([=](
							const QSize &majorSize,
							const QSize &minorSize) {
						labels->resize(
							majorSize.width() + minorSize.width(),
							majorSize.height());
						majorLabel->moveToLeft(0, 0);
						minorLabel->moveToRight(
							0,
							st::channelEarnOverviewMinorLabelSkip);
					}, box->lifetime());

					Ui::AddSkip(box->verticalLayout());
					box->addRow(object_ptr<Ui::CenterWrap<>>(
						box,
						object_ptr<Ui::FlatLabel>(
							box,
							dateText,
							st::channelEarnHistorySubLabel)));
					Ui::AddSkip(box->verticalLayout());
					Ui::AddSkip(box->verticalLayout());
					Ui::AddSkip(box->verticalLayout());
					box->addRow(object_ptr<Ui::CenterWrap<>>(
						box,
						object_ptr<Ui::FlatLabel>(
							box,
							isIn
								? tr::lng_channel_earn_history_in_about()
								: tr::lng_channel_earn_history_out(),
							st::channelEarnHistoryDescriptionLabel)));
					Ui::AddSkip(box->verticalLayout());
					if (isIn) {
						Ui::AddSkip(box->verticalLayout());
					}

					if (!recipient.text.isEmpty()) {
						AddRecipient(box, recipient);
					}
					if (isIn) {
						box->addRow(
							object_ptr<Ui::CenterWrap<>>(
								box,
								Ui::CreatePeerBubble(box, peer)));
					}
					const auto closeBox = [=] { box->closeBox(); };
					{
						const auto &st = st::premiumPreviewDoubledLimitsBox;
						box->setStyle(st);
						auto button = object_ptr<Ui::RoundButton>(
							box,
							(!entry.successLink.isEmpty())
								? tr::lng_channel_earn_history_out_button()
								: tr::lng_box_ok(),
							st::defaultActiveButton);
						button->resizeToWidth(box->width()
							- st.buttonPadding.left()
							- st.buttonPadding.left());
						if (!entry.successLink.isEmpty()) {
							button->setAcceptBoth();
							button->addClickHandler([=](
									Qt::MouseButton button) {
								if (button == Qt::LeftButton) {
									UrlClickHandler::Open(entry.successLink);
								} else if (button == Qt::RightButton) {
									ShowMenu(box, entry.successLink);
								}
							});
						} else {
							button->setClickedCallback(closeBox);
						}
						box->addButton(std::move(button));
					}
					Ui::AddSkip(box->verticalLayout());
					Ui::AddSkip(box->verticalLayout());
					box->addButton(tr::lng_box_ok(), closeBox);
				};

				button->setClickedCallback([=] {
					_show->showBox(Box(detailsBox));
				});
				wrap->geometryValue(
				) | rpl::start_with_next([=](const QRect &g) {
					const auto &padding = st::boxRowPadding;
					const auto majorTop = (g.height() - majorLabel->height())
						/ 2;
					minorLabel->moveToRight(
						padding.right(),
						majorTop + st::channelEarnHistoryMinorLabelSkip);
					majorLabel->moveToRight(
						padding.right() + minorLabel->width(),
						majorTop);
					const auto rightWrapPadding = rect::m::sum::h(padding)
						+ minorLabel->width()
						+ majorLabel->width();
					wrap->setPadding(st::channelEarnHistoryOuter
						+ QMargins(padding.left(), 0, rightWrapPadding, 0));
					button->resize(g.size());
					button->lower();
				}, wrap->lifetime());
			};
			const auto handleSlice = [=](const Data::EarnHistorySlice &s) {
				using Type = Data::EarnHistoryEntry::Type;
				for (const auto &entry : s.list) {
					addHistoryEntry(
						entry,
						(entry.type == Type::In)
							? tr::lng_channel_earn_history_in
							: (entry.type == Type::Return)
							? tr::lng_channel_earn_history_return
							: tr::lng_channel_earn_history_out);
				}
				historyList->resizeToWidth(listsContainer->width());
			};
			const auto &firstSlice = data.currencyEarn.firstHistorySlice;
			handleSlice(firstSlice);
			if (!firstSlice.allLoaded) {
				struct ShowMoreState final {
					ShowMoreState(not_null<PeerData*> peer)
					: api(peer) {
					}
					Api::EarnStatistics api;
					bool loading = false;
					Data::EarnHistorySlice::OffsetToken token;
					rpl::variable<int> showed = 0;
				};
				const auto state
					= lifetime().make_state<ShowMoreState>(_peer);
				state->token = firstSlice.token;
				state->showed = firstSlice.list.size();
				const auto max = firstSlice.total;
				const auto wrap = listsContainer->add(
					object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
						listsContainer,
						object_ptr<Ui::SettingsButton>(
							listsContainer,
							tr::lng_channel_earn_history_show_more(
								lt_count,
								state->showed.value(
								) | rpl::map(
									max - rpl::mappers::_1
								) | tr::to_count()),
							st::statisticsShowMoreButton)));
				const auto button = wrap->entity();
				Ui::AddToggleUpDownArrowToMoreButton(button);

				wrap->toggle(true, anim::type::instant);
				const auto handleReceived = [=](
						Data::EarnHistorySlice slice) {
					state->loading = false;
					handleSlice(slice);
					wrap->toggle(!slice.allLoaded, anim::type::instant);
					state->token = slice.token;
					state->showed = state->showed.current()
						+ slice.list.size();
				};
				button->setClickedCallback([=] {
					if (state->loading) {
						return;
					}
					state->loading = true;
					state->api.requestHistory(state->token, handleReceived);
				});
			}
		}
		if (hasCreditsTab) {
			const auto controller = _controller->parentController();
			const auto show = controller->uiShow();
			const auto entryClicked = [=](
					const Data::CreditsHistoryEntry &e,
					const Data::SubscriptionEntry &s) {
				show->show(Box(
					::Settings::ReceiptCreditsBox,
					controller,
					e,
					s));
			};

			Info::Statistics::AddCreditsHistoryList(
				show,
				data.creditsStatusSlice,
				tabCreditsList->entity(),
				entryClicked,
				_peer,
				true,
				true);
		}
		if (hasCurrencyTab || hasCreditsTab) {
			Ui::AddSkip(listsContainer);
			Ui::AddDivider(listsContainer);
			Ui::AddSkip(listsContainer);
		}

		listsContainer->resizeToWidth(width());
	};

	const auto historyContainer = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	rpl::single(rpl::empty) | rpl::then(
		_stateUpdated.events()
	) | rpl::start_with_next([=] {
		const auto listsContainer = historyContainer->add(
			object_ptr<Ui::VerticalLayout>(container));
		rebuildLists(_state, listsContainer);
		while (historyContainer->count() > 1) {
			delete historyContainer->widgetAt(0);
		}
	}, historyContainer->lifetime());

	if (channel) {
		//constexpr auto kMaxCPM = 50; // Debug.
		const auto requiredLevel = Data::LevelLimits(session)
			.channelRestrictSponsoredLevelMin();
		const auto &phrase = tr::lng_channel_earn_off;
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container,
			phrase(),
			st::settingsButtonNoIconLocked));
		const auto toggled = lifetime().make_state<rpl::event_stream<bool>>();
		const auto isLocked = channel->levelHint() < requiredLevel;
		const auto reason = Ui::AskBoostReason{
			.data = Ui::AskBoostCpm{ .requiredLevel = requiredLevel },
		};

		AddLevelBadge(
			requiredLevel,
			button,
			nullptr,
			channel,
			QMargins(st::boxRowPadding.left(), 0, 0, 0),
			phrase());

		button->toggleOn(rpl::single(
			data.switchedOff
		) | rpl::then(toggled->events()));
		button->setToggleLocked(isLocked);

		button->toggledChanges(
		) | rpl::start_with_next([=](bool value) {
			if (isLocked && value) {
				toggled->fire(false);
				CheckBoostLevel(
					_controller->uiShow(),
					_peer,
					[=](int level) {
						return (level < requiredLevel)
							? std::make_optional(reason)
							: std::nullopt;
					},
					[] {});
			}
			if (!isLocked) {
				const auto weak = Ui::MakeWeak(this);
				const auto show = _controller->uiShow();
				const auto failed = [=](const QString &e) {
					if (weak.data()) {
						toggled->fire(false);
						show->showToast(e);
					}
				};
				Api::RestrictSponsored(channel, value, failed);
			}
		}, button->lifetime());

		Ui::AddSkip(container);
		Ui::AddDividerText(container, tr::lng_channel_earn_off_about());
	}
	Ui::AddSkip(container);

	Ui::ToggleChildrenVisibility(container, true);
	Ui::RpWidget::resizeToWidth(width());
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setState(base::take(_state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_state = memento->state();
	if (_state.currencyEarn || _state.creditsEarn) {
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

} // namespace Info::ChannelEarn


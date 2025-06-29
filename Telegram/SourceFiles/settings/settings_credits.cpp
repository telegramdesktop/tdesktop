/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_credits.h"

#include "api/api_credits.h"
#include "base/call_delayed.h"
#include "boxes/star_gift_box.h"
#include "boxes/gift_credits_box.h"
#include "boxes/gift_premium_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "core/click_handler_types.h"
#include "data/components/credits.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/bot/earn/info_bot_earn_widget.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "info/bot/starref/info_bot_starref_join_widget.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "info/statistics/info_statistics_list_controllers.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "settings/settings_credits_graphics.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/boxes/boost_box.h" // Ui::StartFireworks.
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/slider_natural_width.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"
#include "styles/style_menu_icons.h"
#include "styles/style_channel_earn.h"

namespace Settings {
namespace {

class Credits : public Section<Credits> {
public:
	Credits(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		CreditsType type);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

	void showFinished() override;

	[[nodiscard]] bool hasFlexibleTopBar() const override;

	void setStepDataReference(std::any &data) override;

	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

private:
	void setupContent();
	void setupHistory(not_null<Ui::VerticalLayout*> container);
	void setupSubscriptions(not_null<Ui::VerticalLayout*> container);
	const not_null<Window::SessionController*> _controller;
	const CreditsType _creditsType;

	QWidget *_parent = nullptr;

	QImage _star;
	QImage _balanceStar;

	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	base::unique_qptr<Ui::IconButton> _close;
	rpl::variable<bool> _backToggles;
	rpl::variable<Info::Wrap> _wrap;
	Fn<void(bool)> _setPaused;

	rpl::event_stream<> _showBack;
	rpl::event_stream<> _showFinished;
	rpl::variable<QString> _buttonText;

};

Credits::Credits(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	CreditsType type)
: Section(parent)
, _controller(controller)
, _creditsType(type)
, _star(Ui::GenerateStars(st::creditsTopupButton.height, 1))
, _balanceStar((_creditsType == CreditsType::Ton)
		? Ui::Earn::IconCurrencyColored(
			st::tonFieldIconSize,
			st::windowActiveTextFg->c)
		: Ui::GenerateStars(st::creditsBalanceStarHeight, 1)) {
	_controller->session().giftBoxStickersPacks().load(
		Stickers::GiftBoxPack::Type::Currency);
	setupContent();

	_controller->session().premiumPossibleValue(
	) | rpl::start_with_next([=](bool premiumPossible) {
		if (!premiumPossible) {
			_showBack.fire({});
		}
	}, lifetime());
}

rpl::producer<QString> Credits::title() {
	if (_creditsType == CreditsType::Ton) {
		return tr::lng_credits_currency_summary_title();
	}
	return tr::lng_premium_summary_title();
}

bool Credits::hasFlexibleTopBar() const {
	return true;
}

rpl::producer<> Credits::sectionShowBack() {
	return _showBack.events();
}

void Credits::setStepDataReference(std::any &data) {
	using SectionCustomTopBarData = Info::Settings::SectionCustomTopBarData;
	const auto my = std::any_cast<SectionCustomTopBarData>(&data);
	if (my) {
		_backToggles = std::move(
			my->backButtonEnables
		) | rpl::map_to(true);
		_wrap = std::move(my->wrapValue);
	}
}

void Credits::setupSubscriptions(not_null<Ui::VerticalLayout*> container) {
	const auto history = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = history->entity();
	const auto self = _controller->session().user();

	const auto fill = [=](const Data::CreditsStatusSlice &fullSlice) {
		const auto inner = content;
		if (fullSlice.subscriptions.empty()) {
			return;
		}
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(
			inner,
			tr::lng_credits_subscription_section(),
			{ 0, 0, 0, -st::settingsPremiumOptionsPadding.bottom() });

		const auto fullWrap = inner->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				inner,
				object_ptr<Ui::VerticalLayout>(inner)));

		const auto controller = _controller->parentController();
		const auto entryClicked = [=](
				const Data::CreditsHistoryEntry &e,
				const Data::SubscriptionEntry &s) {
			controller->uiShow()->show(
				Box(ReceiptCreditsBox, controller, e, s));
		};

		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			fullSlice,
			fullWrap->entity(),
			entryClicked,
			self,
			true,
			true,
			true);

		Ui::AddSkip(inner);
		Ui::AddSkip(inner);
		Ui::AddDivider(inner);

		inner->resizeToWidth(container->width());
	};

	const auto apiLifetime = content->lifetime().make_state<rpl::lifetime>();
	{
		using Api = Api::CreditsHistory;
		const auto apiFull = apiLifetime->make_state<Api>(self, true, true);
		apiFull->requestSubscriptions({}, [=](Data::CreditsStatusSlice d) {
			fill(std::move(d));
		});
	}
	{
		using Rebuilder = Data::Session::CreditsSubsRebuilder;
		using RebuilderPtr = std::shared_ptr<Rebuilder>;
		const auto rebuilder = content->lifetime().make_state<RebuilderPtr>(
			self->owner().createCreditsSubsRebuilder());
		rebuilder->get()->events(
		) | rpl::start_with_next([=](Data::CreditsStatusSlice slice) {
			while (content->count()) {
				delete content->widgetAt(0);
			}
			fill(std::move(slice));
		}, content->lifetime());
	}
}

void Credits::setupHistory(not_null<Ui::VerticalLayout*> container) {
	const auto history = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = history->entity();
	const auto self = _controller->session().user();

	Ui::AddSkip(content, st::lineWidth * 6);

	const auto fill = [=](
			not_null<PeerData*> premiumBot,
			const Data::CreditsStatusSlice &fullSlice,
			const Data::CreditsStatusSlice &inSlice,
			const Data::CreditsStatusSlice &outSlice) {
		const auto inner = content;
		if (fullSlice.list.empty()) {
			return;
		}
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
			Ui::AddSubsectionTitle(
				inner,
				tr::lng_credits_summary_history_tab_full(),
				{ 0, 0, 0, -st::defaultSubsectionTitlePadding.bottom() });
		}

		const auto slider = inner->add(
			object_ptr<Ui::SlideWrap<Ui::CustomWidthSlider>>(
				inner,
				object_ptr<Ui::CustomWidthSlider>(
					inner,
					st::creditsHistoryTabsSlider)),
			st::creditsHistoryTabsSliderPadding);
		slider->toggle(!hasOneTab, anim::type::instant);
		if (!hasOneTab) {
			const auto shadow = Ui::CreateChild<Ui::RpWidget>(inner);
			shadow->paintRequest() | rpl::start_with_next([=] {
				auto p = QPainter(shadow);
				p.fillRect(shadow->rect(), st::shadowFg);
			}, shadow->lifetime());
			slider->geometryValue(
			) | rpl::start_with_next([=](const QRect &r) {
				shadow->setGeometry(
					inner->x(),
					rect::bottom(slider) - st::lineWidth,
					inner->width(),
					st::lineWidth);
				shadow->show();
				shadow->raise();
			}, shadow->lifetime());
		}

		slider->entity()->addSection(fullTabText);
		if (hasIn) {
			slider->entity()->addSection(inTabText);
		}
		if (hasOut) {
			slider->entity()->addSection(outTabText);
		}

		{
			const auto &st = st::creditsHistoryTabsSlider;
			slider->entity()->setNaturalWidth(0
				+ st.labelStyle.font->width(fullTabText)
				+ (hasIn ? st.labelStyle.font->width(inTabText) : 0)
				+ (hasOut ? st.labelStyle.font->width(outTabText) : 0)
				+ rect::m::sum::h(st::creditsHistoryTabsSliderPadding));
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

		rpl::single(0) | rpl::then(
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
		}, inner->lifetime());

		const auto controller = _controller->parentController();
		const auto entryClicked = [=](
				const Data::CreditsHistoryEntry &e,
				const Data::SubscriptionEntry &s) {
			controller->uiShow()->show(Box(
				ReceiptCreditsBox,
				controller,
				e,
				s));
		};

		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			fullSlice,
			fullWrap->entity(),
			entryClicked,
			self,
			true,
			true);
		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			inSlice,
			inWrap->entity(),
			entryClicked,
			self,
			true,
			false);
		Info::Statistics::AddCreditsHistoryList(
			controller->uiShow(),
			outSlice,
			outWrap->entity(),
			std::move(entryClicked),
			self,
			false,
			true);

		Ui::AddSkip(inner);
		Ui::AddSkip(inner);

		inner->resizeToWidth(container->width());
	};

	const auto apiLifetime = content->lifetime().make_state<rpl::lifetime>();
	{
		using Api = Api::CreditsHistory;
		const auto c = (_creditsType == CreditsType::Ton);
		const auto apiFull = apiLifetime->make_state<Api>(self, true, true, c);
		const auto apiIn = apiLifetime->make_state<Api>(self, true, false, c);
		const auto apiOut = apiLifetime->make_state<Api>(self, false, true, c);
		apiFull->request({}, [=](Data::CreditsStatusSlice fullSlice) {
			apiIn->request({}, [=](Data::CreditsStatusSlice inSlice) {
				apiOut->request({}, [=](Data::CreditsStatusSlice outSlice) {
					::Api::PremiumPeerBot(
						&_controller->session()
					) | rpl::start_with_next([=](not_null<PeerData*> bot) {
						fill(bot, fullSlice, inSlice, outSlice);
						apiLifetime->destroy();
					}, *apiLifetime);
				});
			});
		});
	}
}

void Credits::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto isCurrency = _creditsType == CreditsType::Ton;
	const auto paid = [=] {
		if (_parent) {
			Ui::StartFireworks(_parent);
		}
	};

	struct State final {
		BuyStarsHandler buyStars;
	};
	const auto state = content->lifetime().make_state<State>();

	const auto paddings = rect::m::sum::h(st::boxRowPadding);
	{
		const auto button = content->add(
			object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
				content,
				object_ptr<Ui::RoundButton>(
					content,
					nullptr,
					st::creditsSettingsBigBalanceButton)),
			st::boxRowPadding)->entity();
		button->setContext([&]() -> Ui::Text::MarkedContext {
			auto customEmojiFactory = [=](const auto &...) {
				const auto &icon = st::settingsIconAdd;
				auto image = QImage(
					(icon.size() + QSize(st::lineWidth * 4, 0))
						* style::DevicePixelRatio(),
					QImage::Format_ARGB32_Premultiplied);
				const auto r = Rect(icon.size()) - Margins(st::lineWidth * 2);
				image.setDevicePixelRatio(style::DevicePixelRatio());
				image.fill(Qt::transparent);
				{
					auto p = QPainter(&image);
					auto hq = PainterHighQualityEnabler(p);
					p.setPen(Qt::NoPen);
					p.setBrush(st::activeButtonFg);
					p.drawEllipse(r);
					icon.paintInCenter(p, r, st::windowBgActive->c);
				}
				return std::make_unique<Ui::Text::StaticCustomEmoji>(
					std::move(image),
					u"topup_button"_q);
			};
			return { .customEmojiFactory = std::move(customEmojiFactory) };
		}());
		button->setText(
			rpl::conditional(
				state->buyStars.loadingValue(),
				rpl::single(TextWithEntities()),
				isCurrency
					? tr::lng_credits_currency_summary_in_button(
						Ui::Text::WithEntities)
					: tr::lng_credits_topup_button(
						lt_emoji,
						rpl::single(Ui::Text::SingleCustomEmoji(u"+"_q)),
						Ui::Text::WithEntities)));
		button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		const auto show = _controller->uiShow();
		if (isCurrency) {
			const auto url = tr::lng_suggest_low_ton_fragment_url(tr::now);
			button->setClickedCallback([=] { UrlClickHandler::Open(url); });
		} else {
			button->setClickedCallback(state->buyStars.handler(show, paid));
		}
		{
			using namespace Info::Statistics;
			const auto loadingAnimation = InfiniteRadialAnimationWidget(
				button,
				button->height() / 2);
			AddChildToWidgetCenter(button, loadingAnimation);
			loadingAnimation->showOn(state->buyStars.loadingValue());
		}
	}

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content, st::lineWidth);

	const auto &textSt = st::creditsPremiumCover.about;
	auto context = [&]() -> Ui::Text::MarkedContext {
		const auto height = textSt.style.font->height;
		auto customEmojiFactory = [=](const auto &...) {
			return std::make_unique<Ui::Text::ShiftedEmoji>(
				isCurrency
					? std::make_unique<Ui::Text::StaticCustomEmoji>(
						Ui::Earn::IconCurrencyColored(
							st::tonFieldIconSize,
							st::windowActiveTextFg->c),
						u"currency_icon:%1"_q.arg(height))
					: Ui::MakeCreditsIconEmoji(height, 1),
				isCurrency
					? QPoint(0, st::lineWidth * 2)
					: QPoint(-st::lineWidth, st::lineWidth));
		};
		return { .customEmojiFactory = std::move(customEmojiFactory) };
	}();
	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_credits_balance_me_count(
					lt_emoji,
					rpl::single(Ui::MakeCreditsIconEntity()),
					lt_amount,
					(isCurrency
						? _controller->session().credits().tonBalanceValue()
						: _controller->session().credits().balanceValue()
					) | rpl::map(
						Lang::FormatCreditsAmountDecimal
					) | rpl::map(Ui::Text::Bold),
					Ui::Text::WithEntities),
				textSt,
				st::defaultPopupMenu,
				std::move(context))));
	if (isCurrency) {
		const auto rate = _controller->session().credits().usdRate();
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<>>(
				content,
				object_ptr<Ui::CenterWrap<>>(
					content,
					object_ptr<Ui::FlatLabel>(
						content,
						_controller->session().credits().tonBalanceValue(
						) | rpl::map([=](CreditsAmount value) {
							using namespace Info::ChannelEarn;
							return value ? ToUsd(value, rate, 3) : QString();
						}),
						st::channelEarnOverviewSubMinorLabel))));
		wrap->toggleOn(_controller->session().credits().tonBalanceValue(
			) | rpl::map(rpl::mappers::_1 > CreditsAmount(0)));
		wrap->finishAnimating();
	}
	Ui::AddSkip(content, st::lineWidth);
	Ui::AddSkip(content, st::lineWidth);
	Ui::AddSkip(content);

	Ui::AddSkip(content);
	if (isCurrency) {
		Ui::AddDividerText(
			content,
			tr::lng_credits_currency_summary_in_subtitle());
	} else {
		Ui::AddDivider(content);
	}
	Ui::AddSkip(content, st::lineWidth * 4);

	const auto controller = _controller->parentController();
	const auto self = _controller->session().user();
	if (!isCurrency) {
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::AbstractButton>>(
				content,
				CreateButtonWithIcon(
					content,
					tr::lng_credits_stats_button(),
					st::settingsCreditsButton,
					{ &st::menuIconStats })));
		wrap->entity()->setClickedCallback([=] {
			controller->showSection(Info::BotEarn::Make(self));
		});
		wrap->toggleOn(_controller->session().credits().loadedValue(
		) | rpl::map([=] {
			return _controller->session().credits().statsEnabled();
		}));
	}
	if (!isCurrency) {
		AddButtonWithIcon(
			content,
			tr::lng_credits_gift_button(),
			st::settingsCreditsButton,
			{ &st::settingsButtonIconGift })->setClickedCallback([=] {
			Ui::ShowGiftCreditsBox(controller, paid);
		});
	}

	if (!isCurrency && Info::BotStarRef::Join::Allowed(self)) {
		AddButtonWithIcon(
			content,
			tr::lng_credits_earn_button(),
			st::settingsCreditsButton,
			{ &st::settingsButtonIconEarn })->setClickedCallback([=] {
			controller->showSection(Info::BotStarRef::Join::Make(self));
		});
	}

	if (!isCurrency) {
		Ui::AddSkip(content, st::lineWidth * 4);
		Ui::AddDivider(content);

		setupSubscriptions(content);
	}
	setupHistory(content);

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> Credits::createPinnedToTop(
		not_null<QWidget*> parent) {
	_parent = parent;
	const auto isCurrency = _creditsType == CreditsType::Ton;

	const auto content = [&]() -> Ui::Premium::TopBarAbstract* {
		const auto weak = base::make_weak(_controller);
		const auto clickContextOther = [=] {
			return QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = weak,
				.botStartAutoSubmit = true,
			});
		};
		return Ui::CreateChild<Ui::Premium::TopBar>(
			parent.get(),
			st::creditsPremiumCover,
			Ui::Premium::TopBarDescriptor{
				.clickContextOther = clickContextOther,
				.title = title(),
				.about = (isCurrency
					? tr::lng_credits_currency_summary_about
					: tr::lng_credits_summary_about)(
						TextWithEntities::Simple),
				.light = true,
				.logo = isCurrency ? u"diamond"_q : QString(),
				.gradientStops = Ui::Premium::CreditsIconGradientStops(),
			});
	}();
	_setPaused = [=](bool paused) {
		content->setPaused(paused);
	};

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		content->setRoundEdges(wrap == Info::Wrap::Layer);
	}, content->lifetime());

	content->setMaximumHeight(st::settingsPremiumTopHeight);
	content->setMinimumHeight(st::infoLayerTopBarHeight);

	content->resize(content->width(), content->maximumHeight());
	content->additionalHeight(
	) | rpl::start_with_next([=](int additionalHeight) {
		const auto wasMax = (content->height() == content->maximumHeight());
		content->setMaximumHeight(st::settingsPremiumTopHeight
			+ additionalHeight);
		if (wasMax) {
			content->resize(content->width(), content->maximumHeight());
		}
	}, content->lifetime());

	{
		const auto balance = AddBalanceWidget(
			content,
			&_controller->session(),
			isCurrency
				? _controller->session().credits().tonBalanceValue()
				: _controller->session().credits().balanceValue(),
			true,
			content->heightValue() | rpl::map([=](int height) {
				const auto ratio = float64(height - content->minimumHeight())
					/ (content->maximumHeight() - content->minimumHeight());
				return (1. - ratio / 0.35);
			}));
		_controller->session().credits().load(true);
		rpl::combine(
			balance->sizeValue(),
			content->sizeValue()
		) | rpl::start_with_next([=](const QSize &, const QSize &) {
			balance->moveToRight(
				(_close
					? _close->width() + st::creditsHistoryRightSkip
					: st::creditsHistoryRightSkip * 2),
				st::creditsHistoryRightSkip);
			balance->update();
		}, balance->lifetime());
	}

	_wrap.value(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			content,
			object_ptr<Ui::IconButton>(
				content,
				(isLayer ? st::infoTopBarBack : st::infoLayerTopBarBack)),
			st::infoTopBarScale);
		_back->setDuration(0);
		_back->toggleOn(isLayer
			? _backToggles.value() | rpl::type_erased()
			: rpl::single(true));
		_back->entity()->addClickHandler([=] {
			_showBack.fire({});
		});
		_back->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			const auto &st = isLayer ? st::infoLayerTopBar : st::infoTopBar;
			content->setTextPosition(
				toggled ? st.back.width : st.titlePosition.x(),
				st.titlePosition.y());
		}, _back->lifetime());

		if (!isLayer) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				content,
				st::infoTopBarClose);
			_close->addClickHandler([=] {
				_controller->parentController()->hideLayer();
				_controller->parentController()->hideSpecialLayer();
			});
			content->widthValue(
			) | rpl::start_with_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, content->lifetime());

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

void Credits::showFinished() {
	_showFinished.fire({});
}

class Currency {
};

} // namespace

template <>
struct SectionFactory<Credits> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Credits>(parent, controller, CreditsType::Stars);
	}
	bool hasCustomTopBar() const final override {
		return true;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

template <>
struct SectionFactory<Currency> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Credits>(parent, controller, CreditsType::Ton);
	}
	bool hasCustomTopBar() const final override {
		return true;
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
};

Type CreditsId() {
	return Credits::Id();
}

Type CurrencyId() {
	return SectionFactory<Currency>::Instance();
}

BuyStarsHandler::BuyStarsHandler() = default;

BuyStarsHandler::~BuyStarsHandler() = default;

Fn<void()> BuyStarsHandler::handler(
		std::shared_ptr<::Main::SessionShow> show,
		Fn<void()> paid) {
	const auto optionsBox = [=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::giveawayGiftCodeBox);
		box->setWidth(st::boxWideWidth);
		box->setTitle(tr::lng_credits_summary_options_subtitle());
		const auto inner = box->verticalLayout();
		const auto self = show->session().user();
		const auto options = _api
			? _api->options()
			: Data::CreditTopupOptions();
		const auto amount = CreditsAmount();
		const auto weak = Ui::MakeWeak(box);
		FillCreditOptions(show, inner, self, amount, [=] {
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
			if (const auto onstack = paid) {
				onstack();
			}
		}, nullptr, options);

		const auto button = box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		const auto buttonWidth = st::boxWideWidth
			- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);
		button->widthValue() | rpl::filter([=] {
			return (button->widthNoMargins() != buttonWidth);
		}) | rpl::start_with_next([=] {
			button->resizeToWidth(buttonWidth);
		}, button->lifetime());
	};
	return crl::guard(this, [=] {
		if (_api && !_api->options().empty()) {
			_loading = false;
			show->show(Box(crl::guard(this, optionsBox)));
		} else {
			_loading = true;
			const auto user = show->session().user();
			_api = std::make_unique<Api::CreditsTopupOptions>(user);
			_api->request(
			) | rpl::start_with_error_done([=](const QString &error) {
				_loading = false;
				show->showToast(error);
			}, [=] {
				_loading = false;
				show->show(Box(crl::guard(this, optionsBox)));
			}, _lifetime);
		}
	});
}

rpl::producer<bool> BuyStarsHandler::loadingValue() const {
	return _loading.value();
}

} // namespace Settings

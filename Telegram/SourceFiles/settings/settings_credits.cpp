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
#include "core/click_handler_types.h"
#include "data/components/credits.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "info/bot/starref/info_bot_starref_join_widget.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
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
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

namespace Settings {
namespace {

class Credits : public Section<Credits> {
public:
	Credits(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

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
	void setupStarRefPromo(not_null<Ui::VerticalLayout*> container);
	const not_null<Window::SessionController*> _controller;

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
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _star(Ui::GenerateStars(st::creditsTopupButton.height, 1))
, _balanceStar(Ui::GenerateStars(st::creditsBalanceStarHeight, 1)) {
	setupContent();

	_controller->session().premiumPossibleValue(
	) | rpl::start_with_next([=](bool premiumPossible) {
		if (!premiumPossible) {
			_showBack.fire({});
		}
	}, lifetime());
}

rpl::producer<QString> Credits::title() {
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

void Credits::setupStarRefPromo(not_null<Ui::VerticalLayout*> container) {
	const auto self = _controller->session().user();
	if (!Info::BotStarRef::Join::Allowed(self)) {
		return;
	}
	Ui::AddSkip(container);
	const auto button = Info::BotStarRef::AddViewListButton(
		container,
		tr::lng_credits_summary_earn_title(),
		tr::lng_credits_summary_earn_about(),
		true);
	button->setClickedCallback([=] {
		_controller->showSection(Info::BotStarRef::Join::Make(self));
	});
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
}

void Credits::setupHistory(not_null<Ui::VerticalLayout*> container) {
	const auto history = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto content = history->entity();
	const auto self = _controller->session().user();

	Ui::AddSkip(content);

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
		const auto apiFull = apiLifetime->make_state<Api>(self, true, true);
		const auto apiIn = apiLifetime->make_state<Api>(self, true, false);
		const auto apiOut = apiLifetime->make_state<Api>(self, false, true);
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
	const auto paid = [=] {
		if (_parent) {
			Ui::StartFireworks(_parent);
		}
	};
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	const auto balanceLine = content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::RpWidget>(content)))->entity();
	const auto balanceIcon = CreateSingleStarWidget(
		balanceLine,
		st::creditsSettingsBigBalance.style.font->height);
	const auto balanceAmount = Ui::CreateChild<Ui::FlatLabel>(
		balanceLine,
		_controller->session().credits().balanceValue(
		) | rpl::map(Lang::FormatStarsAmountDecimal),
		st::creditsSettingsBigBalance);
	balanceAmount->sizeValue() | rpl::start_with_next([=] {
		balanceLine->resize(
			balanceIcon->width()
				+ st::creditsSettingsBigBalanceSkip
				+ balanceAmount->textMaxWidth(),
			balanceIcon->height());
	}, balanceLine->lifetime());
	balanceLine->widthValue() | rpl::start_with_next([=] {
		balanceAmount->moveToRight(0, 0);
	}, balanceLine->lifetime());
	Ui::AddSkip(content);
	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_credits_balance_me(),
				st::infoTopBar.subtitle)));
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	struct State final {
		rpl::variable<bool> confirmButtonBusy = false;
		std::optional<Api::CreditsTopupOptions> api;
	};
	const auto state = content->lifetime().make_state<State>();

	const auto button = content->add(
		object_ptr<Ui::RoundButton>(
			content,
			rpl::conditional(
				state->confirmButtonBusy.value(),
				rpl::single(QString()),
				tr::lng_credits_buy_button()),
			st::creditsSettingsBigBalanceButton),
		st::boxRowPadding);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	const auto show = _controller->uiShow();
	const auto optionsBox = [=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::giveawayGiftCodeBox);
		box->setWidth(st::boxWideWidth);
		box->setTitle(tr::lng_credits_summary_options_subtitle());
		const auto inner = box->verticalLayout();
		const auto self = show->session().user();
		const auto options = state->api
			? state->api->options()
			: Data::CreditTopupOptions();
		const auto amount = StarsAmount();
		FillCreditOptions(show, inner, self, amount, paid, nullptr, options);

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
	button->setClickedCallback([=] {
		if (state->api && !state->api->options().empty()) {
			state->confirmButtonBusy = false;
			show->show(Box(optionsBox));
		} else {
			state->confirmButtonBusy = true;
			state->api.emplace(show->session().user());
			state->api->request(
			) | rpl::start_with_error_done([=](const QString &error) {
				state->confirmButtonBusy = false;
				show->showToast(error);
			}, [=] {
				state->confirmButtonBusy = false;
				show->show(Box(optionsBox));
			}, content->lifetime());
		}
	});
	{
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			button,
			button->height() / 2);
		AddChildToWidgetCenter(button, loadingAnimation);
		loadingAnimation->showOn(state->confirmButtonBusy.value());
	}
	const auto paddings = rect::m::sum::h(st::boxRowPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != (content->width() - paddings));
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(content->width() - paddings);
	}, button->lifetime());

	Ui::AddSkip(content);

	{
		const auto &giftSt = st::creditsSettingsBigBalanceButtonGift;
		const auto giftDelay = giftSt.ripple.hideDuration * 2;
		const auto fakeLoading
			= content->lifetime().make_state<rpl::variable<bool>>(false);
		const auto gift = content->add(
			object_ptr<Ui::RoundButton>(
				content,
				rpl::conditional(
					fakeLoading->value(),
					rpl::single(QString()),
					tr::lng_credits_gift_button()),
				giftSt),
			st::boxRowPadding);
		gift->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		gift->setClickedCallback([=, controller = _controller] {
			if (fakeLoading->current()) {
				return;
			}
			*fakeLoading = true;
			base::call_delayed(giftDelay, crl::guard(gift, [=] {
				*fakeLoading = false;
				Ui::ShowGiftCreditsBox(controller, paid);
			}));
		});
		{
			using namespace Info::Statistics;
			const auto loadingAnimation = InfiniteRadialAnimationWidget(
				gift,
				gift->height() / 2,
				&st::editStickerSetNameLoading);
			AddChildToWidgetCenter(gift, loadingAnimation);
			loadingAnimation->showOn(fakeLoading->value());
		}
		gift->widthValue() | rpl::filter([=] {
			return (gift->widthNoMargins() != (content->width() - paddings));
		}) | rpl::start_with_next([=] {
			gift->resizeToWidth(content->width() - paddings);
		}, gift->lifetime());
	}

	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddDivider(content);

	setupStarRefPromo(content);
	setupSubscriptions(content);
	setupHistory(content);

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> Credits::createPinnedToTop(
		not_null<QWidget*> parent) {
	_parent = parent;

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
				.title = tr::lng_credits_summary_title(),
				.about = tr::lng_credits_summary_about(
					TextWithEntities::Simple),
				.light = true,
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
			_controller->session().credits().balanceValue(),
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

} // namespace

template <>
struct SectionFactory<Credits> : AbstractSectionFactory {
	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<Credits>(parent, controller);
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

} // namespace Settings

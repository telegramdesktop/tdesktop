/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/create_giveaway_box.h"

#include "api/api_premium.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "countries/countries_instance.h"
#include "data/data_peer.h"
#include "info/boosts/giveaway/boost_badge.h"
#include "info/boosts/giveaway/giveaway_list_controllers.h"
#include "info/boosts/giveaway/giveaway_type_row.h"
#include "info/boosts/giveaway/select_countries_box.h"
#include "info/boosts/info_boosts_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "payments/payments_checkout_process.h" // Payments::CheckoutProcess
#include "payments/payments_form.h" // Payments::InvoicePremiumGiftCode
#include "settings/settings_common.h"
#include "settings/settings_premium.h" // Settings::ShowPremium
#include "ui/boxes/choose_date_time.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kDoneTooltipDuration = 5 * crl::time(1000);

[[nodiscard]] QDateTime ThreeDaysAfterToday() {
	auto dateNow = QDateTime::currentDateTime();
	dateNow = dateNow.addDays(3);
	auto timeNow = dateNow.time();
	while (timeNow.minute() % 5) {
		timeNow = timeNow.addSecs(60);
	}
	dateNow.setTime(timeNow);
	return dateNow;
}

[[nodiscard]] Fn<bool(int)> CreateErrorCallback(
		int max,
		tr::phrase<lngtag_count> phrase) {
	return [=](int count) {
		const auto error = (count >= max);
		if (error) {
			Ui::Toast::Show(phrase(tr::now, lt_count, max));
		}
		return error;
	};
}

[[nodiscard]] QWidget *FindFirstShadowInBox(not_null<Ui::BoxContent*> box) {
	for (const auto &child : box->children()) {
		if (child && child->isWidgetType()) {
			const auto w = static_cast<QWidget*>(child);
			if (w->height() == st::lineWidth) {
				return w;
			}
		}
	}
	return nullptr;
}

void AddPremiumTopBarWithDefaultTitleBar(
		not_null<Ui::GenericBox*> box,
		rpl::producer<> showFinished,
		rpl::producer<QString> titleText) {
	struct State final {
		Ui::Animations::Simple animation;
		Ui::Text::String title;

		Ui::RpWidget close;
	};
	const auto state = box->lifetime().make_state<State>();
	box->setNoContentMargin(true);

	std::move(
		titleText
	) | rpl::start_with_next([=](const QString &s) {
		state->title.setText(st::startGiveawayBox.title.style, s);
	}, box->lifetime());

	const auto hPadding = rect::m::sum::h(st::boxRowPadding);
	const auto titlePaintContext = Ui::Text::PaintContext{
		.position = st::boxTitlePosition,
		.outerWidth = (st::boxWideWidth - hPadding),
		.availableWidth = (st::boxWideWidth - hPadding),
	};

	const auto isCloseBarShown = [=] { return box->scrollTop() > 0; };

	const auto closeTopBar = box->setPinnedToTopContent(
		object_ptr<Ui::RpWidget>(box));
	closeTopBar->resize(box->width(), st::boxTitleHeight);
	closeTopBar->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = Painter(closeTopBar);
		const auto radius = st::boxRadius;
		const auto progress = state->animation.value(isCloseBarShown()
			? 1.
			: 0.);
		const auto resultRect = r + QMargins{ 0, 0, 0, radius };
		{
			auto hq = PainterHighQualityEnabler(p);

			if (progress < 1.) {
				auto path = QPainterPath();
				path.addRect(resultRect);
				path.addRect(
					st::boxRowPadding.left(),
					0,
					resultRect.width() - hPadding,
					resultRect.height());
				p.setClipPath(path);
				PainterHighQualityEnabler hq(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::boxDividerBg);
				p.drawRoundedRect(resultRect, radius, radius);
			}
			if (progress > 0.) {
				p.setOpacity(progress);

				p.setClipping(false);
				p.setPen(Qt::NoPen);
				p.setBrush(st::boxBg);
				p.drawRoundedRect(resultRect, radius, radius);

				p.setPen(st::startGiveawayBox.title.textFg);
				p.setBrush(Qt::NoBrush);
				state->title.draw(p, titlePaintContext);
			}
		}
	}, closeTopBar->lifetime());

	{
		const auto close = Ui::CreateChild<Ui::IconButton>(
			closeTopBar.get(),
			st::startGiveawayBoxTitleClose);
		close->setClickedCallback([=] { box->closeBox(); });
		closeTopBar->widthValue(
		) | rpl::start_with_next([=](int w) {
			const auto &pos = st::giveawayGiftCodeCoverClosePosition;
			close->moveToRight(pos.x(), pos.y());
		}, box->lifetime());
		close->show();
	}

	const auto bar = Ui::CreateChild<Ui::Premium::TopBar>(
		box.get(),
		st::startGiveawayCover,
		nullptr,
		tr::lng_giveaway_new_title(),
		tr::lng_giveaway_new_about(Ui::Text::RichLangValue),
		true,
		false);
	bar->setAttribute(Qt::WA_TransparentForMouseEvents);

	box->addRow(
		object_ptr<Ui::BoxContentDivider>(
			box.get(),
			st::giveawayGiftCodeTopHeight
				- st::boxTitleHeight
				+ st::boxDividerHeight
				+ st::settingsSectionSkip,
			st::boxDividerBg,
			RectPart::Bottom),
		{});
	bar->setPaused(true);
	bar->setRoundEdges(false);
	bar->setMaximumHeight(st::giveawayGiftCodeTopHeight);
	bar->setMinimumHeight(st::infoLayerTopBarHeight);
	bar->resize(bar->width(), bar->maximumHeight());
	box->widthValue(
	) | rpl::start_with_next([=](int w) {
		bar->resizeToWidth(w - hPadding);
		bar->moveToLeft(st::boxRowPadding.left(), bar->y());
	}, box->lifetime());

	std::move(
		showFinished
	) | rpl::take(1) | rpl::start_with_next([=] {
		closeTopBar->raise();
		if (const auto shadow = FindFirstShadowInBox(box)) {
			bar->stackUnder(shadow);
		}
		bar->setPaused(false);
		box->scrolls(
		) | rpl::map(isCloseBarShown) | rpl::distinct_until_changed(
		) | rpl::start_with_next([=](bool showBar) {
			state->animation.stop();
			state->animation.start(
				[=] { closeTopBar->update(); },
				showBar ? 0. : 1.,
				showBar ? 1. : 0.,
				st::slideWrapDuration);
		}, box->lifetime());
		box->scrolls(
		) | rpl::start_with_next([=] {
			bar->moveToLeft(bar->x(), -box->scrollTop());
		}, box->lifetime());
	}, box->lifetime());

	bar->show();
}

} // namespace

void CreateGiveawayBox(
		not_null<Ui::GenericBox*> box,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer,
		Fn<void()> reloadOnDone,
		std::optional<Data::BoostPrepaidGiveaway> prepaid) {
	box->setWidth(st::boxWideWidth);

	const auto weakWindow = base::make_weak(controller->parentController());

	using GiveawayType = Giveaway::GiveawayTypeRow::Type;
	using GiveawayGroup = Ui::RadioenumGroup<GiveawayType>;
	struct State final {
		State(not_null<PeerData*> p) : apiOptions(p) {
		}

		Api::PremiumGiftCodeOptions apiOptions;
		rpl::lifetime lifetimeApi;

		std::vector<not_null<PeerData*>> selectedToAward;
		rpl::event_stream<> toAwardAmountChanged;

		std::vector<not_null<PeerData*>> selectedToSubscribe;

		rpl::variable<GiveawayType> typeValue;
		rpl::variable<int> sliderValue;
		rpl::variable<TimeId> dateValue;
		rpl::variable<std::vector<QString>> countriesValue;

		rpl::variable<bool> confirmButtonBusy = true;
	};
	const auto state = box->lifetime().make_state<State>(peer);
	const auto typeGroup = std::make_shared<GiveawayGroup>();

	auto showFinished = Ui::BoxShowFinishes(box);
	AddPremiumTopBarWithDefaultTitleBar(
		box,
		rpl::duplicate(showFinished),
		rpl::conditional(
			state->typeValue.value(
			) | rpl::map(rpl::mappers::_1 == GiveawayType::Random),
			tr::lng_giveaway_start(),
			tr::lng_giveaway_award()));
	{
		const auto &padding = st::giveawayGiftCodeCoverDividerPadding;
		Settings::AddSkip(box->verticalLayout(), padding.bottom());
	}

	const auto loading = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
	{
		loading->toggle(true, anim::type::instant);
		const auto container = loading->entity();
		Settings::AddSkip(container);
		Settings::AddSkip(container);
		container->add(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_contacts_loading(),
					st::giveawayLoadingLabel)));
		Settings::AddSkip(container);
		Settings::AddSkip(container);
	}
	const auto contentWrap = box->verticalLayout()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
	contentWrap->toggle(false, anim::type::instant);

	if (prepaid) {
		contentWrap->entity()->add(
			object_ptr<Giveaway::GiveawayTypeRow>(
				box,
				GiveawayType::Prepaid,
				prepaid->id,
				tr::lng_boosts_prepaid_giveaway_single(),
				tr::lng_boosts_prepaid_giveaway_status(
					lt_count,
					rpl::single(prepaid->quantity) | tr::to_count(),
					lt_duration,
					tr::lng_premium_gift_duration_months(
						lt_count,
						rpl::single(prepaid->months) | tr::to_count())),
				QImage())
		)->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (!prepaid) {
		const auto row = contentWrap->entity()->add(
			object_ptr<Giveaway::GiveawayTypeRow>(
				box,
				GiveawayType::Random,
				tr::lng_giveaway_create_subtitle()));
		row->addRadio(typeGroup);
		row->setClickedCallback([=] {
			state->typeValue.force_assign(GiveawayType::Random);
		});
	}
	if (!prepaid) {
		const auto row = contentWrap->entity()->add(
			object_ptr<Giveaway::GiveawayTypeRow>(
				box,
				GiveawayType::SpecificUsers,
				state->toAwardAmountChanged.events_starting_with(
					rpl::empty_value()
				) | rpl::map([=] {
					const auto &selected = state->selectedToAward;
					return selected.empty()
						? tr::lng_giveaway_award_subtitle()
						: (selected.size() == 1)
						? rpl::single(selected.front()->name())
						: tr::lng_giveaway_award_chosen(
							lt_count,
							rpl::single(selected.size()) | tr::to_count());
				}) | rpl::flatten_latest()));
		row->addRadio(typeGroup);
		row->setClickedCallback([=] {
			auto initBox = [=](not_null<PeerListBox*> peersBox) {
				peersBox->setTitle(tr::lng_giveaway_award_option());
				peersBox->addButton(tr::lng_settings_save(), [=] {
					state->selectedToAward = peersBox->collectSelectedRows();
					state->toAwardAmountChanged.fire({});
					peersBox->closeBox();
				});
				peersBox->addButton(tr::lng_cancel(), [=] {
					peersBox->closeBox();
				});
				peersBox->boxClosing(
				) | rpl::start_with_next([=] {
					state->typeValue.force_assign(
						state->selectedToAward.empty()
							? GiveawayType::Random
							: GiveawayType::SpecificUsers);
				}, peersBox->lifetime());
			};

			using Controller = Giveaway::AwardMembersListController;
			auto listController = std::make_unique<Controller>(
				controller,
				peer,
				state->selectedToAward);
			listController->setCheckError(CreateErrorCallback(
				state->apiOptions.giveawayAddPeersMax(),
				tr::lng_giveaway_maximum_users_error));
			box->uiShow()->showBox(
				Box<PeerListBox>(
					std::move(listController),
					std::move(initBox)),
				Ui::LayerOption::KeepOther);
		});
	}

	{
		const auto &padding = st::giveawayGiftCodeTypeDividerPadding;
		Settings::AddSkip(contentWrap->entity(), padding.top());
		Settings::AddDivider(contentWrap->entity());
		Settings::AddSkip(contentWrap->entity(), padding.bottom());
	}

	const auto randomWrap = contentWrap->entity()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			contentWrap,
			object_ptr<Ui::VerticalLayout>(box)));
	state->typeValue.value(
	) | rpl::start_with_next([=](GiveawayType type) {
		randomWrap->toggle(type == GiveawayType::Random, anim::type::instant);
	}, randomWrap->lifetime());

	randomWrap->toggleOn(
		state->typeValue.value(
		) | rpl::map(rpl::mappers::_1 == GiveawayType::Random),
		anim::type::instant);

	const auto sliderContainer = randomWrap->entity()->add(
		object_ptr<Ui::VerticalLayout>(randomWrap));
	const auto fillSliderContainer = [=] {
		const auto availablePresets = state->apiOptions.availablePresets();
		if (prepaid) {
			state->sliderValue = prepaid->quantity;
			return;
		}
		if (availablePresets.empty()) {
			return;
		}
		state->sliderValue = availablePresets.front();
		const auto title = Settings::AddSubsectionTitle(
			sliderContainer,
			tr::lng_giveaway_quantity_title());
		const auto rightLabel = Ui::CreateChild<Ui::FlatLabel>(
			sliderContainer,
			st::giveawayGiftCodeQuantitySubtitle);
		rightLabel->show();

		const auto floatLabel = Ui::CreateChild<Ui::FlatLabel>(
			sliderContainer,
			st::giveawayGiftCodeQuantityFloat);
		floatLabel->show();

		rpl::combine(
			tr::lng_giveaway_quantity(
				lt_count,
				state->sliderValue.value(
				) | rpl::map([=](int v) -> float64 {
					return state->apiOptions.giveawayBoostsPerPremium() * v;
				})),
			title->positionValue(),
			sliderContainer->geometryValue()
		) | rpl::start_with_next([=](QString s, const QPoint &p, QRect) {
			rightLabel->setText(std::move(s));
			rightLabel->moveToRight(st::boxRowPadding.right(), p.y());
		}, rightLabel->lifetime());

		const auto &padding = st::giveawayGiftCodeSliderPadding;
		Settings::AddSkip(sliderContainer, padding.top());

		class Slider : public Ui::MediaSlider {
		public:
			using Ui::MediaSlider::MediaSlider;

		protected:
			void wheelEvent(QWheelEvent *e) override {
				e->ignore();
			}

		};

		const auto slider = sliderContainer->add(
			object_ptr<Slider>(sliderContainer, st::settingsScale),
			st::boxRowPadding);
		Settings::AddSkip(sliderContainer, padding.bottom());
		slider->resize(slider->width(), st::settingsScale.seekSize.height());
		slider->setPseudoDiscrete(
			availablePresets.size(),
			[=](int index) { return availablePresets[index]; },
			availablePresets.front(),
			[=](int boosts) { state->sliderValue = boosts; },
			[](int) {});

		state->sliderValue.value(
		) | rpl::start_with_next([=](int boosts) {
			floatLabel->setText(QString::number(boosts));

			const auto count = availablePresets.size();
			const auto sliderWidth = slider->width()
				- st::settingsScale.seekSize.width();
			for (auto i = 0; i < count; i++) {
				if ((i + 1 == count || availablePresets[i + 1] > boosts)
					&& availablePresets[i] <= boosts) {
					const auto x = (sliderWidth * i) / (count - 1);
					floatLabel->moveToLeft(
						slider->x()
							+ x
							+ st::settingsScale.seekSize.width() / 2
							- floatLabel->width() / 2,
						slider->y()
							- floatLabel->height()
							- st::giveawayGiftCodeSliderFloatSkip);
					break;
				}
			}
		}, floatLabel->lifetime());

		Settings::AddSkip(sliderContainer);
		Settings::AddDividerText(
			sliderContainer,
			tr::lng_giveaway_quantity_about());
		Settings::AddSkip(sliderContainer);

		sliderContainer->resizeToWidth(box->width());
	};

	{
		const auto channelsContainer = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		Settings::AddSubsectionTitle(
			channelsContainer,
			tr::lng_giveaway_channels_title(),
			st::giveawayGiftCodeChannelsSubsectionPadding);

		struct ListState final {
			ListState(not_null<PeerData*> p) : controller(p) {
			}
			PeerListContentDelegateSimple delegate;
			Giveaway::SelectedChannelsListController controller;
		};
		const auto listState = box->lifetime().make_state<ListState>(peer);

		listState->delegate.setContent(channelsContainer->add(
			object_ptr<PeerListContent>(
				channelsContainer,
				&listState->controller)));
		listState->controller.setDelegate(&listState->delegate);
		listState->controller.channelRemoved(
		) | rpl::start_with_next([=](not_null<PeerData*> peer) {
			auto &list = state->selectedToSubscribe;
			list.erase(ranges::remove(list, peer), end(list));
		}, box->lifetime());
		listState->controller.setTopStatus(tr::lng_giveaway_channels_this(
			lt_count,
			state->sliderValue.value(
			) | rpl::map([=](int v) -> float64 {
				return state->apiOptions.giveawayBoostsPerPremium() * v;
			})));

		using IconType = Settings::IconType;
		Settings::AddButton(
			channelsContainer,
			tr::lng_giveaway_channels_add(),
			st::giveawayGiftCodeChannelsAddButton,
			{ &st::settingsIconAdd, IconType::Round, &st::windowBgActive }
		)->setClickedCallback([=] {
			auto initBox = [=](not_null<PeerListBox*> peersBox) {
				peersBox->setTitle(tr::lng_giveaway_channels_add());
				peersBox->addButton(tr::lng_settings_save(), [=] {
					const auto selected = peersBox->collectSelectedRows();
					state->selectedToSubscribe = selected;
					listState->controller.rebuild(selected);
					peersBox->closeBox();
				});
				peersBox->addButton(tr::lng_cancel(), [=] {
					peersBox->closeBox();
				});
			};

			using Controller = Giveaway::MyChannelsListController;
			auto controller = std::make_unique<Controller>(
				peer,
				box->uiShow(),
				state->selectedToSubscribe);
			controller->setCheckError(CreateErrorCallback(
				state->apiOptions.giveawayAddPeersMax(),
				tr::lng_giveaway_maximum_channels_error));
			box->uiShow()->showBox(
				Box<PeerListBox>(std::move(controller), std::move(initBox)),
				Ui::LayerOption::KeepOther);
		});

		const auto &padding = st::giveawayGiftCodeChannelsDividerPadding;
		Settings::AddSkip(channelsContainer, padding.top());
		Settings::AddDividerText(
			channelsContainer,
			tr::lng_giveaway_channels_about());
		Settings::AddSkip(channelsContainer, padding.bottom());
	}

	const auto membersGroup = std::make_shared<GiveawayGroup>();
	{
		const auto countriesContainer = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		Settings::AddSubsectionTitle(
			countriesContainer,
			tr::lng_giveaway_users_title());

		membersGroup->setValue(GiveawayType::AllMembers);
		auto subtitle = state->countriesValue.value(
		) | rpl::map([=](const std::vector<QString> &list) {
			return list.empty()
				? tr::lng_giveaway_users_from_all_countries()
				: (list.size() == 1)
				? tr::lng_giveaway_users_from_one_country(
					lt_country,
					rpl::single(Countries::Instance().countryNameByISO2(
						list.front())))
				: tr::lng_giveaway_users_from_countries(
					lt_count,
					rpl::single(list.size()) | tr::to_count());
		}) | rpl::flatten_latest();

		const auto showBox = [=] {
			auto done = [=](std::vector<QString> list) {
				state->countriesValue = std::move(list);
			};
			auto error = CreateErrorCallback(
				state->apiOptions.giveawayCountriesMax(),
				tr::lng_giveaway_maximum_countries_error);
			box->uiShow()->showBox(Box(
				Ui::SelectCountriesBox,
				state->countriesValue.current(),
				std::move(done),
				std::move(error)));
		};

		const auto createCallback = [=](GiveawayType type) {
			return [=] {
				const auto was = membersGroup->value();
				membersGroup->setValue(type);
				const auto now = membersGroup->value();
				if (was == now) {
					base::call_delayed(
						st::defaultRippleAnimation.hideDuration,
						box,
						showBox);
				}
			};
		};

		{
			const auto row = countriesContainer->add(
				object_ptr<Giveaway::GiveawayTypeRow>(
					box,
					GiveawayType::AllMembers,
					rpl::duplicate(subtitle)));
			row->addRadio(membersGroup);
			row->setClickedCallback(createCallback(GiveawayType::AllMembers));
		}
		const auto row = countriesContainer->add(
			object_ptr<Giveaway::GiveawayTypeRow>(
				box,
				GiveawayType::OnlyNewMembers,
				std::move(subtitle)));
		row->addRadio(membersGroup);
		row->setClickedCallback(createCallback(GiveawayType::OnlyNewMembers));

		Settings::AddSkip(countriesContainer);
		Settings::AddDividerText(
			countriesContainer,
			tr::lng_giveaway_users_about());
		Settings::AddSkip(countriesContainer);
	}

	const auto addTerms = [=](not_null<Ui::VerticalLayout*> c) {
		auto terms = object_ptr<Ui::FlatLabel>(
			c,
			tr::lng_premium_gift_terms(
				lt_link,
				tr::lng_premium_gift_terms_link(
				) | rpl::map([](const QString &t) {
					return Ui::Text::Link(t, 1);
				}),
				Ui::Text::WithEntities),
			st::boxDividerLabel);
		terms->setLink(1, std::make_shared<LambdaClickHandler>([=] {
			box->closeBox();
			Settings::ShowPremium(&peer->session(), QString());
		}));
		c->add(std::move(terms));
	};

	{
		const auto dateContainer = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		Settings::AddSubsectionTitle(
			dateContainer,
			tr::lng_giveaway_date_title(),
			st::giveawayGiftCodeChannelsSubsectionPadding);

		state->dateValue = ThreeDaysAfterToday().toSecsSinceEpoch();
		const auto button = Settings::AddButtonWithLabel(
			dateContainer,
			tr::lng_giveaway_date(),
			state->dateValue.value() | rpl::map(
				base::unixtime::parse
			) | rpl::map(Ui::FormatDateTime),
			st::defaultSettingsButton);

		button->setClickedCallback([=] {
			box->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> b) {
				Ui::ChooseDateTimeBox(b, {
					.title = tr::lng_giveaway_date_select(),
					.submit = tr::lng_settings_save(),
					.done = [=](TimeId time) {
						state->dateValue = time;
						b->closeBox();
					},
					.min = QDateTime::currentSecsSinceEpoch,
					.time = state->dateValue.current(),
					.max = [=] {
						return QDateTime::currentSecsSinceEpoch()
							+ state->apiOptions.giveawayPeriodMax();;
					},
				});
			}));
		});

		Settings::AddSkip(dateContainer);
		if (prepaid) {
			auto terms = object_ptr<Ui::VerticalLayout>(dateContainer);
			terms->add(object_ptr<Ui::FlatLabel>(
				terms,
				tr::lng_giveaway_date_about(
					lt_count,
					state->sliderValue.value() | tr::to_count()),
				st::boxDividerLabel));
			Settings::AddSkip(terms.data());
			Settings::AddSkip(terms.data());
			addTerms(terms.data());
			dateContainer->add(object_ptr<Ui::DividerLabel>(
				dateContainer,
				std::move(terms),
				st::settingsDividerLabelPadding));
		} else {
			Settings::AddDividerText(
				dateContainer,
				tr::lng_giveaway_date_about(
					lt_count,
					state->sliderValue.value() | tr::to_count()));
			Settings::AddSkip(dateContainer);
		}
	}

	const auto durationGroup = std::make_shared<Ui::RadiobuttonGroup>(0);
	const auto listOptions = contentWrap->entity()->add(
		object_ptr<Ui::VerticalLayout>(box));
	const auto rebuildListOptions = [=](int amountUsers) {
		if (prepaid) {
			return;
		}
		while (listOptions->count()) {
			delete listOptions->widgetAt(0);
		}
		Settings::AddSubsectionTitle(
			listOptions,
			tr::lng_giveaway_duration_title(
				lt_count,
				rpl::single(amountUsers) | tr::to_count()),
			st::giveawayGiftCodeChannelsSubsectionPadding);
		Ui::Premium::AddGiftOptions(
			listOptions,
			durationGroup,
			state->apiOptions.options(amountUsers),
			st::giveawayGiftCodeGiftOption,
			true);

		Settings::AddSkip(listOptions);

		auto termsContainer = object_ptr<Ui::VerticalLayout>(listOptions);
		addTerms(termsContainer.data());
		listOptions->add(object_ptr<Ui::DividerLabel>(
			listOptions,
			std::move(termsContainer),
			st::settingsDividerLabelPadding));

		box->verticalLayout()->resizeToWidth(box->width());
	};
	if (!prepaid) {
		rpl::combine(
			state->sliderValue.value(),
			state->typeValue.value()
		) | rpl::start_with_next([=](int users, GiveawayType type) {
			typeGroup->setValue(type);
			rebuildListOptions((type == GiveawayType::SpecificUsers)
				? state->selectedToAward.size()
				: users);
		}, box->lifetime());
	} else {
		typeGroup->setValue(GiveawayType::Random);
	}
	{
		using namespace Info::Statistics;
		const auto &stButton = st::startGiveawayBox;
		box->setStyle(stButton);
		auto button = object_ptr<Ui::RoundButton>(
			box,
			rpl::never<QString>(),
			st::giveawayGiftCodeStartButton);

		AddLabelWithBadgeToButton(
			button,
			rpl::conditional(
				state->typeValue.value(
				) | rpl::map(rpl::mappers::_1 == GiveawayType::Random),
				tr::lng_giveaway_start(),
				tr::lng_giveaway_award()),
			state->sliderValue.value(
			) | rpl::map([=](int v) -> int {
				return state->apiOptions.giveawayBoostsPerPremium() * v;
			}),
			state->confirmButtonBusy.value() | rpl::map(!rpl::mappers::_1));

		{
			const auto loadingAnimation = InfiniteRadialAnimationWidget(
				button,
				st::giveawayGiftCodeStartButton.height / 2);
			button->sizeValue(
			) | rpl::start_with_next([=](const QSize &s) {
				const auto size = loadingAnimation->size();
				loadingAnimation->moveToLeft(
					(s.width() - size.width()) / 2,
					(s.height() - size.height()) / 2);
			}, loadingAnimation->lifetime());
			loadingAnimation->showOn(state->confirmButtonBusy.value());
		}

		button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		state->typeValue.value(
		) | rpl::start_with_next([=, raw = button.data()] {
			raw->resizeToWidth(box->width()
				- stButton.buttonPadding.left()
				- stButton.buttonPadding.right());
		}, button->lifetime());
		button->setClickedCallback([=] {
			if (state->confirmButtonBusy.current()) {
				return;
			}
			const auto type = typeGroup->value();
			const auto isSpecific = (type == GiveawayType::SpecificUsers);
			const auto isRandom = (type == GiveawayType::Random);
			if (!isSpecific && !isRandom) {
				return;
			}
			auto invoice = state->apiOptions.invoice(
				isSpecific
					? state->selectedToAward.size()
					: state->sliderValue.current(),
				prepaid
					? prepaid->months
					: state->apiOptions.monthsFromPreset(
						durationGroup->value()));
			if (isSpecific) {
				if (state->selectedToAward.empty()) {
					return;
				}
				invoice.purpose = Payments::InvoicePremiumGiftCodeUsers{
					ranges::views::all(
						state->selectedToAward
					) | ranges::views::transform([](
							const not_null<PeerData*> p) {
						return not_null{ p->asUser() };
					}) | ranges::to_vector,
					peer->asChannel(),
				};
			} else if (isRandom) {
				invoice.purpose = Payments::InvoicePremiumGiftCodeGiveaway{
					.boostPeer = peer->asChannel(),
					.additionalChannels = ranges::views::all(
						state->selectedToSubscribe
					) | ranges::views::transform([](
							const not_null<PeerData*> p) {
						return not_null{ p->asChannel() };
					}) | ranges::to_vector,
					.countries = state->countriesValue.current(),
					.untilDate = state->dateValue.current(),
					.onlyNewSubscribers = (membersGroup->value()
						== GiveawayType::OnlyNewMembers),
				};
			}
			state->confirmButtonBusy = true;
			const auto show = box->uiShow();
			const auto weak = Ui::MakeWeak(box.get());
			const auto done = [=](Payments::CheckoutResult result) {
				const auto isPaid = result == Payments::CheckoutResult::Paid;
				if (result == Payments::CheckoutResult::Pending || isPaid) {
					if (const auto strong = weak.data()) {
						strong->window()->setFocus();
						strong->closeBox();
					}
				}
				if (isPaid) {
					reloadOnDone();
					const auto filter = [=](const auto &...) {
						if (const auto window = weakWindow.get()) {
							window->showSection(Info::Boosts::Make(peer));
						}
						return false;
					};
					const auto title = isSpecific
						? tr::lng_giveaway_awarded_title
						: tr::lng_giveaway_created_title;
					const auto body = isSpecific
						? tr::lng_giveaway_awarded_body
						: tr::lng_giveaway_created_body;
					show->showToast({
						.text = Ui::Text::Bold(
							title(tr::now)).append('\n').append(
								body(
									tr::now,
									lt_link,
									Ui::Text::Link(
										tr::lng_giveaway_created_link(
											tr::now)),
									Ui::Text::WithEntities)),
						.duration = kDoneTooltipDuration,
						.adaptive = true,
						.filter = filter,
					});
				} else {
					state->confirmButtonBusy = false;
				}
			};
			const auto startPrepaid = [=](Fn<void()> close) {
				if (!weak) {
					close();
					return;
				}
				state->apiOptions.applyPrepaid(
					invoice,
					prepaid->id
				) | rpl::start_with_error_done([=](const QString &error) {
					if (const auto window = weakWindow.get()) {
						window->uiShow()->showToast(error);
						close();
						done(Payments::CheckoutResult::Cancelled);
					}
				}, [=] {
					close();
					done(Payments::CheckoutResult::Paid);
				}, box->lifetime());
			};
			if (prepaid) {
				const auto cancel = [=](Fn<void()> close) {
					if (weak) {
						state->confirmButtonBusy = false;
					}
					close();
				};
				show->show(Ui::MakeConfirmBox({
					.text = tr::lng_giveaway_start_sure(tr::now),
					.confirmed = startPrepaid,
					.cancelled = cancel,
				}));
			} else {
				Payments::CheckoutProcess::Start(std::move(invoice), done);
			}
		});
		box->addButton(std::move(button));
	}
	state->typeValue.force_assign(GiveawayType::Random);

	std::move(
		showFinished
	) | rpl::take(1) | rpl::start_with_next([=] {
		if (!loading->toggled()) {
			return;
		}
		const auto done = [=] {
			state->lifetimeApi.destroy();
			loading->toggle(false, anim::type::instant);
			state->confirmButtonBusy = false;
			fillSliderContainer();
			rebuildListOptions(1);
			contentWrap->toggle(true, anim::type::instant);
			contentWrap->resizeToWidth(box->width());
		};
		if (prepaid) {
			return done();
		}
		state->lifetimeApi = state->apiOptions.request(
		) | rpl::start_with_error_done([=](const QString &error) {
			box->uiShow()->showToast(error);
			box->closeBox();
		}, done);
	}, box->lifetime());
}

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
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
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

} // namespace

void CreateGiveawayBox(
		not_null<Ui::GenericBox*> box,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer) {
	box->setWidth(st::boxWideWidth);

	const auto weakWindow = base::make_weak(controller->parentController());

	const auto bar = box->verticalLayout()->add(
		object_ptr<Ui::Premium::TopBar>(
			box,
			st::giveawayGiftCodeCover,
			nullptr,
			tr::lng_giveaway_new_title(),
			tr::lng_giveaway_new_about(Ui::Text::RichLangValue),
			true));
	{
		bar->setPaused(true);
		bar->setMaximumHeight(st::giveawayGiftCodeTopHeight);
		bar->setMinimumHeight(st::infoLayerTopBarHeight);
		bar->resize(bar->width(), bar->maximumHeight());

		const auto container = box->verticalLayout();
		const auto &padding = st::giveawayGiftCodeCoverDividerPadding;
		Settings::AddSkip(container, padding.top());
		Settings::AddDivider(container);
		Settings::AddSkip(container, padding.bottom());

		const auto close = Ui::CreateChild<Ui::IconButton>(
			container.get(),
			st::boxTitleClose);
		close->setClickedCallback([=] { box->closeBox(); });
		box->widthValue(
		) | rpl::start_with_next([=](int) {
			const auto &pos = st::giveawayGiftCodeCoverClosePosition;
			close->moveToRight(pos.x(), pos.y());
		}, box->lifetime());
	}

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

		bool confirmButtonBusy = false;
	};
	const auto state = box->lifetime().make_state<State>(peer);
	const auto typeGroup = std::make_shared<GiveawayGroup>();

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

	{
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
	{
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
				peer);
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
		Settings::AddDividerText(
			dateContainer,
			tr::lng_giveaway_date_about(
				lt_count,
				state->sliderValue.value() | tr::to_count()));
		Settings::AddSkip(dateContainer);
	}

	const auto durationGroup = std::make_shared<Ui::RadiobuttonGroup>(0);
	const auto listOptions = contentWrap->entity()->add(
		object_ptr<Ui::VerticalLayout>(box));
	const auto rebuildListOptions = [=](int amountUsers) {
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

		auto terms = object_ptr<Ui::FlatLabel>(
			listOptions,
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
		listOptions->add(object_ptr<Ui::DividerLabel>(
			listOptions,
			std::move(terms),
			st::settingsDividerLabelPadding));

		box->verticalLayout()->resizeToWidth(box->width());
	};
	{

		rpl::combine(
			state->sliderValue.value(),
			state->typeValue.value()
		) | rpl::start_with_next([=](int users, GiveawayType type) {
			typeGroup->setValue(type);
			rebuildListOptions((type == GiveawayType::SpecificUsers)
				? state->selectedToAward.size()
				: users);
		}, box->lifetime());
	}
	{
		// TODO mini-icon.
		const auto &stButton = st::premiumGiftBox;
		box->setStyle(stButton);
		auto button = object_ptr<Ui::RoundButton>(
			box,
			rpl::conditional(
				state->typeValue.value(
				) | rpl::map(rpl::mappers::_1 == GiveawayType::Random),
				tr::lng_giveaway_start(),
				tr::lng_giveaway_award()),
			st::giveawayGiftCodeStartButton);
		button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		state->typeValue.value(
		) | rpl::start_with_next([=, raw = button.data()] {
			raw->resizeToWidth(box->width()
				- stButton.buttonPadding.left()
				- stButton.buttonPadding.right());
		}, button->lifetime());
		button->setClickedCallback([=] {
			if (state->confirmButtonBusy) {
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
				durationGroup->value());
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
				if (const auto strong = weak.data()) {
					state->confirmButtonBusy = false;
					strong->window()->setFocus();
					strong->closeBox();
				}
				if (result == Payments::CheckoutResult::Paid) {
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
				}
			};
			Payments::CheckoutProcess::Start(std::move(invoice), done);
		});
		box->addButton(std::move(button));
	}
	state->typeValue.force_assign(GiveawayType::Random);

	box->setShowFinishedCallback([=] {
		if (!loading->toggled()) {
			return;
		}
		bar->setPaused(false);
		state->lifetimeApi = state->apiOptions.request(
		) | rpl::start_with_error_done([=](const QString &error) {
		}, [=] {
			state->lifetimeApi.destroy();
			loading->toggle(false, anim::type::instant);
			fillSliderContainer();
			rebuildListOptions(1);
			contentWrap->toggle(true, anim::type::instant);
			contentWrap->resizeToWidth(box->width());
		});
	});
}

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/boosts/create_giveaway_box.h"

#include "api/api_credits.h"
#include "api/api_premium.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "countries/countries_instance.h"
#include "data/data_peer.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "info/channel_statistics/boosts/giveaway/giveaway_list_controllers.h"
#include "info/channel_statistics/boosts/giveaway/giveaway_type_row.h"
#include "info/channel_statistics/boosts/giveaway/select_countries_box.h"
#include "info/channel_statistics/boosts/info_boosts_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/statistics/info_statistics_list_controllers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "payments/payments_checkout_process.h" // Payments::CheckoutProcess
#include "payments/payments_form.h" // Payments::InvoicePremiumGiftCode
#include "settings/settings_common.h"
#include "settings/settings_premium.h" // Settings::ShowPremium
#include "ui/boxes/choose_date_time.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/ui_utility.h"
#include "styles/style_color_indices.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"

#include <xxhash.h> // XXH64.

namespace {

constexpr auto kDoneTooltipDuration = 5 * crl::time(1000);
constexpr auto kAdditionalPrizeLengthMax = 128;

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

[[nodiscard]] uint64 UniqueIdFromCreditsOption(
		const Data::CreditsGiveawayOption &d,
		not_null<PeerData*> peer) {
	const auto string = QString::number(d.credits)
		+ d.storeProduct
		+ d.currency
		+ QString::number(d.amount)
		+ QString::number(peer->id.value)
		+ QString::number(peer->session().uniqueId());

	return XXH64(string.data(), string.size() * sizeof(ushort), 0);
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
		rpl::producer<QString> titleText,
		rpl::producer<TextWithEntities> subtitleText) {
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
	) | rpl::start_with_next([=] {
		auto p = Painter(closeTopBar);
		const auto r = closeTopBar->rect();
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
		Ui::Premium::TopBarDescriptor{
			.clickContextOther = nullptr,
			.title = tr::lng_giveaway_new_title(),
			.about = std::move(subtitleText),
			.light = true,
			.optimizeMinistars = false,
		});
	bar->setAttribute(Qt::WA_TransparentForMouseEvents);

	box->addRow(
		object_ptr<Ui::BoxContentDivider>(
			box.get(),
			st::giveawayGiftCodeTopHeight
				- st::boxTitleHeight
				+ st::boxDividerHeight
				+ st::defaultVerticalListSkip,
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
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Fn<void()> reloadOnDone,
		std::optional<Data::BoostPrepaidGiveaway> prepaid) {
	box->setWidth(st::boxWideWidth);

	const auto weakWindow = base::make_weak(navigation->parentController());

	using GiveawayType = Giveaway::GiveawayTypeRow::Type;
	using GiveawayGroup = Ui::RadioenumGroup<GiveawayType>;
	using CreditsGroup = Ui::RadioenumGroup<int>;
	struct State final {
		State(not_null<PeerData*> p) : apiOptions(p), apiCreditsOptions(p) {
		}

		Api::PremiumGiftCodeOptions apiOptions;
		Api::CreditsGiveawayOptions apiCreditsOptions;
		rpl::lifetime lifetimeApi;

		std::vector<not_null<PeerData*>> selectedToAward;
		rpl::event_stream<> toAwardAmountChanged;

		std::vector<not_null<PeerData*>> selectedToSubscribe;

		rpl::variable<GiveawayType> typeValue;
		rpl::variable<int> sliderValue;
		rpl::variable<TimeId> dateValue;
		rpl::variable<std::vector<QString>> countriesValue;

		rpl::variable<QString> additionalPrize;
		rpl::variable<int> chosenMonths;
		rpl::variable<bool> showWinners;

		rpl::variable<bool> confirmButtonBusy = true;
	};
	const auto group = peer->isMegagroup();
	const auto state = box->lifetime().make_state<State>(peer);
	const auto typeGroup = std::make_shared<GiveawayGroup>();
	const auto creditsGroup = std::make_shared<CreditsGroup>();

	const auto isPrepaidCredits = (prepaid && prepaid->credits);

	const auto isSpecificUsers = [=] {
		return !state->selectedToAward.empty();
	};
	const auto hideSpecificUsersOn = [=] {
		return rpl::combine(
			state->typeValue.value(),
			state->toAwardAmountChanged.events_starting_with(
				rpl::empty_value()) | rpl::type_erased()
		) | rpl::map([=](GiveawayType type, auto) {
			return (type == GiveawayType::Credits) || !isSpecificUsers();
		});
	};

	auto showFinished = Ui::BoxShowFinishes(box);
	AddPremiumTopBarWithDefaultTitleBar(
		box,
		rpl::duplicate(showFinished),
		rpl::conditional(
			hideSpecificUsersOn(),
			tr::lng_giveaway_start(),
			tr::lng_giveaway_award()),
		rpl::conditional(
			isPrepaidCredits
				? rpl::single(true) | rpl::type_erased()
				: state->typeValue.value() | rpl::map(
					rpl::mappers::_1 == GiveawayType::Credits),
			(peer->isMegagroup()
				? tr::lng_giveaway_credits_new_about_group()
				: tr::lng_giveaway_credits_new_about()),
			(peer->isMegagroup()
				? tr::lng_giveaway_new_about_group()
				: tr::lng_giveaway_new_about())
		) | rpl::map(Ui::Text::RichLangValue));
	{
		const auto &padding = st::giveawayGiftCodeCoverDividerPadding;
		Ui::AddSkip(box->verticalLayout(), padding.bottom());
	}

	const auto loading = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
	{
		loading->toggle(true, anim::type::instant);
		const auto container = loading->entity();
		Ui::AddSkip(container);
		Ui::AddSkip(container);
		container->add(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_contacts_loading(),
					st::giveawayLoadingLabel)));
		Ui::AddSkip(container);
		Ui::AddSkip(container);
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
				prepaid->credits
					? GiveawayType::PrepaidCredits
					: GiveawayType::Prepaid,
				prepaid->credits ? st::colorIndexOrange : prepaid->id,
				tr::lng_boosts_prepaid_giveaway_single(),
				prepaid->credits
					? tr::lng_boosts_prepaid_giveaway_credits_status(
						lt_count,
						rpl::single(prepaid->quantity) | tr::to_count(),
						lt_amount,
						tr::lng_prize_credits_amount(
							lt_count_decimal,
							rpl::single(prepaid->credits) | tr::to_count()))
					: tr::lng_boosts_prepaid_giveaway_status(
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
				state->toAwardAmountChanged.events_starting_with(
					rpl::empty_value()
				) | rpl::map([=] {
					const auto &selected = state->selectedToAward;
					return selected.empty()
						? tr::lng_giveaway_create_subtitle()
						: (selected.size() == 1)
						? rpl::single(selected.front()->name())
						: tr::lng_giveaway_award_chosen(
							lt_count,
							rpl::single(selected.size()) | tr::to_count());
				}) | rpl::flatten_latest(),
				group));
		row->addRadio(typeGroup);
		row->setClickedCallback([=] {
			auto initBox = [=](not_null<PeerListBox*> peersBox) {
				peersBox->setTitle(tr::lng_giveaway_award_option());

				auto aboveOwned = object_ptr<Ui::VerticalLayout>(peersBox);
				const auto above = aboveOwned.data();
				peersBox->peerListSetAboveWidget(std::move(aboveOwned));
				Ui::AddSkip(above);
				const auto buttonRandom = above->add(
					object_ptr<Ui::SettingsButton>(
						peersBox,
						tr::lng_giveaway_random_button(),
						st::settingsButtonLightNoIcon));
				buttonRandom->setClickedCallback([=] {
					state->selectedToAward.clear();
					state->toAwardAmountChanged.fire({});
					state->typeValue.force_assign(GiveawayType::Random);
					peersBox->closeBox();
				});
				Ui::AddSkip(above);

				peersBox->addButton(tr::lng_settings_save(), [=] {
					state->selectedToAward = peersBox->collectSelectedRows();
					state->toAwardAmountChanged.fire({});
					state->typeValue.force_assign(GiveawayType::Random);
					peersBox->closeBox();
				});
				peersBox->addButton(tr::lng_cancel(), [=] {
					peersBox->closeBox();
				});
			};

			using Controller = Giveaway::AwardMembersListController;
			auto listController = std::make_unique<Controller>(
				navigation,
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
	const auto creditsOption = [=](int index) {
		const auto options = state->apiCreditsOptions.options();
		return (index >= 0 && index < options.size())
			? options[index]
			: Data::CreditsGiveawayOption();
	};
	const auto creditsOptionWinners = [=](int index) {
		const auto winners = creditsOption(index).winners;
		return ranges::views::all(
			winners
		) | ranges::views::transform([](const auto &w) {
			return w.users;
		}) | ranges::to_vector;
	};
	const auto creditsTypeWrap = contentWrap->entity()->add(
		object_ptr<Ui::VerticalLayout>(contentWrap->entity()));
	const auto fillCreditsTypeWrap = [=] {
		if (state->apiCreditsOptions.options().empty()) {
			return;
		}

		const auto row = creditsTypeWrap->add(
			object_ptr<Giveaway::GiveawayTypeRow>(
				box,
				GiveawayType::Credits,
				st::colorIndexOrange,
				tr::lng_credits_summary_title(),
				tr::lng_giveaway_create_subtitle(),
				QImage()));
		row->addRadio(typeGroup);
		row->setClickedCallback([=] {
			state->typeValue.force_assign(GiveawayType::Credits);
		});
	};

	{
		const auto &padding = st::giveawayGiftCodeTypeDividerPadding;
		Ui::AddSkip(contentWrap->entity(), padding.top());
		Ui::AddDivider(contentWrap->entity());
		Ui::AddSkip(contentWrap->entity(), padding.bottom());
	}

	const auto randomWrap = contentWrap->entity()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			contentWrap,
			object_ptr<Ui::VerticalLayout>(box)));
	state->typeValue.value(
	) | rpl::start_with_next([=](GiveawayType type) {
		randomWrap->toggle(!isSpecificUsers(), anim::type::instant);
	}, randomWrap->lifetime());

	randomWrap->toggleOn(hideSpecificUsersOn(), anim::type::instant);

	const auto randomCreditsWrap = randomWrap->entity()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			contentWrap,
			object_ptr<Ui::VerticalLayout>(box)));
	randomCreditsWrap->toggleOn(
		state->typeValue.value(
		) | rpl::map(rpl::mappers::_1 == GiveawayType::Credits),
		anim::type::instant);
	const auto fillCreditsOptions = [=] {
		randomCreditsWrap->entity()->clear();

		const auto &st = st::giveawayTypeListItem;
		const auto &stButton = st::defaultSettingsButton;
		const auto &stStatus = st::defaultTextStyle;
		const auto buttonInnerSkip = st.height - stButton.height;
		const auto options = state->apiCreditsOptions.options();
		const auto content = randomCreditsWrap->entity();
		const auto title = Ui::AddSubsectionTitle(
			content,
			tr::lng_giveaway_credits_options_title());

		const auto rightLabel = Ui::CreateChild<Ui::FlatLabel>(
			content,
			st::giveawayGiftCodeQuantitySubtitle);
		rightLabel->show();

		rpl::combine(
			tr::lng_giveaway_quantity(
				lt_count,
				creditsGroup->value() | rpl::map([=](int i) -> float64 {
					return creditsOption(i).yearlyBoosts;
				})),
			title->positionValue(),
			content->geometryValue()
		) | rpl::start_with_next([=](QString s, const QPoint &p, QRect) {
			rightLabel->setText(std::move(s));
			rightLabel->moveToRight(st::boxRowPadding.right(), p.y());
		}, rightLabel->lifetime());

		const auto buttonHeight = st.height;
		const auto minCredits = 0;

		struct State final {
			rpl::variable<bool> isExtended = false;
		};
		const auto creditsState = content->lifetime().make_state<State>();

		for (auto i = 0; i < options.size(); i++) {
			const auto &option = options[i];
			if (option.credits < minCredits) {
				continue;
			}
			struct State final {
				std::optional<Ui::Text::String> text;
				QString status;
				bool hasStatus = false;
			};
			const auto buttonWrap = content->add(
				object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
					content,
					object_ptr<Ui::SettingsButton>(
						content,
						rpl::never<QString>(),
						stButton)));
			const auto button = buttonWrap->entity();
			button->setPaddingOverride({ 0, buttonInnerSkip, 0, 0 });
			const auto buttonState = button->lifetime().make_state<State>();
			buttonState->text.emplace(
				st.nameStyle,
				tr::lng_credits_summary_options_credits(
					tr::now,
					lt_count_decimal,
					option.credits));
			buttonState->status = tr::lng_giveaway_credits_option_status(
				tr::now,
				lt_count_decimal,
				option.credits);
			const auto price = Ui::CreateChild<Ui::FlatLabel>(
				button,
				Ui::FillAmountAndCurrency(option.amount, option.currency),
				st::creditsTopupPrice);
			const auto inner = Ui::CreateChild<Ui::RpWidget>(button);
			const auto stars = Ui::GenerateStars(
				st.nameStyle.font->height,
				(i + 1));
			const auto textLeft = st.photoPosition.x()
				+ (st.nameStyle.font->spacew * 2)
				+ (stars.width() / style::DevicePixelRatio());
			state->sliderValue.value(
			) | rpl::start_with_next([=](int users) {
				const auto option = creditsOption(i);
				buttonState->hasStatus = false;
				for (const auto &winner : option.winners) {
					if (winner.users == users) {
						auto status = tr::lng_giveaway_credits_option_status(
							tr::now,
							lt_count_decimal,
							winner.perUserStars);
						buttonState->status = std::move(status);
						buttonState->hasStatus = true;
						inner->update();
						return;
					}
				}
				inner->update();
			}, button->lifetime());
			inner->paintRequest(
			) | rpl::start_with_next([=](const QRect &rect) {
				auto p = QPainter(inner);
				const auto namey = buttonState->hasStatus
					? st.namePosition.y()
					: (buttonHeight - stStatus.font->height) / 2;
				p.drawImage(st.photoPosition.x(), namey, stars);
				p.setPen(st.nameFg);
				buttonState->text->draw(p, {
					.position = QPoint(textLeft, namey),
					.availableWidth = inner->width() - textLeft,
					.elisionLines = 1,
				});
				if (buttonState->hasStatus) {
					p.setFont(stStatus.font);
					p.setPen(st.statusFg);
					p.setBrush(Qt::NoBrush);
					p.drawText(
						st.photoPosition.x(),
						st.statusPosition.y() + stStatus.font->ascent,
						buttonState->status);
				}
			}, inner->lifetime());
			button->widthValue(
			) | rpl::start_with_next([=](int width) {
				price->moveToRight(
					st::boxRowPadding.right(),
					(buttonHeight - price->height()) / 2);
				inner->moveToLeft(0, 0);
				inner->resize(
					width
						- price->width()
						- st::boxRowPadding.right()
						- st::boxRowPadding.left() / 2,
					buttonHeight);
			}, button->lifetime());

			{
				const auto &st = st::defaultCheckbox;
				const auto radio = Ui::CreateChild<Ui::Radioenum<int>>(
					button,
					creditsGroup,
					i,
					QString(),
					st);
				radio->moveToLeft(
					st::boxRowPadding.left(),
					(buttonHeight - radio->checkRect().height()) / 2);
				radio->setAttribute(Qt::WA_TransparentForMouseEvents);
				radio->show();
			}
			button->setClickedCallback([=] {
				creditsGroup->setValue(i);
			});
			if (option.isDefault) {
				creditsGroup->setValue(i);
			}
			buttonWrap->toggle(
				(!option.isExtended) || option.isDefault,
				anim::type::instant);
			if (option.isExtended) {
				buttonWrap->toggleOn(creditsState->isExtended.value());
			}
			Ui::ToggleChildrenVisibility(button, true);
		}

		{
			Ui::AddSkip(content, st::settingsButton.padding.top());
			const auto showMoreWrap = Info::Statistics::AddShowMoreButton(
				content,
				tr::lng_stories_show_more());
			showMoreWrap->toggle(true, anim::type::instant);

			showMoreWrap->entity()->setClickedCallback([=] {
				showMoreWrap->toggle(false, anim::type::instant);
				creditsState->isExtended = true;
			});
		}

		Ui::AddSkip(content);
		Ui::AddDividerText(content, tr::lng_giveaway_credits_options_about());
		Ui::AddSkip(content);
	};

	const auto sliderContainerWrap = randomWrap->entity()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			randomWrap,
			object_ptr<Ui::VerticalLayout>(randomWrap)));
	const auto sliderContainer = sliderContainerWrap->entity();
	sliderContainerWrap->toggle(true, anim::type::instant);
	const auto fillSliderContainer = [=] {
		const auto availablePresets = state->apiOptions.availablePresets();
		const auto creditsOptions = state->apiCreditsOptions.options();
		if (prepaid) {
			state->sliderValue = prepaid->quantity;
			return;
		}
		if (availablePresets.empty()
			&& (creditsOptions.empty()
				|| creditsOptions.front().winners.empty())) {
			return;
		}
		state->sliderValue = availablePresets.empty()
			? creditsOptions.front().winners.front().users
			: availablePresets.front();
		auto creditsValueType = typeGroup->value(
			) | rpl::map(rpl::mappers::_1 == GiveawayType::Credits);
		const auto title = Ui::AddSubsectionTitle(
			sliderContainer,
			rpl::conditional(
				rpl::duplicate(creditsValueType),
				tr::lng_giveaway_credits_quantity_title(),
				tr::lng_giveaway_quantity_title()));
		const auto rightLabel = Ui::CreateChild<Ui::FlatLabel>(
			sliderContainer,
			st::giveawayGiftCodeQuantitySubtitle);
		rightLabel->show();
		rpl::duplicate(
			creditsValueType
		) | rpl::start_with_next([=](bool isCredits) {
			rightLabel->setVisible(!isCredits);
		}, rightLabel->lifetime());

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
		Ui::AddSkip(sliderContainer, padding.top());

		const auto sliderParent = sliderContainer->add(
			object_ptr<Ui::VerticalLayout>(sliderContainer),
			st::boxRowPadding);
		struct State final {
			Ui::MediaSliderWheelless *slider = nullptr;
		};
		const auto sliderState = sliderParent->lifetime().make_state<State>();
		Ui::AddSkip(sliderContainer, padding.bottom());
		rpl::combine(
			rpl::duplicate(creditsValueType),
			creditsGroup->value()
		) | rpl::start_with_next([=](bool isCredits, int value) {
			while (sliderParent->count()) {
				delete sliderParent->widgetAt(0);
			}
			sliderState->slider = sliderParent->add(
				object_ptr<Ui::MediaSliderWheelless>(
					sliderContainer,
					st::settingsScale));
			sliderState->slider->resize(
				sliderState->slider->width(),
				st::settingsScale.seekSize.height());
			const auto &values = isCredits
				? creditsOptionWinners(value)
				: availablePresets;
			const auto resultValue = [&] {
				const auto sliderValue = state->sliderValue.current();
				return ranges::contains(values, sliderValue)
					? sliderValue
					: values.front();
			}();
			state->sliderValue.force_assign(resultValue);
			if (values.size() <= 1) {
				sliderContainerWrap->toggle(false, anim::type::instant);
				return;
			} else {
				sliderContainerWrap->toggle(true, anim::type::instant);
			}
			sliderState->slider->setPseudoDiscrete(
				values.size(),
				[=](int index) { return values[index]; },
				resultValue,
				[=](int boosts) { state->sliderValue = boosts; },
				[](int) {});
		}, sliderParent->lifetime());

		rpl::combine(
			rpl::duplicate(creditsValueType),
			creditsGroup->value(),
			state->sliderValue.value()
		) | rpl::start_with_next([=](
				bool isCredits,
				int credits,
				int boosts) {
			floatLabel->setText(QString::number(boosts));

			if (!sliderState->slider) {
				return;
			}
			const auto &values = isCredits
				? creditsOptionWinners(credits)
				: availablePresets;
			const auto count = values.size();
			if (count <= 1) {
				return;
			}
			const auto sliderWidth = sliderState->slider->width()
				- st::settingsScale.seekSize.width();
			for (auto i = 0; i < count; i++) {
				if ((i + 1 == count || values[i + 1] > boosts)
					&& values[i] <= boosts) {
					const auto x = (sliderWidth * i) / (count - 1);
					const auto mapped = sliderState->slider->mapTo(
						sliderContainer,
						sliderState->slider->pos());
					floatLabel->moveToLeft(
						mapped.x()
							+ x
							+ st::settingsScale.seekSize.width() / 2
							- floatLabel->width() / 2,
						mapped.y()
							- floatLabel->height()
							- st::giveawayGiftCodeSliderFloatSkip);
					break;
				}
			}
		}, floatLabel->lifetime());

		Ui::AddSkip(sliderContainer);
		Ui::AddDividerText(
			sliderContainer,
			rpl::conditional(
				rpl::duplicate(creditsValueType),
				tr::lng_giveaway_credits_quantity_about(),
				tr::lng_giveaway_quantity_about()));
		Ui::AddSkip(sliderContainer);

		sliderContainer->resizeToWidth(box->width());
	};

	{
		const auto channelsContainer = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		Ui::AddSubsectionTitle(
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
		listState->controller.setTopStatus((peer->isMegagroup()
			? tr::lng_giveaway_channels_this_group
			: tr::lng_giveaway_channels_this)(
				lt_count,
				state->sliderValue.value(
				) | rpl::map([=](int v) -> float64 {
					return (prepaid && prepaid->boosts)
						? prepaid->boosts
						: (state->apiOptions.giveawayBoostsPerPremium() * v);
				})));

		using IconType = Settings::IconType;
		Settings::AddButtonWithIcon(
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
		Ui::AddSkip(channelsContainer, padding.top());
		Ui::AddDividerText(
			channelsContainer,
			tr::lng_giveaway_channels_about());
		Ui::AddSkip(channelsContainer, padding.bottom());
	}

	const auto membersGroup = std::make_shared<GiveawayGroup>();
	{
		const auto countriesContainer = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		Ui::AddSubsectionTitle(
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
				const auto was = membersGroup->current();
				membersGroup->setValue(type);
				const auto now = membersGroup->current();
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
					rpl::duplicate(subtitle),
					group));
			row->addRadio(membersGroup);
			row->setClickedCallback(createCallback(GiveawayType::AllMembers));
		}
		const auto row = countriesContainer->add(
			object_ptr<Giveaway::GiveawayTypeRow>(
				box,
				GiveawayType::OnlyNewMembers,
				std::move(subtitle),
				group));
		row->addRadio(membersGroup);
		row->setClickedCallback(createCallback(GiveawayType::OnlyNewMembers));

		Ui::AddSkip(countriesContainer);
		Ui::AddDividerText(
			countriesContainer,
			(group
				? tr::lng_giveaway_users_about_group()
				: tr::lng_giveaway_users_about()));
		Ui::AddSkip(countriesContainer);
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

	const auto durationGroup = std::make_shared<Ui::RadiobuttonGroup>(0);
	durationGroup->setChangedCallback([=](int value) {
		state->chosenMonths = state->apiOptions.monthsFromPreset(value);
	});
	const auto listOptionsRandom = randomWrap->entity()->add(
		object_ptr<Ui::VerticalLayout>(box));
	const auto listOptionsSpecific = contentWrap->entity()->add(
		object_ptr<Ui::VerticalLayout>(box));
	const auto rebuildListOptions = [=](GiveawayType type, int usersCount) {
		if (prepaid) {
			return;
		}
		while (listOptionsRandom->count()) {
			delete listOptionsRandom->widgetAt(0);
		}
		while (listOptionsSpecific->count()) {
			delete listOptionsSpecific->widgetAt(0);
		}
		const auto listOptions = isSpecificUsers()
			? listOptionsSpecific
			: listOptionsRandom;
		if (type != GiveawayType::Credits) {
			Ui::AddSubsectionTitle(
				listOptions,
				tr::lng_giveaway_duration_title(
					lt_count,
					rpl::single(usersCount) | tr::to_count()),
				st::giveawayGiftCodeChannelsSubsectionPadding);
			Ui::Premium::AddGiftOptions(
				listOptions,
				durationGroup,
				state->apiOptions.options(usersCount),
				st::giveawayGiftCodeGiftOption,
				true);

			Ui::AddSkip(listOptions);

			auto termsContainer = object_ptr<Ui::VerticalLayout>(listOptions);
			addTerms(termsContainer.data());
			listOptions->add(object_ptr<Ui::DividerLabel>(
				listOptions,
				std::move(termsContainer),
				st::defaultBoxDividerLabelPadding));

			Ui::AddSkip(listOptions);
		}

		box->verticalLayout()->resizeToWidth(box->width());
	};
	if (!prepaid) {
		rpl::combine(
			state->sliderValue.value(),
			state->typeValue.value()
		) | rpl::start_with_next([=](int users, GiveawayType type) {
			typeGroup->setValue(type);
			rebuildListOptions(
				type,
				isSpecificUsers() ? state->selectedToAward.size() : users);
		}, box->lifetime());
	} else {
		typeGroup->setValue(GiveawayType::Random);
	}

	{
		const auto additionalWrap = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		const auto additionalToggle = additionalWrap->add(
			object_ptr<Ui::SettingsButton>(
				additionalWrap,
				tr::lng_giveaway_additional_prizes(),
				st::defaultSettingsButton));
		const auto additionalInner = additionalWrap->add(
			object_ptr<Ui::SlideWrap<Ui::InputField>>(
				additionalWrap,
				object_ptr<Ui::InputField>(
					additionalWrap,
					st::giveawayGiftCodeAdditionalField,
					Ui::InputField::Mode::SingleLine,
					tr::lng_giveaway_additional_prizes_ph()),
				st::giveawayGiftCodeAdditionalPaddingMin));
		const auto additionalPadded = additionalInner->wrapped();
		const auto additional = additionalInner->entity();
		additionalInner->hide(anim::type::instant);
		additional->setMaxLength(kAdditionalPrizeLengthMax);
		const auto fillAdditionalPrizeValue = [=] {
			state->additionalPrize = additional->getLastText().trimmed();
		};
		additionalToggle->toggleOn(rpl::single(false))->toggledChanges(
		) | rpl::start_with_next([=](bool toggled) {
			if (!toggled && Ui::InFocusChain(additional)) {
				additionalWrap->setFocus();
				state->additionalPrize = QString();
			}
			additionalInner->toggle(toggled, anim::type::normal);
			if (toggled) {
				additional->setFocusFast();
				fillAdditionalPrizeValue();
			}
		}, additionalInner->lifetime());
		additionalInner->finishAnimating();

		additional->changes() | rpl::filter([=] {
			return additionalInner->toggled();
		}) | rpl::start_with_next(
			fillAdditionalPrizeValue,
			additional->lifetime());

		Ui::AddSkip(additionalWrap);

		auto monthsValue = prepaid
			? (rpl::single(prepaid->months) | rpl::type_erased())
			: state->chosenMonths.value();
		const auto usersCountByType = [=](GiveawayType type) {
			if (!isSpecificUsers()) {
				return state->sliderValue.value() | rpl::type_erased();
			}
			return state->toAwardAmountChanged.events_starting_with_copy(
				rpl::empty
			) | rpl::map([=] {
				return int(state->selectedToAward.size());
			}) | rpl::type_erased();
		};
		auto usersCountValue = prepaid
			? (rpl::single(prepaid->quantity) | rpl::type_erased())
			: state->typeValue.value(
			) | rpl::map(usersCountByType) | rpl::flatten_latest();

		const auto additionalLabel = Ui::CreateChild<Ui::FlatLabel>(
			additionalInner,
			rpl::duplicate(usersCountValue) | rpl::map([](int count) {
				return QString::number(count);
			}),
			st::giveawayGiftCodeAdditionalLabel);
		additionalLabel->widthValue() | rpl::start_with_next([=](int width) {
			const auto min = st::giveawayGiftCodeAdditionalPaddingMin;
			const auto skip = st::giveawayGiftCodeAdditionalLabelSkip;
			const auto added = std::max(width + skip - min.left(), 0);
			const auto &field = st::giveawayGiftCodeAdditionalField;
			const auto top = field.textMargins.top();
			additionalLabel->moveToLeft(min.right(), min.top() + top);
			additionalPadded->setPadding(min + QMargins(added, 0, 0, 0));
		}, additionalLabel->lifetime());

		auto additionalAbout = rpl::combine(
			state->additionalPrize.value(),
			std::move(monthsValue),
			std::move(usersCountValue)
		) | rpl::map([=](QString prize, int months, int users) {
			const auto duration = ((months >= 12)
				? tr::lng_premium_gift_duration_years
				: tr::lng_premium_gift_duration_months)(
					tr::now,
					lt_count,
					(months >= 12) ? (months / 12) : months);
			if (prize.isEmpty()) {
				return tr::lng_giveaway_prizes_just_premium(
					tr::now,
					lt_count,
					users,
					lt_duration,
					TextWithEntities{ duration },
					Ui::Text::RichLangValue);
			}
			return tr::lng_giveaway_prizes_additional(
				tr::now,
				lt_count,
				users,
				lt_prize,
				TextWithEntities{ prize },
				lt_duration,
				TextWithEntities{ duration },
				Ui::Text::RichLangValue);
		});
		auto creditsAdditionalAbout = rpl::combine(
			state->additionalPrize.value(),
			state->sliderValue.value(),
			creditsGroup->value()
		) | rpl::map([=](QString prize, int users, int creditsIndex) {
			const auto credits = creditsOption(creditsIndex).credits;
			return prize.isEmpty()
				? tr::lng_giveaway_prizes_just_credits(
					tr::now,
					lt_count,
					credits,
					Ui::Text::RichLangValue)
				: tr::lng_giveaway_prizes_additional_credits(
					tr::now,
					lt_count,
					users,
					lt_prize,
					TextWithEntities{ prize },
					lt_amount,
					tr::lng_giveaway_prizes_additional_credits_amount(
						tr::now,
						lt_count,
						credits,
						Ui::Text::RichLangValue),
					Ui::Text::RichLangValue);
		});

		auto creditsValueType = typeGroup->value(
		) | rpl::map(rpl::mappers::_1 == GiveawayType::Credits);

		Ui::AddDividerText(
			additionalWrap,
			rpl::conditional(
				additionalToggle->toggledValue(),
				rpl::conditional(
					rpl::duplicate(creditsValueType),
					std::move(creditsAdditionalAbout),
					std::move(additionalAbout)),
				rpl::conditional(
					rpl::duplicate(creditsValueType),
					tr::lng_giveaway_additional_credits_about(),
					tr::lng_giveaway_additional_about()
				) | rpl::map(Ui::Text::WithEntities)));
		Ui::AddSkip(additionalWrap);
	}

	{
		const auto dateContainer = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		Ui::AddSubsectionTitle(
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
							+ state->apiOptions.giveawayPeriodMax();
					},
				});
			}));
		});

		Ui::AddSkip(dateContainer);
		if (prepaid) {
			auto terms = object_ptr<Ui::VerticalLayout>(dateContainer);
			terms->add(object_ptr<Ui::FlatLabel>(
				terms,
				(group
					? tr::lng_giveaway_date_about_group
					: tr::lng_giveaway_date_about)(
						lt_count,
						state->sliderValue.value() | tr::to_count()),
				st::boxDividerLabel));
			Ui::AddSkip(terms.data());
			Ui::AddSkip(terms.data());
			addTerms(terms.data());
			dateContainer->add(object_ptr<Ui::DividerLabel>(
				dateContainer,
				std::move(terms),
				st::defaultBoxDividerLabelPadding));
			Ui::AddSkip(dateContainer);
		} else {
			Ui::AddDividerText(
				dateContainer,
				(group
					? tr::lng_giveaway_date_about_group
					: tr::lng_giveaway_date_about)(
						lt_count,
						state->sliderValue.value() | tr::to_count()));
			Ui::AddSkip(dateContainer);
		}
	}

	{
		const auto winnersWrap = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		const auto winnersToggle = winnersWrap->add(
			object_ptr<Ui::SettingsButton>(
				winnersWrap,
				tr::lng_giveaway_show_winners(),
				st::defaultSettingsButton));
		state->showWinners = winnersToggle->toggleOn(
			rpl::single(false)
		)->toggledValue();
		Ui::AddSkip(winnersWrap);

		Ui::AddDividerText(
			winnersWrap,
			tr::lng_giveaway_show_winners_about());
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
				hideSpecificUsersOn(),
				tr::lng_giveaway_start(),
				tr::lng_giveaway_award()),
			(prepaid && prepaid->boosts)
				? rpl::single(prepaid->boosts) | rpl::type_erased()
				: rpl::conditional(
					state->typeValue.value(
					) | rpl::map(rpl::mappers::_1 == GiveawayType::Credits),
					creditsGroup->value() | rpl::map([=](int v) {
						return creditsOption(v).yearlyBoosts;
					}),
					rpl::combine(
						state->sliderValue.value(),
						hideSpecificUsersOn()
					) | rpl::map([=](int value, bool random) -> int {
						return state->apiOptions.giveawayBoostsPerPremium()
							* (random
								? value
								: int(state->selectedToAward.size()));
					})),
			state->confirmButtonBusy.value() | rpl::map(!rpl::mappers::_1));

		{
			const auto loadingAnimation = InfiniteRadialAnimationWidget(
				button,
				st::giveawayGiftCodeStartButton.height / 2);
			AddChildToWidgetCenter(button.data(), loadingAnimation);
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
			const auto type = typeGroup->current();
			const auto isSpecific = isSpecificUsers();
			const auto isRandom = (type == GiveawayType::Random);
			const auto isCredits = (type == GiveawayType::Credits);
			if (!isSpecific && !isRandom && !isCredits) {
				return;
			}
			auto invoice = [&] {
				if (isPrepaidCredits) {
					return Payments::InvoicePremiumGiftCode{
						.creditsAmount = prepaid->credits,
						.randomId = prepaid->id,
						.users = prepaid->quantity,
					};
				} else if (isCredits) {
					const auto option = creditsOption(
						creditsGroup->current());
					return Payments::InvoicePremiumGiftCode{
						.currency = option.currency,
						.storeProduct = option.storeProduct,
						.creditsAmount = option.credits,
						.randomId = UniqueIdFromCreditsOption(option, peer),
						.amount = option.amount,
						.users = state->sliderValue.current(),
					};
				}
				return state->apiOptions.invoice(
					isSpecific
						? state->selectedToAward.size()
						: state->sliderValue.current(),
					prepaid
						? prepaid->months
						: state->apiOptions.monthsFromPreset(
							durationGroup->current()));
			}();
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
			} else if (isRandom || isCredits || isPrepaidCredits) {
				invoice.purpose = Payments::InvoicePremiumGiftCodeGiveaway{
					.boostPeer = peer->asChannel(),
					.additionalChannels = ranges::views::all(
						state->selectedToSubscribe
					) | ranges::views::transform([](
							const not_null<PeerData*> p) {
						return not_null{ p->asChannel() };
					}) | ranges::to_vector,
					.countries = state->countriesValue.current(),
					.additionalPrize = state->additionalPrize.current(),
					.untilDate = state->dateValue.current(),
					.onlyNewSubscribers = (membersGroup->current()
						== GiveawayType::OnlyNewMembers),
					.showWinners = state->showWinners.current(),
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
					const auto group = peer->isMegagroup();
					const auto title = isSpecific
						? tr::lng_giveaway_awarded_title
						: tr::lng_giveaway_created_title;
					const auto body = isSpecific
						? (group
							? tr::lng_giveaway_awarded_body_group
							: tr::lng_giveaway_awarded_body)
						: (group
							? tr::lng_giveaway_created_body_group
							: tr::lng_giveaway_created_body);
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
						.filter = filter,
						.adaptive = true,
						.duration = kDoneTooltipDuration,
					});
				} else if (weak) {
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
			if (!prepaid) {
				state->chosenMonths = state->apiOptions.monthsFromPreset(0);
			}
			fillCreditsTypeWrap();
			fillCreditsOptions();
			rebuildListOptions(state->typeValue.current(), 1);
			contentWrap->toggle(true, anim::type::instant);
			contentWrap->resizeToWidth(box->width());
		};
		const auto receivedOptions = [=] {
			state->lifetimeApi.destroy();
			state->lifetimeApi = state->apiCreditsOptions.request(
			) | rpl::start_with_error_done([=](const QString &error) {
				box->uiShow()->showToast(error);
				box->closeBox();
			}, done);
		};
		if (prepaid) {
			return done();
		}
		state->lifetimeApi = state->apiOptions.request(
		) | rpl::start_with_error_done([=](const QString &error) {
			box->uiShow()->showToast(error);
			box->closeBox();
		}, receivedOptions);
	}, box->lifetime());
}

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/create_giveaway_box.h"

#include "api/api_premium.h"
#include "base/unixtime.h"
#include "boxes/peers/edit_participants_box.h" // ParticipantsBoxController
#include "data/data_peer.h"
#include "data/data_subscription_option.h"
#include "data/data_user.h"
#include "info/boosts/giveaway/giveaway_type_row.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "payments/payments_checkout_process.h" // Payments::CheckoutProcess
#include "payments/payments_form.h" // Payments::InvoicePremiumGiftCode
#include "settings/settings_common.h"
#include "settings/settings_premium.h" // Settings::ShowPremium
#include "ui/boxes/choose_date_time.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace {

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

class MembersListController : public ParticipantsBoxController {
public:
	using ParticipantsBoxController::ParticipantsBoxController;

	void rowClicked(not_null<PeerListRow*> row) override;
	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> participant) const override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

};

void MembersListController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PeerListRow> MembersListController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user || user->isInaccessible() || user->isBot() || user->isSelf()) {
		return nullptr;
	}
	return std::make_unique<PeerListRow>(participant);
}

base::unique_qptr<Ui::PopupMenu> MembersListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

} // namespace

void CreateGiveawayBox(
		not_null<Ui::GenericBox*> box,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer) {
	using GiveawayType = Giveaway::GiveawayTypeRow::Type;
	using GiveawayGroup = Ui::RadioenumGroup<GiveawayType>;
	struct State final {
		State(not_null<PeerData*> p) : apiOptions(p) {
		}

		Api::PremiumGiftCodeOptions apiOptions;
		rpl::lifetime lifetimeApi;

		std::vector<not_null<PeerData*>> selectedToAward;
		rpl::event_stream<> toAwardAmountChanged;

		rpl::variable<GiveawayType> typeValue;
		rpl::variable<int> sliderValue;
		rpl::variable<TimeId> dateValue;

		bool confirmButtonBusy = false;
	};
	const auto state = box->lifetime().make_state<State>(peer);
	const auto typeGroup = std::make_shared<GiveawayGroup>();

	box->setWidth(st::boxWideWidth);

	{
		const auto row = box->verticalLayout()->add(
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
		const auto row = box->verticalLayout()->add(
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

			box->uiShow()->showBox(
				Box<PeerListBox>(
					std::make_unique<MembersListController>(
						controller,
						peer,
						ParticipantsRole::Members),
					std::move(initBox)),
				Ui::LayerOption::KeepOther);
		});
	}

	Settings::AddSkip(box->verticalLayout());
	Settings::AddDivider(box->verticalLayout());
	Settings::AddSkip(box->verticalLayout());

	const auto randomWrap = box->verticalLayout()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
	state->typeValue.value(
	) | rpl::start_with_next([=](GiveawayType type) {
		randomWrap->toggle(type == GiveawayType::Random, anim::type::instant);
	}, randomWrap->lifetime());

	const auto sliderContainer = randomWrap->entity()->add(
		object_ptr<Ui::VerticalLayout>(randomWrap));
	const auto fillSliderContainer = [=] {
		if (sliderContainer->count()) {
			return;
		}
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

		Settings::AddSkip(sliderContainer);
		Settings::AddSkip(sliderContainer);
		const auto slider = sliderContainer->add(
			object_ptr<Ui::MediaSlider>(sliderContainer, st::settingsScale),
			st::boxRowPadding);
		Settings::AddSkip(sliderContainer);
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
						slider->y() - floatLabel->height());
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
		const auto dateContainer = randomWrap->entity()->add(
			object_ptr<Ui::VerticalLayout>(randomWrap));
		Settings::AddSubsectionTitle(
			dateContainer,
			tr::lng_giveaway_date_title());

		state->dateValue = ThreeDaysAfterToday().toSecsSinceEpoch();
		const auto button = Settings::AddButtonWithLabel(
			dateContainer,
			tr::lng_giveaway_date(),
			state->dateValue.value() | rpl::map(
				base::unixtime::parse
			) | rpl::map(Ui::FormatDateTime),
			st::defaultSettingsButton);

		button->setClickedCallback([=] {
			constexpr auto kSevenDays = 3600 * 24 * 7;
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
							+ kSevenDays;
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
	{
		const auto listOptions = box->verticalLayout()->add(
			object_ptr<Ui::VerticalLayout>(box));
		const auto rebuildListOptions = [=](int amountUsers) {
			fillSliderContainer();
			while (listOptions->count()) {
				delete listOptions->widgetAt(0);
			}
			Settings::AddSubsectionTitle(
				listOptions,
				tr::lng_giveaway_duration_title(
					lt_count,
					rpl::single(amountUsers) | tr::to_count()));
			Ui::Premium::AddGiftOptions(
				listOptions,
				durationGroup,
				state->apiOptions.options(amountUsers),
				st::giveawayGiftCodeGiftOption,
				true);

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

		rpl::combine(
			state->sliderValue.value(),
			state->typeValue.value()
		) | rpl::start_with_next([=](int users, GiveawayType type) {
			typeGroup->setValue(type);
			const auto rebuild = [=] {
				rebuildListOptions((type == GiveawayType::SpecificUsers)
					? state->selectedToAward.size()
					: users);
			};
			if (!listOptions->count()) {
				state->lifetimeApi = state->apiOptions.request(
				) | rpl::start_with_error_done([=](const QString &error) {
				}, rebuild);
			} else {
				rebuild();
			}
		}, box->lifetime());
		state->lifetimeApi = state->apiOptions.request(
		) | rpl::start_with_error_done([=](const QString &error) {
		}, [=] {
			rebuildListOptions(1);
		});
	}
	{
		// TODO mini-icon.
		const auto &stButton = st::premiumGiftBox;
		box->setStyle(stButton);
		auto button = object_ptr<Ui::RoundButton>(
			box,
			state->toAwardAmountChanged.events_starting_with(
				rpl::empty_value()
			) | rpl::map([=] {
				return (typeGroup->value() == GiveawayType::SpecificUsers)
					? tr::lng_giveaway_award()
					: tr::lng_giveaway_start();
			}) | rpl::flatten_latest(),
			st::giveawayGiftCodeStartButton);
		button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		button->resizeToWidth(box->width()
			- stButton.buttonPadding.left()
			- stButton.buttonPadding.right());
		button->setClickedCallback([=] {
			if (state->confirmButtonBusy) {
				return;
			}
			if (typeGroup->value() == GiveawayType::SpecificUsers) {
				if (state->selectedToAward.empty()) {
					return;
				}
				auto invoice = state->apiOptions.invoice(
					state->selectedToAward.size(),
					durationGroup->value());
				invoice.purpose = Payments::InvoicePremiumGiftCodeUsers{
					ranges::views::all(
						state->selectedToAward
					) | ranges::views::transform([](
							const not_null<PeerData*> p) {
						return not_null{ p->asUser() };
					}) | ranges::to_vector,
					peer->asChannel(),
				};
				state->confirmButtonBusy = true;
				Payments::CheckoutProcess::Start(
					std::move(invoice),
					crl::guard(box, [=](auto) {
						state->confirmButtonBusy = false;
						box->window()->setFocus();
					}));
			}
		});
		box->addButton(std::move(button));
	}
	state->typeValue.force_assign(GiveawayType::Random);
}

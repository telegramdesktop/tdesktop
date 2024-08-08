/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/edit_invite_link.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/text/format_values.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/effects/credits_graphics.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace Ui {
namespace {

constexpr auto kMaxLimit = std::numeric_limits<int>::max();
constexpr auto kHour = 3600;
constexpr auto kDay = 86400;
constexpr auto kMaxLabelLength = 32;

[[nodiscard]] QString FormatExpireDate(TimeId date) {
	if (date > 0) {
		return langDateTime(base::unixtime::parse(date));
	} else if (-date < kDay) {
		return tr::lng_hours(tr::now, lt_count, (-date / kHour));
	} else if (-date < 7 * kDay) {
		return tr::lng_days(tr::now, lt_count, (-date / kDay));
	} else {
		return tr::lng_weeks(tr::now, lt_count, (-date / (7 * kDay)));
	}
}

} // namespace

void EditInviteLinkBox(
		not_null<GenericBox*> box,
		Fn<InviteLinkSubscriptionToggle()> fillSubscription,
		const InviteLinkFields &data,
		Fn<void(InviteLinkFields)> done) {
	using namespace rpl::mappers;

	const auto link = data.link;
	const auto isGroup = data.isGroup;
	const auto isPublic = data.isPublic;
	const auto subscriptionLocked = data.subscriptionCredits > 0;
	box->setTitle(link.isEmpty()
		? tr::lng_group_invite_new_title()
		: tr::lng_group_invite_edit_title());

	const auto container = box->verticalLayout();
	const auto addTitle = [&](
			not_null<VerticalLayout*> container,
			rpl::producer<QString> text) {
		container->add(
			object_ptr<FlatLabel>(
				container,
				std::move(text),
				st::defaultSubsectionTitle),
			(st::defaultSubsectionTitlePadding
				+ style::margins(0, st::defaultVerticalListSkip, 0, 0)));
	};
	const auto addDivider = [&](
			not_null<VerticalLayout*> container,
			rpl::producer<QString> text,
			style::margins margins = style::margins()) {
		container->add(
			object_ptr<DividerLabel>(
				container,
				object_ptr<FlatLabel>(
					container,
					std::move(text),
					st::boxDividerLabel),
				st::defaultBoxDividerLabelPadding),
			margins);
	};

	const auto now = base::unixtime::now();
	const auto expire = data.expireDate ? data.expireDate : kMaxLimit;
	const auto expireGroup = std::make_shared<RadiobuttonGroup>(expire);
	const auto usage = data.usageLimit ? data.usageLimit : kMaxLimit;
	const auto usageGroup = std::make_shared<RadiobuttonGroup>(usage);

	using Buttons = base::flat_map<int, base::unique_qptr<Radiobutton>>;
	struct State {
		Buttons expireButtons;
		Buttons usageButtons;
		int expireValue = 0;
		int usageValue = 0;
		rpl::variable<bool> requestApproval = false;
		rpl::variable<bool> subscription = false;
	};
	const auto state = box->lifetime().make_state<State>(State{
		.expireValue = expire,
		.usageValue = usage,
		.requestApproval = (data.requestApproval && !isPublic),
		.subscription = false,
	});

	const auto requestApproval = (isPublic || subscriptionLocked)
		? nullptr
		: container->add(
			object_ptr<SettingsButton>(
				container,
				tr::lng_group_invite_request_approve(),
				st::settingsButtonNoIcon),
			style::margins{ 0, 0, 0, st::defaultVerticalListSkip });
	if (requestApproval) {
		requestApproval->toggleOn(state->requestApproval.value(), true);
		requestApproval->setClickedCallback([=] {
			state->requestApproval.force_assign(!requestApproval->toggled());
			state->subscription.force_assign(false);
		});
		addDivider(container, rpl::conditional(
			state->requestApproval.value(),
			(isGroup
				? tr::lng_group_invite_about_approve()
				: tr::lng_group_invite_about_approve_channel()),
			(isGroup
				? tr::lng_group_invite_about_no_approve()
				: tr::lng_group_invite_about_no_approve_channel())));
	}
	auto credits = (Ui::NumberInput*)(nullptr);
	if (!isPublic && fillSubscription) {
		Ui::AddSkip(container);
		const auto &[subscription, input] = fillSubscription();
		credits = input.get();
		subscription->toggleOn(state->subscription.value(), true);
		if (subscriptionLocked) {
			input->setText(QString::number(data.subscriptionCredits));
			input->setReadOnly(true);
			state->subscription.force_assign(true);
			state->requestApproval.force_assign(false);
			subscription->setToggleLocked(true);
			subscription->finishAnimating();
		}
		subscription->setClickedCallback([=, show = box->uiShow()] {
			if (subscriptionLocked) {
				show->showToast(
					tr::lng_group_invite_subscription_toast(tr::now));
				return;
			}
			state->subscription.force_assign(!subscription->toggled());
			state->requestApproval.force_assign(false);
		});
	}

	const auto labelField = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::defaultInputField,
			tr::lng_group_invite_label_header(),
			data.label),
		style::margins(
			st::defaultSubsectionTitlePadding.left(),
			st::defaultVerticalListSkip,
			st::defaultSubsectionTitlePadding.right(),
			st::defaultVerticalListSkip * 2));
	labelField->setMaxLength(kMaxLabelLength);
	addDivider(container, tr::lng_group_invite_label_about());

	const auto &saveLabel = link.isEmpty()
		? tr::lng_formatting_link_create
		: tr::lng_settings_save;
	box->addButton(saveLabel(), [=] {
		const auto label = labelField->getLastText();
		const auto expireDate = (state->expireValue == kMaxLimit)
			? 0
			: (state->expireValue < 0)
			? (base::unixtime::now() - state->expireValue)
			: state->expireValue;
		const auto usageLimit = (state->usageValue == kMaxLimit)
			? 0
			: state->usageValue;
		done(InviteLinkFields{
			.link = link,
			.label = label,
			.expireDate = expireDate,
			.usageLimit = usageLimit,
			.subscriptionCredits = credits
				? credits->getLastText().toInt()
				: 0,
			.requestApproval = state->requestApproval.current(),
			.isGroup = isGroup,
			.isPublic = isPublic,
		});
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	if (subscriptionLocked) {
		return;
	}
	addTitle(container, tr::lng_group_invite_expire_title());
	const auto expiresWrap = container->add(
		object_ptr<VerticalLayout>(container),
		style::margins(0, 0, 0, st::defaultVerticalListSkip));
	addDivider(
		container,
		tr::lng_group_invite_expire_about());

	const auto usagesSlide = container->add(
		object_ptr<SlideWrap<VerticalLayout>>(
			container,
			object_ptr<VerticalLayout>(container)));
	const auto usagesInner = usagesSlide->entity();
	addTitle(usagesInner, tr::lng_group_invite_usage_title());
	const auto usagesWrap = usagesInner->add(
		object_ptr<VerticalLayout>(usagesInner),
		style::margins(0, 0, 0, st::defaultVerticalListSkip));
	addDivider(usagesInner, tr::lng_group_invite_usage_about());

	static const auto addButton = [](
			not_null<VerticalLayout*> container,
			const std::shared_ptr<RadiobuttonGroup> &group,
			int value,
			const QString &text) {
		return container->add(
			object_ptr<Radiobutton>(
				container,
				group,
				value,
				text),
			st::inviteLinkLimitMargin);
	};

	const auto regenerate = [=] {
		expireGroup->setValue(state->expireValue);
		usageGroup->setValue(state->usageValue);

		auto expires = std::vector{ kMaxLimit, -kHour, -kDay, -kDay * 7, 0 };
		auto usages = std::vector{ kMaxLimit, 1, 10, 100, 0 };
		auto defaults = State();
		for (auto i = begin(expires); i != end(expires); ++i) {
			if (*i == state->expireValue) {
				break;
			} else if (*i == kMaxLimit) {
				continue;
			} else if (!*i || (now - *i >= state->expireValue)) {
				expires.insert(i, state->expireValue);
				break;
			}
		}
		for (auto i = begin(usages); i != end(usages); ++i) {
			if (*i == state->usageValue) {
				break;
			} else if (*i == kMaxLimit) {
				continue;
			} else if (!*i || *i > state->usageValue) {
				usages.insert(i, state->usageValue);
				break;
			}
		}
		state->expireButtons.clear();
		state->usageButtons.clear();
		for (const auto limit : expires) {
			const auto text = (limit == kMaxLimit)
				? tr::lng_group_invite_expire_never(tr::now)
				: !limit
				? tr::lng_group_invite_expire_custom(tr::now)
				: FormatExpireDate(limit);
			state->expireButtons.emplace(
				limit,
				addButton(expiresWrap, expireGroup, limit, text));
		}
		for (const auto limit : usages) {
			const auto text = (limit == kMaxLimit)
				? tr::lng_group_invite_usage_any(tr::now)
				: !limit
				? tr::lng_group_invite_usage_custom(tr::now)
				: Lang::FormatCountDecimal(limit);
			state->usageButtons.emplace(
				limit,
				addButton(usagesWrap, usageGroup, limit, text));
		}
	};

	const auto guard = MakeWeak(box);
	expireGroup->setChangedCallback([=](int value) {
		if (value) {
			state->expireValue = value;
			return;
		}
		expireGroup->setValue(state->expireValue);
		box->getDelegate()->show(Box([=](not_null<GenericBox*> box) {
			const auto save = [=](TimeId result) {
				if (!result) {
					return;
				}
				if (guard) {
					state->expireValue = result;
					regenerate();
				}
				box->closeBox();
			};
			const auto now = base::unixtime::now();
			const auto time = (state->expireValue == kMaxLimit)
				? (now + kDay)
				: (state->expireValue > now)
				? state->expireValue
				: (state->expireValue < 0)
				? (now - state->expireValue)
				: (now + kDay);
			ChooseDateTimeBox(box, {
				.title = tr::lng_group_invite_expire_after(),
				.submit = tr::lng_settings_save(),
				.done = save,
				.time = time,
			});
		}));
	});
	usageGroup->setChangedCallback([=](int value) {
		if (value) {
			state->usageValue = value;
			return;
		}
		usageGroup->setValue(state->usageValue);
		box->getDelegate()->show(Box([=](not_null<GenericBox*> box) {
			const auto height = st::boxPadding.bottom()
				+ st::defaultInputField.heightMin
				+ st::boxPadding.bottom();
			box->setTitle(tr::lng_group_invite_expire_after());
			const auto wrap = box->addRow(object_ptr<FixedHeightWidget>(
				box,
				height));
			const auto input = CreateChild<NumberInput>(
				wrap,
				st::defaultInputField,
				tr::lng_group_invite_custom_limit(),
				(state->usageValue == kMaxLimit
					? QString()
					: QString::number(state->usageValue)),
				200'000);
			wrap->widthValue(
			) | rpl::start_with_next([=](int width) {
				input->resize(width, input->height());
				input->moveToLeft(0, st::boxPadding.bottom());
			}, input->lifetime());
			box->setFocusCallback([=] {
				input->setFocusFast();
			});

			const auto save = [=] {
				const auto value = input->getLastText().toInt();
				if (value <= 0) {
					input->showError();
					return;
				}
				if (guard) {
					state->usageValue = value;
					regenerate();
				}
				box->closeBox();
			};
			QObject::connect(input, &NumberInput::submitted, save);
			box->addButton(tr::lng_settings_save(), save);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
	});

	regenerate();

	usagesSlide->toggleOn(state->requestApproval.value() | rpl::map(!_1));
	usagesSlide->finishAnimating();
}

void CreateInviteLinkBox(
		not_null<GenericBox*> box,
		Fn<InviteLinkSubscriptionToggle()> fillSubscription,
		bool isGroup,
		bool isPublic,
		Fn<void(InviteLinkFields)> done) {
	EditInviteLinkBox(
		box,
		std::move(fillSubscription),
		InviteLinkFields{ .isGroup = isGroup, .isPublic = isPublic },
		std::move(done));
}

} // namespace Ui

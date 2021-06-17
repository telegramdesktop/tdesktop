/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/edit_invite_link.h"

#include "lang/lang_keys.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "base/unixtime.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace Ui {
namespace {

constexpr auto kMaxLimit = std::numeric_limits<int>::max();
constexpr auto kHour = 3600;
constexpr auto kDay = 86400;

[[nodiscard]] QString FormatExpireDate(TimeId date) {
	if (date > 0) {
		return langDateTime(base::unixtime::parse(date));
	} else if (-date < kDay) {
		return tr::lng_group_call_duration_hours(
			tr::now,
			lt_count,
			(-date / kHour));
	} else if (-date < 7 * kDay) {
		return tr::lng_group_call_duration_days(
			tr::now,
			lt_count,
			(-date / kDay));
	} else {
		return tr::lng_local_storage_limit_weeks(
			tr::now,
			lt_count,
			(-date / (7 * kDay)));
	}
}

} // namespace

void EditInviteLinkBox(
		not_null<GenericBox*> box,
		const InviteLinkFields &data,
		Fn<void(InviteLinkFields)> done) {
	const auto link = data.link;
	box->setTitle(link.isEmpty()
		? tr::lng_group_invite_new_title()
		: tr::lng_group_invite_edit_title());

	const auto container = box->verticalLayout();
	const auto addTitle = [&](rpl::producer<QString> text) {
		container->add(
			object_ptr<FlatLabel>(
				container,
				std::move(text),
				st::settingsSubsectionTitle),
			st::settingsSubsectionTitlePadding);
	};
	const auto addDivider = [&](
			rpl::producer<QString> text,
			style::margins margins = style::margins()) {
		container->add(
			object_ptr<DividerLabel>(
				container,
				object_ptr<FlatLabel>(
					container,
					std::move(text),
					st::boxDividerLabel),
				st::settingsDividerLabelPadding),
			margins);
	};

	addTitle(tr::lng_group_invite_expire_title());
	const auto expiresWrap = container->add(
		object_ptr<VerticalLayout>(container),
		style::margins(0, 0, 0, st::settingsSectionSkip));
	addDivider(
		tr::lng_group_invite_expire_about(),
		style::margins(0, 0, 0, st::settingsSectionSkip));

	addTitle(tr::lng_group_invite_usage_title());
	const auto usagesWrap = container->add(
		object_ptr<VerticalLayout>(container),
		style::margins(0, 0, 0, st::settingsSectionSkip));
	addDivider(tr::lng_group_invite_usage_about());

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
	};
	const auto state = box->lifetime().make_state<State>(State{
		.expireValue = expire,
		.usageValue = usage
	});
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
				: QString("%L1").arg(limit);
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

	const auto &saveLabel = link.isEmpty()
		? tr::lng_formatting_link_create
		: tr::lng_settings_save;
	box->addButton(saveLabel(), [=] {
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
			.expireDate = expireDate,
			.usageLimit = usageLimit
		});
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void CreateInviteLinkBox(
		not_null<GenericBox*> box,
		Fn<void(InviteLinkFields)> done) {
	EditInviteLinkBox(box, InviteLinkFields(), std::move(done));
}

} // namespace Ui

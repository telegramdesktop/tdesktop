/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/self_destruction_box.h"

#include "api/api_authorizations.h"
#include "api/api_cloud_password.h"
#include "api/api_self_destruct.h"
#include "apiwrap.h"
#include "boxes/passcode_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace {

using Type = SelfDestructionBox::Type;

void AddDeleteAccount(
		not_null<Ui::BoxContent*> box,
		not_null<Main::Session*> session) {
	if (!session->isTestMode()) {
		return;
	}
	const auto maybeState = session->api().cloudPassword().stateCurrent();
	if (!maybeState || !maybeState->hasPassword) {
		return;
	}
	const auto top = box->addTopButton(st::infoTopBarMenu);
	const auto menu
		= top->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
	const auto handler = [=] {
		session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Core::CloudPasswordState &state) {
			auto fields = PasscodeBox::CloudFields::From(state);
			fields.customTitle = tr::lng_settings_destroy_title();
			fields.customDescription = tr::lng_context_mark_read_all_sure_2(
				tr::now,
				Ui::Text::RichLangValue).text;
			fields.customSubmitButton = tr::lng_theme_delete();
			fields.customCheckCallback = [=](
					const Core::CloudPasswordResult &result,
					QPointer<PasscodeBox> box) {
				session->api().request(MTPaccount_DeleteAccount(
					MTP_flags(MTPaccount_DeleteAccount::Flag::f_password),
					MTP_string("Manual"),
					result.result
				)).done([=] {
					if (box) {
						box->uiShow()->hideLayer();
					}
				}).fail([=](const MTP::Error &error) {
					if (box) {
						box->handleCustomCheckError(error.type());
					}
				}).send();
			};
			box->uiShow()->showBox(Box<PasscodeBox>(session, fields));
		}, top->lifetime());
	};
	top->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			top,
			st::popupMenuWithIcons);

		const auto addAction = Ui::Menu::CreateAddActionCallback(menu->get());
		addAction({
			.text = tr::lng_settings_destroy_title(tr::now),
			.handler = handler,
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
		(*menu)->popup(QCursor::pos());
	});
}

[[nodiscard]] std::vector<int> Values(Type type) {
	switch (type) {
	case Type::Account: return { 30, 90, 180, 365, 548, 720 };
	case Type::Sessions: return { 7, 30, 90, 180, 365 };
	}
	Unexpected("SelfDestructionBox::Type in Values.");
}

} // namespace

SelfDestructionBox::SelfDestructionBox(
	QWidget*,
	not_null<Main::Session*> session,
	Type type,
	rpl::producer<int> preloaded)
: _type(type)
, _session(session)
, _ttlValues(Values(type))
, _loading(
		this,
		tr::lng_contacts_loading(tr::now),
		st::membersAbout) {
	std::move(
		preloaded
	) | rpl::take(
		1
	) | rpl::start_with_next([=](int days) {
		gotCurrent(days);
	}, lifetime());
}

void SelfDestructionBox::gotCurrent(int days) {
	Expects(!_ttlValues.empty());

	_loading.destroy();

	auto daysAdjusted = _ttlValues[0];
	for (const auto value : _ttlValues) {
		if (qAbs(days - value) < qAbs(days - daysAdjusted)) {
			daysAdjusted = value;
		}
	}
	_ttlGroup = std::make_shared<Ui::RadiobuttonGroup>(daysAdjusted);

	if (_prepared) {
		showContent();
	}
}

void SelfDestructionBox::showContent() {
	auto y = st::boxOptionListPadding.top();
	_description.create(
		this,
		(_type == Type::Account
			? tr::lng_self_destruct_description(tr::now)
			: tr::lng_self_destruct_sessions_description(tr::now)),
		st::boxLabel);
	_description->moveToLeft(st::boxPadding.left(), y);
	y += _description->height() + st::boxMediumSkip;

	for (const auto value : _ttlValues) {
		const auto button = Ui::CreateChild<Ui::Radiobutton>(
			this,
			_ttlGroup,
			value,
			DaysLabel(value),
			st::autolockButton);
		button->moveToLeft(st::boxPadding.left(), y);
		y += button->heightNoMargins() + st::boxOptionListSkip;
	}
	showChildren();

	clearButtons();
	addButton(tr::lng_settings_save(), [=] {
		const auto value = _ttlGroup->current();
		switch (_type) {
		case Type::Account:
			_session->api().selfDestruct().updateAccountTTL(value);
			break;
		case Type::Sessions:
			_session->api().authorizations().updateTTL(value);
			break;
		}

		closeBox();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

QString SelfDestructionBox::DaysLabel(int days) {
	return !days
		? QString()
		//: (days > 364)
		//? tr::lng_years(tr::now, lt_count, days / 365)
		: (days > 25)
		? tr::lng_months(tr::now, lt_count, std::max(days / 30, 1))
		: tr::lng_weeks(tr::now, lt_count, std::max(days / 7, 1));
}

void SelfDestructionBox::prepare() {
	setTitle((_type == Type::Account
		? tr::lng_self_destruct_title()
		: tr::lng_self_destruct_sessions_title()));

	auto fake = object_ptr<Ui::FlatLabel>(
		this,
		(_type == Type::Account
			? tr::lng_self_destruct_description(tr::now)
			: tr::lng_self_destruct_sessions_description(tr::now)),
		st::boxLabel);
	const auto boxHeight = st::boxOptionListPadding.top()
		+ fake->height() + st::boxMediumSkip
		+ (_ttlValues.size()
			* (st::defaultRadio.diameter + st::boxOptionListSkip))
		- st::boxOptionListSkip
		+ st::boxOptionListPadding.bottom() + st::boxPadding.bottom();
	fake.destroy();

	setDimensions(st::boxWidth, boxHeight);

	addButton(tr::lng_cancel(), [this] { closeBox(); });

	if (_loading) {
		_loading->moveToLeft(
			(st::boxWidth - _loading->width()) / 2,
			boxHeight / 3);
		_prepared = true;
	} else {
		showContent();
	}

	AddDeleteAccount(this, _session);
}

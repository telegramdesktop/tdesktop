/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/self_destruction_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "apiwrap.h"
#include "api/api_self_destruct.h"
#include "api/api_authorizations.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

using Type = SelfDestructionBox::Type;

[[nodiscard]] std::vector<int> Values(Type type) {
	switch (type) {
	case Type::Account: return { 30, 90, 180, 365 };
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
		: (days > 364)
		? tr::lng_years(tr::now, lt_count, days / 365)
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
}

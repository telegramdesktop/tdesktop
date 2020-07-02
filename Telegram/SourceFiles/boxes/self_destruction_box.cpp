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
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

SelfDestructionBox::SelfDestructionBox(
	QWidget*,
	not_null<Main::Session*> session,
	rpl::producer<int> preloaded)
: _session(session)
, _ttlValues{ 30, 90, 180, 365 }
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
		tr::lng_self_destruct_description(tr::now),
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
		_session->api().selfDestruct().update(_ttlGroup->value());
		closeBox();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

QString SelfDestructionBox::DaysLabel(int days) {
	return (days > 364)
		? tr::lng_self_destruct_years(tr::now, lt_count, days / 365)
		: tr::lng_self_destruct_months(tr::now, lt_count, qMax(days / 30, 1));
}

void SelfDestructionBox::prepare() {
	setTitle(tr::lng_self_destruct_title());

	auto fake = object_ptr<Ui::FlatLabel>(
		this,
		tr::lng_self_destruct_description(tr::now),
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

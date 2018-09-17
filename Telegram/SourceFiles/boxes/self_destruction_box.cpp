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
#include "auth_session.h"
#include "styles/style_boxes.h"

SelfDestructionBox::SelfDestructionBox(
	QWidget*,
	rpl::producer<int> preloaded)
: _ttlValues{ 30, 90, 180, 365 }
, _loading(
		this,
		lang(lng_contacts_loading),
		Ui::FlatLabel::InitType::Simple,
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
		lang(lng_self_destruct_description),
		Ui::FlatLabel::InitType::Simple,
		st::boxLabel);
	_description->moveToLeft(st::boxPadding.left(), y);
	y += _description->height() + st::boxMediumSkip;

	const auto count = int(_ttlValues.size());
	for (const auto value : _ttlValues) {
		const auto button = Ui::CreateChild<Ui::Radiobutton>(
			this,
			_ttlGroup,
			value,
			DaysLabel(value),
			st::langsButton);
		button->moveToLeft(st::boxPadding.left(), y);
		y += button->heightNoMargins() + st::boxOptionListSkip;
	}
	showChildren();

	clearButtons();
	addButton(langFactory(lng_settings_save), [=] {
		Auth().api().saveSelfDestruct(_ttlGroup->value());
		closeBox();
	});
	addButton(langFactory(lng_cancel), [=] { closeBox(); });
}

QString SelfDestructionBox::DaysLabel(int days) {
	return (days > 364)
		? lng_self_destruct_years(lt_count, days / 365)
		: lng_self_destruct_months(lt_count, qMax(days / 30, 1));
}

void SelfDestructionBox::prepare() {
	setTitle(langFactory(lng_self_destruct_title));

	auto fake = object_ptr<Ui::FlatLabel>(
		this,
		lang(lng_self_destruct_description),
		Ui::FlatLabel::InitType::Simple,
		st::boxLabel);
	const auto boxHeight = st::boxOptionListPadding.top()
		+ fake->height() + st::boxMediumSkip
		+ (_ttlValues.size()
			* (st::defaultRadio.diameter + st::boxOptionListSkip))
		- st::boxOptionListSkip
		+ st::boxOptionListPadding.bottom() + st::boxPadding.bottom();
	fake.destroy();

	setDimensions(st::boxWidth, boxHeight);

	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	if (_loading) {
		_loading->moveToLeft(
			(st::boxWidth - _loading->width()) / 2,
			boxHeight / 3);
		_prepared = true;
	} else {
		showContent();
	}
}

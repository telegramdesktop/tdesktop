/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/auto_lock_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "core/application.h"
#include "mainwindow.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

AutoLockBox::AutoLockBox(QWidget*, not_null<Main::Session*> session)
: _session(session) {
}

void AutoLockBox::prepare() {
	setTitle(tr::lng_passcode_autolock());

	addButton(tr::lng_box_ok(), [this] { closeBox(); });

	auto options = { 60, 300, 3600, 18000 };

	auto group = std::make_shared<Ui::RadiobuttonGroup>(
		Core::App().settings().autoLock());
	auto y = st::boxOptionListPadding.top() + st::autolockButton.margin.top();
	auto count = int(options.size());
	_options.reserve(count);
	for (auto seconds : options) {
		_options.emplace_back(this, group, seconds, (seconds % 3600) ? tr::lng_passcode_autolock_minutes(tr::now, lt_count, seconds / 60) : tr::lng_passcode_autolock_hours(tr::now, lt_count, seconds / 3600), st::autolockButton);
		_options.back()->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _options.back()->heightNoMargins() + st::boxOptionListSkip;
	}
	group->setChangedCallback([this](int value) { durationChanged(value); });

	setDimensions(st::autolockWidth, st::boxOptionListPadding.top() + count * _options.back()->heightNoMargins() + (count - 1) * st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::boxPadding.bottom());
}

void AutoLockBox::durationChanged(int seconds) {
	Core::App().settings().setAutoLock(seconds);
	Core::App().saveSettingsDelayed();

	Core::App().checkAutoLock(crl::now());
	closeBox();
}

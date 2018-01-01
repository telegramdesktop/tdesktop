/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/autolock_box.h"

#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_boxes.h"

void AutoLockBox::prepare() {
	setTitle(langFactory(lng_passcode_autolock));

	addButton(langFactory(lng_box_ok), [this] { closeBox(); });

	auto options = { 60, 300, 3600, 18000 };

	auto group = std::make_shared<Ui::RadiobuttonGroup>(Global::AutoLock());
	auto y = st::boxOptionListPadding.top() + st::langsButton.margin.top();
	auto count = int(options.size());
	_options.reserve(count);
	for (auto seconds : options) {
		_options.emplace_back(this, group, seconds, (seconds % 3600) ? lng_passcode_autolock_minutes(lt_count, seconds / 60) : lng_passcode_autolock_hours(lt_count, seconds / 3600), st::langsButton);
		_options.back()->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _options.back()->heightNoMargins() + st::boxOptionListSkip;
	}
	group->setChangedCallback([this](int value) { durationChanged(value); });

	setDimensions(st::langsWidth, st::boxOptionListPadding.top() + count * _options.back()->heightNoMargins() + (count - 1) * st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::boxPadding.bottom());
}

void AutoLockBox::durationChanged(int seconds) {
	Global::SetAutoLock(seconds);
	Local::writeUserSettings();
	Global::RefLocalPasscodeChanged().notify();

	Auth().checkAutoLock();
	closeBox();
}

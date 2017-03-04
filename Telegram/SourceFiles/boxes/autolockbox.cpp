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
#include "boxes/autolockbox.h"

#include "lang.h"
#include "localstorage.h"
#include "boxes/confirmbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"

void AutoLockBox::prepare() {
	setTitle(lang(lng_passcode_autolock));

	addButton(lang(lng_box_ok), [this] { closeBox(); });

	int opts[] = { 60, 300, 3600, 18000 }, cnt = sizeof(opts) / sizeof(opts[0]);
	auto y = st::boxOptionListPadding.top();
	_options.reserve(cnt);
	for (auto i = 0; i != cnt; ++i) {
		auto v = opts[i];
		_options.push_back(new Ui::Radiobutton(this, qsl("autolock"), v, (v % 3600) ? lng_passcode_autolock_minutes(lt_count, v / 60) : lng_passcode_autolock_hours(lt_count, v / 3600), (Global::AutoLock() == v), st::langsButton));
		_options.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _options.back()->heightNoMargins() + st::boxOptionListSkip;
		connect(_options.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}

	setDimensions(st::langsWidth, st::boxOptionListPadding.top() + cnt * st::langsButton.height + (cnt - 1) * st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::boxPadding.bottom());
}

void AutoLockBox::onChange() {
	if (!isBoxShown()) return;

	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		int32 v = _options[i]->val();
		if (_options[i]->checked()) {
			Global::SetAutoLock(v);
			Local::writeUserSettings();
			Global::RefLocalPasscodeChanged().notify();
		}
	}
	App::wnd()->checkAutoLock();
	closeBox();
}

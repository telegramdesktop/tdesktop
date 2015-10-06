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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "localstorage.h"

#include "autolockbox.h"
#include "confirmbox.h"
#include "mainwidget.h"
#include "window.h"

AutoLockBox::AutoLockBox() :
_close(this, lang(lng_box_ok), st::defaultBoxButton) {

	bool haveTestLang = (cLang() == languageTest);

	int32 opts[] = { 60, 300, 3600, 18000 }, cnt = sizeof(opts) / sizeof(opts[0]);

	resizeMaxHeight(st::langsWidth, st::boxTitleHeight + cnt * (st::boxOptionListPadding.top() + st::langsButton.height) + st::boxOptionListPadding.bottom() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _close.height() + st::boxButtonPadding.bottom());

	int32 y = st::boxTitleHeight + st::boxOptionListPadding.top();
	_options.reserve(cnt);
	for (int32 i = 0; i < cnt; ++i) {
		int32 v = opts[i];
		_options.push_back(new Radiobutton(this, qsl("autolock"), v, (v % 3600) ? lng_passcode_autolock_minutes(lt_count, v / 60) : lng_passcode_autolock_hours(lt_count, v / 3600), (cAutoLock() == v), st::langsButton));
		_options.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _options.back()->height() + st::boxOptionListPadding.top();
		connect(_options.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));

	_close.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _close.height());
	prepare();
}

void AutoLockBox::hideAll() {
	_close.hide();
	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		_options[i]->hide();
	}
}

void AutoLockBox::showAll() {
	_close.show();
	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		_options[i]->show();
	}
}

void AutoLockBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_passcode_autolock));
}

void AutoLockBox::onChange() {
	if (isHidden()) return;

	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		int32 v = _options[i]->val();
		if (_options[i]->checked()) {
			cSetAutoLock(v);
			Local::writeUserSettings();
		}
	}
	App::wnd()->checkAutoLock();
	onClose();
}

AutoLockBox::~AutoLockBox() {
	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		delete _options[i];
	}
}

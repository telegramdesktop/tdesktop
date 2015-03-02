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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "localstorage.h"

#include "autolockbox.h"
#include "confirmbox.h"
#include "mainwidget.h"
#include "window.h"

AutoLockBox::AutoLockBox() :
_done(this, lang(lng_about_done), st::langsCloseButton),
_hiding(false), a_opacity(0, 1) {

	bool haveTestLang = (cLang() == languageTest);

	int32 opts[] = { 60, 300, 3600, 18000 }, cnt = sizeof(opts) / sizeof(opts[0]);

	_width = st::langsWidth;
	_height = st::addContactTitleHeight + st::langsPadding.top() + st::langsPadding.bottom() + cnt * (st::langPadding.top() + st::rbDefFlat.height + st::langPadding.bottom()) + _done.height();

	int32 y = st::addContactTitleHeight + st::langsPadding.top();
	_options.reserve(cnt);
	for (int32 i = 0; i < cnt; ++i) {
		int32 v = opts[i];
		_options.push_back(new FlatRadiobutton(this, qsl("autolock"), v, (v % 3600) ? lng_passcode_autolock_minutes(lt_count, v / 60) : lng_passcode_autolock_hours(lt_count, v / 3600), (cAutoLock() == v), st::langButton));
		_options.back()->move(st::langsPadding.left() + st::langPadding.left(), y + st::langPadding.top());
		y += st::langPadding.top() + _options.back()->height() + st::langPadding.bottom();
		connect(_options.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}

	_done.move(0, _height - _done.height());

	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void AutoLockBox::hideAll() {
	_done.hide();
	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		_options[i]->hide();
	}
}

void AutoLockBox::showAll() {
	_done.show();
	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		_options[i]->show();
	}
}

void AutoLockBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	}
}

void AutoLockBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void AutoLockBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(0, 0, _width, _height, st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);

			// draw box title / text
			p.setFont(st::addContactTitleFont->f);
			p.setPen(st::black->p);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_languages));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void AutoLockBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			setFocus();
		}
	} else {
		a_opacity.update(ms, anim::linear);
	}
	update();
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

void AutoLockBox::onClose() {
	emit closed();
}

void AutoLockBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

AutoLockBox::~AutoLockBox() {
	for (int32 i = 0, l = _options.size(); i < l; ++i) {
		delete _options[i];
	}
}

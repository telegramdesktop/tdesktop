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

#include "passcodebox.h"
#include "window.h"

#include "localstorage.h"

PasscodeBox::PasscodeBox(bool turningOff) : _turningOff(turningOff),
_about(st::addContactWidth - st::addContactPadding.left() - st::addContactPadding.right()),
_saveButton(this, lang(lng_settings_save), st::btnSelectDone),
_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
_oldPasscode(this, st::inpAddContact, lang(lng_passcode_enter_old)),
_newPasscode(this, st::inpAddContact, lang(lng_passcode_enter_new)),
_reenterPasscode(this, st::inpAddContact, lang(lng_passcode_confirm_new)),
a_opacity(0, 1), _hiding(false) {

	_width = st::addContactWidth;
	_about.setRichText(st::usernameFont, lang(lng_passcode_about));
	int32 aboutHeight = _about.countHeight(_width - st::addContactPadding.left() - st::addContactPadding.right());
	_oldPasscode.setEchoMode(QLineEdit::Password);
	_newPasscode.setEchoMode(QLineEdit::Password);
	_reenterPasscode.setEchoMode(QLineEdit::Password);
	if (turningOff) {
		_oldPasscode.show();
		_boxTitle = lang(lng_passcode_remove);
		_height = st::addContactTitleHeight + st::addContactPadding.top() + 1 * _oldPasscode.height() + st::usernameSkip + aboutHeight + st::addContactPadding.bottom() + _saveButton.height();
	} else {
		if (cHasPasscode()) {
			_oldPasscode.show();
			_boxTitle = lang(lng_passcode_change);
			_height = st::addContactTitleHeight + st::addContactPadding.top() + 3 * _oldPasscode.height() + st::usernameSkip * 2 + 1 * st::addContactDelta + aboutHeight + st::addContactPadding.bottom() + _saveButton.height();
		} else {
			_oldPasscode.hide();
			_boxTitle = lang(lng_passcode_create);
			_height = st::addContactTitleHeight + st::addContactPadding.top() + 2 * _oldPasscode.height() + st::usernameSkip + 1 * st::addContactDelta + aboutHeight + st::addContactPadding.bottom() + _saveButton.height();
		}
	}

	_oldPasscode.setGeometry(st::addContactPadding.left(), st::addContactTitleHeight + st::addContactPadding.top(), _width - st::addContactPadding.left() - st::addContactPadding.right(), _oldPasscode.height());
	_newPasscode.setGeometry(st::addContactPadding.left(), _oldPasscode.y() + ((turningOff || cHasPasscode()) ? (_oldPasscode.height() + st::usernameSkip) : 0), _oldPasscode.width(), _oldPasscode.height());
	_reenterPasscode.setGeometry(st::addContactPadding.left(), _newPasscode.y() + _newPasscode.height() + st::addContactDelta, _newPasscode.width(), _newPasscode.height());

	int32 buttonTop = _height - _cancelButton.height();
	_cancelButton.move(0, buttonTop);
	_saveButton.move(_width - _saveButton.width(), buttonTop);

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));
	
	_badOldTimer.setSingleShot(true);
	connect(&_badOldTimer, SIGNAL(timeout()), this, SLOT(onBadOldPasscode()));

	connect(&_oldPasscode, SIGNAL(changed()), this, SLOT(onOldChanged()));
	connect(&_newPasscode, SIGNAL(changed()), this, SLOT(onNewChanged()));
	connect(&_reenterPasscode, SIGNAL(changed()), this, SLOT(onNewChanged()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void PasscodeBox::hideAll() {
	_oldPasscode.hide();
	_newPasscode.hide();
	_reenterPasscode.hide();
	_saveButton.hide();
	_cancelButton.hide();
}

void PasscodeBox::showAll() {
	if (_turningOff) {
		_oldPasscode.show();
		_newPasscode.hide();
		_reenterPasscode.hide();
	} else {
		if (cHasPasscode()) {
			_oldPasscode.show();
		} else {
			_oldPasscode.hide();
		}
		_newPasscode.show();
		_reenterPasscode.show();
	}
	_saveButton.show();
	_cancelButton.show();
}

void PasscodeBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_oldPasscode.hasFocus()) {
			if (_turningOff) {
				onSave();
			} else {
				_newPasscode.setFocus();
			}
		} else if (_newPasscode.hasFocus()) {
			_reenterPasscode.setFocus();
		} else if (_reenterPasscode.hasFocus()) {
			if (cHasPasscode() && _oldPasscode.text().isEmpty()) {
				_oldPasscode.setFocus();
				_oldPasscode.notaBene();
			} else if (_newPasscode.text().isEmpty()) {
				_newPasscode.setFocus();
				_newPasscode.notaBene();
			} else if (_reenterPasscode.text().isEmpty()) {
				_reenterPasscode.notaBene();
			} else {
				onSave();
			}
		}
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void PasscodeBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void PasscodeBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);
			p.fillRect(0, size().height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			p.setPen(st::usernameColor->p);
			_about.draw(p, st::addContactPadding.left(), (_turningOff ? _oldPasscode : _reenterPasscode).y() + _oldPasscode.height() + st::usernameSkip, _width - st::addContactPadding.left() - st::addContactPadding.right());

			if (!_oldError.isEmpty()) {
				p.setPen(st::setErrColor->p);
				p.drawText(QRect(0, _oldPasscode.y() + _oldPasscode.height(), _width, st::usernameSkip), _oldError, style::al_center);
			}

			if (!_newError.isEmpty()) {
				p.setPen(st::setErrColor->p);
				p.drawText(QRect(0, _reenterPasscode.y() + _reenterPasscode.height(), _width, st::usernameSkip), _newError, style::al_center);
			}

			// paint button sep
			p.fillRect(st::btnSelectCancel.width, size().height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

			// draw box title / text
			p.setPen(st::black->p);
			p.setFont(st::addContactTitleFont->f);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, _boxTitle);
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void PasscodeBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			if (_oldPasscode.isHidden()) {
				_newPasscode.setFocus();
			} else {
				_oldPasscode.setFocus();
			}
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
}

void PasscodeBox::onSave() {
	QString old = _oldPasscode.text(), pwd = _newPasscode.text(), conf = _reenterPasscode.text();
	if (_turningOff || cHasPasscode()) {
		if (Local::checkPasscode(old.toUtf8())) {
			if (_turningOff) pwd = conf = QString();
		} else {
			_oldPasscode.setDisabled(true);
			_newPasscode.setDisabled(true);
			_reenterPasscode.setDisabled(true);
			_saveButton.setDisabled(true);
			_oldError = QString();
			update();
			_badOldTimer.start(WrongPasscodeTimeout);
			return;
		}
	}
	if (!_turningOff && pwd.isEmpty()) {
		_newPasscode.setFocus();
		_newPasscode.notaBene();
		return;
	}
	if (pwd != conf) {
		_reenterPasscode.setFocus();
		_reenterPasscode.notaBene();
		if (!conf.isEmpty()) {
			_newError = lang(lng_passcode_differ);
			update();
		}
	} else if (!_turningOff && cHasPasscode() && old == pwd) {
		_newPasscode.setFocus();
		_newPasscode.notaBene();
		_newError = lang(lng_passcode_is_same);
		update();
	} else {
		Local::setPasscode(pwd.toUtf8());
		App::wnd()->checkAutoLock();
		App::wnd()->getTitle()->showUpdateBtn();
		emit closed();
	}
}

void PasscodeBox::onBadOldPasscode() {
	_oldPasscode.setDisabled(false);
	_newPasscode.setDisabled(false);
	_reenterPasscode.setDisabled(false);
	_saveButton.setDisabled(false);
	_oldPasscode.selectAll();
	_oldPasscode.setFocus();
	_oldPasscode.notaBene();
	_oldError = lang(lng_passcode_wrong);
	update();
}

void PasscodeBox::onOldChanged() {
	if (!_oldError.isEmpty()) {
		_oldError = QString();
		update();
	}
}

void PasscodeBox::onNewChanged() {
	if (!_newError.isEmpty()) {
		_newError = QString();
		update();
	}
}

void PasscodeBox::onCancel() {
	emit closed();
}

void PasscodeBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

PasscodeBox::~PasscodeBox() {
}

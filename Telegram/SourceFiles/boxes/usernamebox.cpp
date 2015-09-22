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

#include "application.h"
#include "usernamebox.h"
#include "mainwidget.h"
#include "window.h"

UsernameBox::UsernameBox() :
_saveButton(this, lang(lng_settings_save), st::usernameDone),
_cancelButton(this, lang(lng_cancel), st::usernameCancel),
_usernameInput(this, st::inpAddContact, qsl("@username"), App::self()->username),
_saveRequest(0), _checkRequest(0), _about(st::usernameWidth - 2 * st::boxTitlePos.x())  {
	_about.setRichText(st::usernameFont, lang(lng_username_about));
	_goodText = App::self()->username.isEmpty() ? QString() : lang(lng_username_available);
	initBox();
}

void UsernameBox::initBox() {
	resizeMaxHeight(st::usernameWidth, st::boxTitleHeight + st::addContactPadding.top() + _usernameInput.height() + st::addContactPadding.bottom() + _about.countHeight(st::usernameWidth - 2 * st::boxTitlePos.x()) + st::usernameSkip + _saveButton.height());

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_usernameInput, SIGNAL(changed()), this, SLOT(onChanged()));

	_checkTimer.setSingleShot(true);
	connect(&_checkTimer, SIGNAL(timeout()), this, SLOT(onCheck()));

	prepare();
}

void UsernameBox::hideAll() {
	_usernameInput.hide();
	_saveButton.hide();
	_cancelButton.hide();
}

void UsernameBox::showAll() {
	_usernameInput.show();
	_saveButton.show();
	_cancelButton.show();
}

void UsernameBox::showDone() {
	_usernameInput.setFocus();
}

void UsernameBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onSave();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void UsernameBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_username_title), true);

	// paint shadow
	p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::usernameCancel.width, size().height() - st::usernameCancel.height, st::lineWidth, st::usernameCancel.height, st::btnSelectSep->b);

	if (!_errorText.isEmpty()) {
		p.setPen(st::setErrColor->p);
		p.setFont(st::setErrFont->f);
		int32 w = st::setErrFont->m.width(_errorText);
		p.drawText((width() - w) / 2, _usernameInput.y() + _usernameInput.height() + ((st::usernameSkip - st::setErrFont->height) / 2) + st::setErrFont->ascent, _errorText);
	} else if (!_goodText.isEmpty()) {
		p.setPen(st::setGoodColor->p);
		p.setFont(st::setErrFont->f);
		int32 w = st::setErrFont->m.width(_goodText);
		p.drawText((width() - w) / 2, _usernameInput.y() + _usernameInput.height() + ((st::usernameSkip - st::setErrFont->height) / 2) + st::setErrFont->ascent, _goodText);
	}
	p.setPen(st::usernameColor->p);
	_about.draw(p, st::boxTitlePos.x(), _usernameInput.y() + _usernameInput.height() + st::usernameSkip, width() - 2 * st::boxTitlePos.x());
}

void UsernameBox::resizeEvent(QResizeEvent *e) {
	_usernameInput.setGeometry(st::addContactPadding.left(), st::boxTitleHeight + st::addContactPadding.top(), width() - st::addContactPadding.left() - st::addContactPadding.right(), _usernameInput.height());

	int32 buttonTop = height() - _cancelButton.height();
	_cancelButton.move(0, buttonTop);
	_saveButton.move(width() - _saveButton.width(), buttonTop);
}

void UsernameBox::onSave() {
	if (_saveRequest) return;

	_sentUsername = getName();
	_saveRequest = MTP::send(MTPaccount_UpdateUsername(MTP_string(_sentUsername)), rpcDone(&UsernameBox::onUpdateDone), rpcFail(&UsernameBox::onUpdateFail));
}

void UsernameBox::onCheck() {
	if (_checkRequest) {
		MTP::cancel(_checkRequest);
	}
	QString name = getName();
	if (name.size() >= MinUsernameLength) {
		_checkUsername = name;
		_checkRequest = MTP::send(MTPaccount_CheckUsername(MTP_string(name)), rpcDone(&UsernameBox::onCheckDone), rpcFail(&UsernameBox::onCheckFail));
	}
}

void UsernameBox::onChanged() {
	QString name = getName();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer.stop();
	} else {
		int32 i, len = name.size();
		for (int32 i = 0; i < len; ++i) {
			QChar ch = name.at(i);
			if ((ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_' && (ch != '@' || i > 0)) {
				if (_errorText != lang(lng_username_bad_symbols)) {
					_errorText = lang(lng_username_bad_symbols);
					update();
				}
				_checkTimer.stop();
				return;
			}
		}
		if (name.size() < MinUsernameLength) {
			if (_errorText != lang(lng_username_too_short)) {
				_errorText = lang(lng_username_too_short);
				update();
			}
			_checkTimer.stop();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer.start(UsernameCheckTimeout);
		}
	}
}

void UsernameBox::onUpdateDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(1, user));
	emit closed();
}

bool UsernameBox::onUpdateFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_saveRequest = 0;
	QString err(error.type());
	if (err == "USERNAME_NOT_MODIFIED" || _sentUsername == App::self()->username) {
		App::self()->setName(textOneLine(App::self()->firstName), textOneLine(App::self()->lastName), textOneLine(App::self()->nameOrPhone), textOneLine(_sentUsername));
		emit closed();
		return true;
	} else if (err == "USERNAME_INVALID") {
		_usernameInput.setFocus();
		_usernameInput.notaBene();
		_errorText = lang(lng_username_invalid);
		return true;
	} else if (err == "USERNAME_OCCUPIED" || err == "USERNAMES_UNAVAILABLE") {
		_usernameInput.setFocus();
		_usernameInput.notaBene();
		_errorText = lang(lng_username_occupied);
		return true;
	}
	_usernameInput.setFocus();
	return true;
}

void UsernameBox::onCheckDone(const MTPBool &result) {
	_checkRequest = 0;
	QString newError = (result.v || _checkUsername == App::self()->username) ? QString() : lang(lng_username_occupied);
	QString newGood = newError.isEmpty() ? lang(lng_username_available) : QString();
	if (_errorText != newError || _goodText != newGood) {
		_errorText = newError;
		_goodText = newGood;
		update();
	}
}

bool UsernameBox::onCheckFail(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_checkRequest = 0;
	QString err(error.type());
	if (err == "USERNAME_INVALID") {
		_errorText = lang(lng_username_invalid);
		update();
		return true;
	} else if (err == "USERNAME_OCCUPIED" && _checkUsername != App::self()->username) {
		_errorText = lang(lng_username_occupied);
		update();
		return true;
	}
	_goodText = QString();
	_usernameInput.setFocus();
	return true;
}

QString UsernameBox::getName() const {
	return _usernameInput.text().replace('@', QString()).trimmed();
}

/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "lang.h"

#include "application.h"
#include "usernamebox.h"
#include "mainwidget.h"
#include "window.h"

UsernameInput::UsernameInput(QWidget *parent, const style::flatInput &st, const QString &ph, const QString &val) : FlatInput(parent, st, ph, val) {
}

void UsernameInput::correctValue(QKeyEvent *e, const QString &was) {
	QString oldText(text()), newText;
	int32 oldPos(cursorPosition()), newPos(-1), oldLen(oldText.length());
	newText.reserve(oldLen);

	for (int32 i = 0; i < oldLen; ++i) {
		if (i == oldPos) {
			newPos = newText.length();
		}

		QChar ch = oldText[i];
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || (ch == '@' && !i)) {
			if (newText.size() < MaxUsernameLength) {
				newText.append(ch);
			}
		}
	}
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != oldText) {
		setText(newText);
		setCursorPosition(newPos);
	}
}

UsernameBox::UsernameBox() :
_saveButton(this, lang(lng_settings_save), st::usernameDone),
_cancelButton(this, lang(lng_cancel), st::usernameCancel),
_usernameInput(this, st::inpAddContact, qsl("@username"), App::self()->username),
_saveRequest(0), _checkRequest(0), _about(st::usernameWidth - 2 * st::addContactTitlePos.x()),
a_opacity(0, 1), _hiding(false) {
	_about.setRichText(st::usernameFont, lang(lng_username_about));
	initBox();
}

void UsernameBox::initBox() {
	_width = st::usernameWidth;
	_height = st::addContactTitleHeight + st::addContactPadding.top() + _usernameInput.height() + st::addContactPadding.bottom() + _about.countHeight(st::usernameWidth - 2 * st::addContactTitlePos.x()) + st::usernameSkip + _saveButton.height();
	_usernameInput.setGeometry(st::addContactPadding.left(), st::addContactTitleHeight + st::addContactPadding.top(), _width - st::addContactPadding.left() - st::addContactPadding.right(), _usernameInput.height());

	int32 buttonTop = _height - _cancelButton.height();
	_cancelButton.move(0, buttonTop);
	_saveButton.move(_width - _saveButton.width(), buttonTop);

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));
	connect(&_usernameInput, SIGNAL(changed()), this, SLOT(onChanged()));

	_checkTimer.setSingleShot(true);
	connect(&_checkTimer, SIGNAL(timeout()), this, SLOT(onCheck()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
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

void UsernameBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onSave();
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void UsernameBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void UsernameBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);
			p.fillRect(0, size().height() - st::usernameCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.fillRect(st::usernameCancel.width, size().height() - st::usernameCancel.height, st::lineWidth, st::usernameCancel.height, st::btnSelectSep->b);

			// draw box title / text
			p.setPen(st::black->p);
			p.setFont(st::addContactTitleFont->f);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_username_title));

			if (!_errorText.isEmpty()) {
				p.setPen(st::setErrColor->p);
				p.setFont(st::setErrFont->f);
				int32 w = st::setErrFont->m.width(_errorText);
				p.drawText((_width - w) / 2, _usernameInput.y() + _usernameInput.height() + ((st::usernameSkip - st::setErrFont->height) / 2) + st::setErrFont->ascent, _errorText);
			}
			p.setPen(st::usernameColor->p);
			_about.draw(p, st::addContactTitlePos.x(), _usernameInput.y() + _usernameInput.height() + st::usernameSkip, width() - 2 * st::addContactTitlePos.x());
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void UsernameBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			_usernameInput.setFocus();
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
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
		_checkRequest = MTP::send(MTPaccount_CheckUsername(MTP_string(name)), rpcDone(&UsernameBox::onCheckDone), rpcFail(&UsernameBox::onCheckFail));
	}
}

void UsernameBox::onChanged() {
	QString name = getName();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty()) {
			_errorText = QString();
			update();
		}
		_checkTimer.stop();
	} else if (name.size() < MinUsernameLength) {
		if (_errorText != lang(lng_username_too_short)) {
			_errorText = lang(lng_username_too_short);
			update();
		}
		_checkTimer.stop();
	} else {
		if (!_errorText.isEmpty()) {
			_errorText = QString();
			update();
		}
		_checkTimer.start(UsernameCheckTimeout);
	}
}

void UsernameBox::onUpdateDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(QVector<MTPUser>(1, user)));
	emit closed();
}

bool UsernameBox::onUpdateFail(const RPCError &error) {
	_saveRequest = 0;
	QString err(error.type()), name = getName();
	if (err == "USERNAME_NOT_MODIFIED" || _sentUsername == textOneLine(name)) {
		App::self()->setName(textOneLine(App::self()->firstName), textOneLine(App::self()->lastName), textOneLine(App::self()->nameOrPhone), textOneLine(name));
		emit closed();
		return true;
	} else if (err == "USERNAME_INVALID") {
		_usernameInput.setFocus();
		_usernameInput.notaBene();
		_errorText = lang(lng_username_invalid);
		return true;
	} else if (err == "USERNAME_OCCUPIED") {
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
	QString newError = result.v ? QString() : lang(lng_username_occupied);
	if (_errorText != newError) {
		_errorText = newError;
		update();
	}
}

bool UsernameBox::onCheckFail(const RPCError &error) {
	_checkRequest = 0;
	QString err(error.type());
	if (err == "USERNAME_INVALID") {
		_errorText = lang(lng_username_invalid);
		update();
		return true;
	} else if (err == "USERNAME_OCCUPIED") {
		_errorText = lang(lng_username_occupied);
		update();
		return true;
	}
	_usernameInput.setFocus();
	return true;
}

QString UsernameBox::getName() const {
	return _usernameInput.text().replace('@', QString()).trimmed();
}

void UsernameBox::onCancel() {
	emit closed();
}

void UsernameBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

UsernameBox::~UsernameBox() {
}

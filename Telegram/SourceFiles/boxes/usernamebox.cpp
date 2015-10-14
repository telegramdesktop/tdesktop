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

#include "application.h"
#include "usernamebox.h"
#include "mainwidget.h"
#include "window.h"

UsernameBox::UsernameBox() : AbstractBox(st::boxWidth),
_save(this, lang(lng_settings_save), st::defaultBoxButton),
_cancel(this, lang(lng_cancel), st::cancelBoxButton),
_username(this, st::defaultInputField, qsl("@username"), App::self()->username, false),
_link(this, QString(), st::defaultBoxLinkButton),
_saveRequestId(0), _checkRequestId(0),
_about(st::boxWidth - st::usernamePadding.left()) {
	setBlueTitle(true);

	_goodText = App::self()->username.isEmpty() ? QString() : lang(lng_username_available);

	textstyleSet(&st::usernameTextStyle);
	_about.setRichText(st::boxTextFont, lang(lng_username_about));
	resizeMaxHeight(st::boxWidth, st::boxTitleHeight + st::usernamePadding.top() + _username.height() + st::usernameSkip + _about.countHeight(st::boxWidth - st::usernamePadding.left()) + 3 * st::usernameTextStyle.lineHeight + st::usernamePadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom());
	textstyleRestore();

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_username, SIGNAL(changed()), this, SLOT(onChanged()));
	connect(&_username, SIGNAL(submitted(bool)), this, SLOT(onSave()));

	connect(&_link, SIGNAL(clicked()), this, SLOT(onLinkClick()));

	_checkTimer.setSingleShot(true);
	connect(&_checkTimer, SIGNAL(timeout()), this, SLOT(onCheck()));

	prepare();
}

void UsernameBox::hideAll() {
	_username.hide();
	_save.hide();
	_cancel.hide();
	_link.hide();

	AbstractBox::hideAll();
}

void UsernameBox::showAll() {
	_username.show();
	_save.show();
	_cancel.show();
	updateLinkText();

	AbstractBox::showAll();
}

void UsernameBox::showDone() {
	_username.setFocus();
}

void UsernameBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_username_title));

	if (!_copiedTextLink.isEmpty()) {
		p.setPen(st::usernameDefaultFg);
		p.setFont(st::boxTextFont);
		p.drawTextLeft(st::usernamePadding.left(), _username.y() + _username.height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), _copiedTextLink);
	} else if (!_errorText.isEmpty()) {
		p.setPen(st::setErrColor);
		p.setFont(st::boxTextFont);
		p.drawTextLeft(st::usernamePadding.left(), _username.y() + _username.height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), _errorText);
	} else if (!_goodText.isEmpty()) {
		p.setPen(st::setGoodColor);
		p.setFont(st::boxTextFont);
		p.drawTextLeft(st::usernamePadding.left(), _username.y() + _username.height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), _goodText);
	} else {
		p.setPen(st::usernameDefaultFg);
		p.setFont(st::boxTextFont);
		p.drawTextLeft(st::usernamePadding.left(), _username.y() + _username.height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), lang(lng_username_choose));
	}
	p.setPen(st::black);
	textstyleSet(&st::usernameTextStyle);
	int32 availw = st::boxWidth - st::usernamePadding.left(), h = _about.countHeight(availw);
	_about.drawLeft(p, st::usernamePadding.left(), _username.y() + _username.height() + st::usernameSkip, availw, width());
	textstyleRestore();

	int32 linky = _username.y() + _username.height() + st::usernameSkip + h + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2);
	if (_link.isHidden()) {
		p.drawTextLeft(st::usernamePadding.left(), linky, width(), lang(lng_username_link_willbe));
		p.setPen(st::usernameDefaultFg);
		p.drawTextLeft(st::usernamePadding.left(), linky + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2), width(), qsl("https://telegram.me/username"));
	} else {
		p.drawTextLeft(st::usernamePadding.left(), linky, width(), lang(lng_username_link));
	}
}

void UsernameBox::resizeEvent(QResizeEvent *e) {
	_username.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _username.height());
	_username.moveToLeft(st::usernamePadding.left(), st::boxTitleHeight + st::usernamePadding.top());

	textstyleSet(&st::usernameTextStyle);
	int32 availw = st::boxWidth - st::usernamePadding.left(), h = _about.countHeight(availw);
	textstyleRestore();
	int32 linky = _username.y() + _username.height() + st::usernameSkip + h + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2);
	_link.moveToLeft(st::usernamePadding.left(), linky + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2));

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());

	AbstractBox::resizeEvent(e);
}

void UsernameBox::onSave() {
	if (_saveRequestId) return;

	_sentUsername = getName();
	_saveRequestId = MTP::send(MTPaccount_UpdateUsername(MTP_string(_sentUsername)), rpcDone(&UsernameBox::onUpdateDone), rpcFail(&UsernameBox::onUpdateFail));
}

void UsernameBox::onCheck() {
	if (_checkRequestId) {
		MTP::cancel(_checkRequestId);
	}
	QString name = getName();
	if (name.size() >= MinUsernameLength) {
		_checkUsername = name;
		_checkRequestId = MTP::send(MTPaccount_CheckUsername(MTP_string(name)), rpcDone(&UsernameBox::onCheckDone), rpcFail(&UsernameBox::onCheckFail));
	}
}

void UsernameBox::onChanged() {
	updateLinkText();
	QString name = getName();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_copiedTextLink = _errorText = _goodText = QString();
			update();
		}
		_checkTimer.stop();
	} else {
		int32 i, len = name.size();
		for (int32 i = 0; i < len; ++i) {
			QChar ch = name.at(i);
			if ((ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_' && (ch != '@' || i > 0)) {
				if (_errorText != lang(lng_username_bad_symbols) || !_copiedTextLink.isEmpty()) {
					_copiedTextLink = QString();
					_errorText = lang(lng_username_bad_symbols);
					update();
				}
				_checkTimer.stop();
				return;
			}
		}
		if (name.size() < MinUsernameLength) {
			if (_errorText != lang(lng_username_too_short) || !_copiedTextLink.isEmpty()) {
				_copiedTextLink = QString();
				_errorText = lang(lng_username_too_short);
				update();
			}
			_checkTimer.stop();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty() || !_copiedTextLink.isEmpty()) {
				_copiedTextLink = _errorText = _goodText = QString();
				update();
			}
			_checkTimer.start(UsernameCheckTimeout);
		}
	}
}

void UsernameBox::onLinkClick() {
	App::app()->clipboard()->setText(qsl("https://telegram.me/") + getName());
	_copiedTextLink = lang(lng_username_copied);
	update();
}

void UsernameBox::onUpdateDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(1, user));
	emit closed();
}

bool UsernameBox::onUpdateFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_saveRequestId = 0;
	QString err(error.type());
	if (err == "USERNAME_NOT_MODIFIED" || _sentUsername == App::self()->username) {
		App::self()->setName(textOneLine(App::self()->firstName), textOneLine(App::self()->lastName), textOneLine(App::self()->nameOrPhone), textOneLine(_sentUsername));
		emit closed();
		return true;
	} else if (err == "USERNAME_INVALID") {
		_username.setFocus();
		_username.showError();
		_copiedTextLink = QString();
		_errorText = lang(lng_username_invalid);
		update();
		return true;
	} else if (err == "USERNAME_OCCUPIED" || err == "USERNAMES_UNAVAILABLE") {
		_username.setFocus();
		_username.showError();
		_copiedTextLink = QString();
		_errorText = lang(lng_username_occupied);
		update();
		return true;
	}
	_username.setFocus();
	return true;
}

void UsernameBox::onCheckDone(const MTPBool &result) {
	_checkRequestId = 0;
	QString newError = (result.v || _checkUsername == App::self()->username) ? QString() : lang(lng_username_occupied);
	QString newGood = newError.isEmpty() ? lang(lng_username_available) : QString();
	if (_errorText != newError || _goodText != newGood || !_copiedTextLink.isEmpty()) {
		_errorText = newError;
		_goodText = newGood;
		_copiedTextLink = QString();
		update();
	}
}

bool UsernameBox::onCheckFail(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_checkRequestId = 0;
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
	_copiedTextLink = QString();
	_username.setFocus();
	return true;
}

QString UsernameBox::getName() const {
	return _username.text().replace('@', QString()).trimmed();
}

void UsernameBox::updateLinkText() {
	QString uname = getName();
	_link.setText(st::boxTextFont->elided(qsl("https://telegram.me/") + uname, st::boxWidth - st::usernamePadding.left() - st::usernamePadding.right()));
	if (uname.isEmpty()) {
		if (!_link.isHidden()) {
			_link.hide();
			update();
		}
	} else {
		if (_link.isHidden()) {
			_link.show();
			update();
		}
	}
}
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/username_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/widgets/buttons.h"
#include "ui/special_fields.h"
#include "ui/toast/toast.h"
#include "core/application.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "mtproto/facade.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

constexpr auto kMinUsernameLength = 5;

} // namespace

UsernameBox::UsernameBox(QWidget*, not_null<Main::Session*> session)
: _session(session)
, _username(
	this,
	st::defaultInputField,
	rpl::single(qsl("@username")),
	session->user()->username,
	false)
, _link(this, QString(), st::boxLinkButton)
, _about(st::boxWidth - st::usernamePadding.left())
, _checkTimer(this) {
}

void UsernameBox::prepare() {
	_goodText = _session->user()->username.isEmpty()
		? QString()
		: tr::lng_username_available(tr::now);

	setTitle(tr::lng_username_title());

	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	connect(_username, &Ui::MaskedInputField::changed, [=] { changed(); });
	connect(_username, &Ui::MaskedInputField::submitted, [=] { save(); });
	_link->addClickHandler([=] { linkClick(); });

	_about.setText(st::usernameTextStyle, tr::lng_username_about(tr::now));
	setDimensions(st::boxWidth, st::usernamePadding.top() + _username->height() + st::usernameSkip + _about.countHeight(st::boxWidth - st::usernamePadding.left()) + 3 * st::usernameTextStyle.lineHeight + st::usernamePadding.bottom());

	_checkTimer->setSingleShot(true);
	connect(_checkTimer, &QTimer::timeout, [=] { check(); });

	updateLinkText();
}

void UsernameBox::setInnerFocus() {
	_username->setFocusFast();
}

void UsernameBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setFont(st::boxTextFont);
	if (!_errorText.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawTextLeft(st::usernamePadding.left(), _username->y() + _username->height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), _errorText);
	} else if (!_goodText.isEmpty()) {
		p.setPen(st::boxTextFgGood);
		p.drawTextLeft(st::usernamePadding.left(), _username->y() + _username->height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), _goodText);
	} else {
		p.setPen(st::usernameDefaultFg);
		p.drawTextLeft(st::usernamePadding.left(), _username->y() + _username->height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), tr::lng_username_choose(tr::now));
	}
	p.setPen(st::boxTextFg);
	int32 availw = st::boxWidth - st::usernamePadding.left(), h = _about.countHeight(availw);
	_about.drawLeft(p, st::usernamePadding.left(), _username->y() + _username->height() + st::usernameSkip, availw, width());

	int32 linky = _username->y() + _username->height() + st::usernameSkip + h + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2);
	if (_link->isHidden()) {
		p.drawTextLeft(st::usernamePadding.left(), linky, width(), tr::lng_username_link_willbe(tr::now));
		p.setPen(st::usernameDefaultFg);
		p.drawTextLeft(st::usernamePadding.left(), linky + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2), width(), Core::App().createInternalLinkFull(qsl("username")));
	} else {
		p.drawTextLeft(st::usernamePadding.left(), linky, width(), tr::lng_username_link(tr::now));
	}
}

void UsernameBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_username->resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _username->height());
	_username->moveToLeft(st::usernamePadding.left(), st::usernamePadding.top());

	int32 availw = st::boxWidth - st::usernamePadding.left(), h = _about.countHeight(availw);
	int32 linky = _username->y() + _username->height() + st::usernameSkip + h + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2);
	_link->moveToLeft(st::usernamePadding.left(), linky + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2));
}

void UsernameBox::save() {
	if (_saveRequestId) return;

	_sentUsername = getName();
	_saveRequestId = MTP::send(MTPaccount_UpdateUsername(MTP_string(_sentUsername)), rpcDone(&UsernameBox::onUpdateDone), rpcFail(&UsernameBox::onUpdateFail));
}

void UsernameBox::check() {
	if (_checkRequestId) {
		MTP::cancel(_checkRequestId);
	}
	QString name = getName();
	if (name.size() >= kMinUsernameLength) {
		_checkUsername = name;
		_checkRequestId = MTP::send(
			MTPaccount_CheckUsername(
				MTP_string(name)),
			rpcDone(&UsernameBox::onCheckDone),
			rpcFail(&UsernameBox::onCheckFail));
	}
}

void UsernameBox::changed() {
	updateLinkText();
	QString name = getName();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer->stop();
	} else {
		int32 len = name.size();
		for (int32 i = 0; i < len; ++i) {
			QChar ch = name.at(i);
			if ((ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_' && (ch != '@' || i > 0)) {
				if (_errorText != tr::lng_username_bad_symbols(tr::now)) {
					_errorText = tr::lng_username_bad_symbols(tr::now);
					update();
				}
				_checkTimer->stop();
				return;
			}
		}
		if (name.size() < kMinUsernameLength) {
			if (_errorText != tr::lng_username_too_short(tr::now)) {
				_errorText = tr::lng_username_too_short(tr::now);
				update();
			}
			_checkTimer->stop();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer->start(UsernameCheckTimeout);
		}
	}
}

void UsernameBox::linkClick() {
	QGuiApplication::clipboard()->setText(Core::App().createInternalLinkFull(getName()));
	Ui::Toast::Show(tr::lng_username_copied(tr::now));
}

void UsernameBox::onUpdateDone(const MTPUser &user) {
	_session->data().processUser(user);
	closeBox();
}

bool UsernameBox::onUpdateFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	const auto self = _session->user();
	const auto &err = error.type();
	if (err == qstr("USERNAME_NOT_MODIFIED") || _sentUsername == self->username) {
		self->setName(
			TextUtilities::SingleLine(self->firstName),
			TextUtilities::SingleLine(self->lastName),
			TextUtilities::SingleLine(self->nameOrPhone),
			TextUtilities::SingleLine(_sentUsername));
		closeBox();
		return true;
	} else if (err == qstr("USERNAME_INVALID")) {
		_username->setFocus();
		_username->showError();
		_errorText = tr::lng_username_invalid(tr::now);
		update();
		return true;
	} else if (err == qstr("USERNAME_OCCUPIED") || err == qstr("USERNAMES_UNAVAILABLE")) {
		_username->setFocus();
		_username->showError();
		_errorText = tr::lng_username_occupied(tr::now);
		update();
		return true;
	}
	_username->setFocus();
	return true;
}

void UsernameBox::onCheckDone(const MTPBool &result) {
	_checkRequestId = 0;
	const auto newError = (mtpIsTrue(result)
		|| _checkUsername == _session->user()->username)
		? QString()
		: tr::lng_username_occupied(tr::now);
	const auto newGood = newError.isEmpty()
		? tr::lng_username_available(tr::now)
		: QString();
	if (_errorText != newError || _goodText != newGood) {
		_errorText = newError;
		_goodText = newGood;
		update();
	}
}

bool UsernameBox::onCheckFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_checkRequestId = 0;
	QString err(error.type());
	if (err == qstr("USERNAME_INVALID")) {
		_errorText = tr::lng_username_invalid(tr::now);
		update();
		return true;
	} else if (err == qstr("USERNAME_OCCUPIED") && _checkUsername != _session->user()->username) {
		_errorText = tr::lng_username_occupied(tr::now);
		update();
		return true;
	}
	_goodText = QString();
	_username->setFocus();
	return true;
}

QString UsernameBox::getName() const {
	return _username->text().replace('@', QString()).trimmed();
}

void UsernameBox::updateLinkText() {
	QString uname = getName();
	_link->setText(st::boxTextFont->elided(Core::App().createInternalLinkFull(uname), st::boxWidth - st::usernamePadding.left() - st::usernamePadding.right()));
	if (uname.isEmpty()) {
		if (!_link->isHidden()) {
			_link->hide();
			update();
		}
	} else {
		if (_link->isHidden()) {
			_link->show();
			update();
		}
	}
}
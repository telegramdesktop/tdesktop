/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/username_box.h"

#include "lang/lang_keys.h"
#include "application.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/toast/toast.h"
#include "styles/style_boxes.h"
#include "messenger.h"

namespace {

constexpr auto kMinUsernameLength = 5;

} // namespace

UsernameBox::UsernameBox(QWidget*)
: _username(this, st::defaultInputField, [] { return qsl("@username"); }, App::self()->username, false)
, _link(this, QString(), st::boxLinkButton)
, _about(st::boxWidth - st::usernamePadding.left())
, _checkTimer(this) {
}

void UsernameBox::prepare() {
	_goodText = App::self()->username.isEmpty() ? QString() : lang(lng_username_available);

	setTitle(langFactory(lng_username_title));

	addButton(langFactory(lng_settings_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	connect(_username, SIGNAL(changed()), this, SLOT(onChanged()));
	connect(_username, SIGNAL(submitted(bool)), this, SLOT(onSave()));
	connect(_link, SIGNAL(clicked()), this, SLOT(onLinkClick()));

	_about.setRichText(st::usernameTextStyle, lang(lng_username_about));
	setDimensions(st::boxWidth, st::usernamePadding.top() + _username->height() + st::usernameSkip + _about.countHeight(st::boxWidth - st::usernamePadding.left()) + 3 * st::usernameTextStyle.lineHeight + st::usernamePadding.bottom());

	_checkTimer->setSingleShot(true);
	connect(_checkTimer, SIGNAL(timeout()), this, SLOT(onCheck()));

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
		p.drawTextLeft(st::usernamePadding.left(), _username->y() + _username->height() + ((st::usernameSkip - st::boxTextFont->height) / 2), width(), lang(lng_username_choose));
	}
	p.setPen(st::boxTextFg);
	int32 availw = st::boxWidth - st::usernamePadding.left(), h = _about.countHeight(availw);
	_about.drawLeft(p, st::usernamePadding.left(), _username->y() + _username->height() + st::usernameSkip, availw, width());

	int32 linky = _username->y() + _username->height() + st::usernameSkip + h + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2);
	if (_link->isHidden()) {
		p.drawTextLeft(st::usernamePadding.left(), linky, width(), lang(lng_username_link_willbe));
		p.setPen(st::usernameDefaultFg);
		p.drawTextLeft(st::usernamePadding.left(), linky + st::usernameTextStyle.lineHeight + ((st::usernameTextStyle.lineHeight - st::boxTextFont->height) / 2), width(), Messenger::Instance().createInternalLinkFull(qsl("username")));
	} else {
		p.drawTextLeft(st::usernamePadding.left(), linky, width(), lang(lng_username_link));
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
	if (name.size() >= kMinUsernameLength) {
		_checkUsername = name;
		_checkRequestId = MTP::send(
			MTPaccount_CheckUsername(
				MTP_string(name)),
			rpcDone(&UsernameBox::onCheckDone),
			rpcFail(&UsernameBox::onCheckFail));
	}
}

void UsernameBox::onChanged() {
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
				if (_errorText != lang(lng_username_bad_symbols)) {
					_errorText = lang(lng_username_bad_symbols);
					update();
				}
				_checkTimer->stop();
				return;
			}
		}
		if (name.size() < kMinUsernameLength) {
			if (_errorText != lang(lng_username_too_short)) {
				_errorText = lang(lng_username_too_short);
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

void UsernameBox::onLinkClick() {
	Application::clipboard()->setText(Messenger::Instance().createInternalLinkFull(getName()));
	Ui::Toast::Show(lang(lng_username_copied));
}

void UsernameBox::onUpdateDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(1, user));
	closeBox();
}

bool UsernameBox::onUpdateFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	QString err(error.type());
	if (err == qstr("USERNAME_NOT_MODIFIED") || _sentUsername == App::self()->username) {
		App::self()->setName(TextUtilities::SingleLine(App::self()->firstName), TextUtilities::SingleLine(App::self()->lastName), TextUtilities::SingleLine(App::self()->nameOrPhone), TextUtilities::SingleLine(_sentUsername));
		closeBox();
		return true;
	} else if (err == qstr("USERNAME_INVALID")) {
		_username->setFocus();
		_username->showError();
		_errorText = lang(lng_username_invalid);
		update();
		return true;
	} else if (err == qstr("USERNAME_OCCUPIED") || err == qstr("USERNAMES_UNAVAILABLE")) {
		_username->setFocus();
		_username->showError();
		_errorText = lang(lng_username_occupied);
		update();
		return true;
	}
	_username->setFocus();
	return true;
}

void UsernameBox::onCheckDone(const MTPBool &result) {
	_checkRequestId = 0;
	QString newError = (mtpIsTrue(result) || _checkUsername == App::self()->username) ? QString() : lang(lng_username_occupied);
	QString newGood = newError.isEmpty() ? lang(lng_username_available) : QString();
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
		_errorText = lang(lng_username_invalid);
		update();
		return true;
	} else if (err == qstr("USERNAME_OCCUPIED") && _checkUsername != App::self()->username) {
		_errorText = lang(lng_username_occupied);
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
	_link->setText(st::boxTextFont->elided(Messenger::Instance().createInternalLinkFull(uname), st::boxWidth - st::usernamePadding.left() - st::usernamePadding.right()));
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
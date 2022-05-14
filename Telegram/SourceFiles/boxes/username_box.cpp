/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/username_box.h"

#include "boxes/peers/edit_peer_common.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/special_fields.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

UsernameBox::UsernameBox(QWidget*, not_null<Main::Session*> session)
: _session(session)
, _font(st::normalFont)
, _padding(st::usernamePadding)
, _textCenterTop((_font->height - _font->height) / 2)
, _api(&_session->mtp())
, _username(
	this,
	st::defaultInputField,
	rpl::single(qsl("@username")),
	session->user()->username,
	QString())
, _about(
	this,
	tr::lng_username_description(Ui::Text::RichLangValue),
	st::defaultBoxLabel)
, _link(this, QString(), st::defaultLinkButton)
, _checkTimer([=] { check(); }) {
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

	_about->resizeToWidth(
		st::boxWideWidth - _padding.left() - _padding.right());
	_about->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(
			st::boxWideWidth,
			(_padding.top()
				+ _username->height()
				+ st::usernameSkip
				+ height
				+ 3 * _font->height
				+ _padding.bottom()));
	}, lifetime());

	updateLinkText();
}

void UsernameBox::setInnerFocus() {
	_username->setFocusFast();
}

void UsernameBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	const auto textTop = _username->y()
		+ _username->height()
		+ ((st::usernameSkip - _font->height) / 2);

	p.setFont(_font);
	if (!_errorText.isEmpty()) {
		p.setPen(st::boxTextFgError);
		p.drawTextLeft(
			_padding.left(),
			textTop,
			width(),
			_errorText);
	} else if (!_goodText.isEmpty()) {
		p.setPen(st::boxTextFgGood);
		p.drawTextLeft(
			_padding.left(),
			textTop,
			width(),
			_goodText);
	} else {
		p.setPen(st::usernameDefaultFg);
		p.drawTextLeft(
			_padding.left(),
			textTop,
			width(),
			tr::lng_username_choose(tr::now));
	}
	p.setPen(st::boxTextFg);

	const auto linkTop = _username->y()
		+ _username->height()
		+ st::usernameSkip
		+ _about->height()
		+ _font->height
		+ _textCenterTop;
	if (_link->isHidden()) {
		p.drawTextLeft(
			_padding.left(),
			linkTop,
			width(),
			tr::lng_username_link_willbe(tr::now));
		p.setPen(st::usernameDefaultFg);
		const auto link = _session->createInternalLinkFull(qsl("username"));
		p.drawTextLeft(
			_padding.left(),
			linkTop
				+ _font->height
				+ _textCenterTop,
			width(),
			link);
	} else {
		p.drawTextLeft(
			_padding.left(),
			linkTop,
			width(),
			tr::lng_username_link(tr::now));
	}
}

void UsernameBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_username->resize(
		width() - _padding.left() - _padding.right(),
		_username->height());
	_username->moveToLeft(_padding.left(), _padding.top());

	_about->moveToLeft(
		_padding.left(),
		_username->y() + _username->height() + st::usernameSkip);

	const auto linkTop = _about->y()
		+ _about->height()
		+ _font->height
		+ _textCenterTop;
	_link->moveToLeft(
		_padding.left(),
		linkTop + _font->height + _textCenterTop);
}

void UsernameBox::save() {
	if (_saveRequestId) {
		return;
	}

	_sentUsername = getName();
	_saveRequestId = _api.request(MTPaccount_UpdateUsername(
		MTP_string(_sentUsername)
	)).done([=](const MTPUser &result) {
		_saveRequestId = 0;
		_session->data().processUser(result);
		closeBox();
	}).fail([=](const MTP::Error &error) {
		_saveRequestId = 0;
		updateFail(error.type());
	}).send();
}

void UsernameBox::check() {
	_api.request(base::take(_checkRequestId)).cancel();

	const auto name = getName();
	if (name.size() < Ui::EditPeer::kMinUsernameLength) {
		return;
	}
	_checkUsername = name;
	_checkRequestId = _api.request(MTPaccount_CheckUsername(
		MTP_string(name)
	)).done([=](const MTPBool &result) {
		_checkRequestId = 0;

		_errorText = (mtpIsTrue(result)
				|| _checkUsername == _session->user()->username)
			? QString()
			: tr::lng_username_occupied(tr::now);
		_goodText = _errorText.isEmpty()
			? tr::lng_username_available(tr::now)
			: QString();

		update();
	}).fail([=](const MTP::Error &error) {
		_checkRequestId = 0;
		checkFail(error.type());
	}).send();
}

void UsernameBox::changed() {
	updateLinkText();
	const auto name = getName();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer.cancel();
	} else {
		const auto len = int(name.size());
		for (auto i = 0; i < len; ++i) {
			const auto ch = name.at(i);
			if ((ch < 'A' || ch > 'Z')
				&& (ch < 'a' || ch > 'z')
				&& (ch < '0' || ch > '9')
				&& ch != '_'
				&& (ch != '@' || i > 0)) {
				if (_errorText != tr::lng_username_bad_symbols(tr::now)) {
					_errorText = tr::lng_username_bad_symbols(tr::now);
					update();
				}
				_checkTimer.cancel();
				return;
			}
		}
		if (name.size() < Ui::EditPeer::kMinUsernameLength) {
			if (_errorText != tr::lng_username_too_short(tr::now)) {
				_errorText = tr::lng_username_too_short(tr::now);
				update();
			}
			_checkTimer.cancel();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer.callOnce(Ui::EditPeer::kUsernameCheckTimeout);
		}
	}
}

void UsernameBox::linkClick() {
	QGuiApplication::clipboard()->setText(
		_session->createInternalLinkFull(getName()));
	Ui::Toast::Show(tr::lng_username_copied(tr::now));
}

void UsernameBox::updateFail(const QString &error) {
	const auto self = _session->user();
	if ((error == qstr("USERNAME_NOT_MODIFIED"))
		|| (_sentUsername == self->username)) {
		self->setName(
			TextUtilities::SingleLine(self->firstName),
			TextUtilities::SingleLine(self->lastName),
			TextUtilities::SingleLine(self->nameOrPhone),
			TextUtilities::SingleLine(_sentUsername));
		closeBox();
	} else if (error == qstr("USERNAME_INVALID")) {
		_username->setFocus();
		_username->showError();
		_errorText = tr::lng_username_invalid(tr::now);
		update();
	} else if ((error == qstr("USERNAME_OCCUPIED"))
		|| (error == qstr("USERNAMES_UNAVAILABLE"))) {
		_username->setFocus();
		_username->showError();
		_errorText = tr::lng_username_occupied(tr::now);
		update();
	} else {
		_username->setFocus();
	}
}

void UsernameBox::checkFail(const QString &error) {
	if (error == qstr("USERNAME_INVALID")) {
		_errorText = tr::lng_username_invalid(tr::now);
		update();
	} else if ((error == qstr("USERNAME_OCCUPIED"))
		&& (_checkUsername != _session->user()->username)) {
		_errorText = tr::lng_username_occupied(tr::now);
		update();
	} else {
		_goodText = QString();
		_username->setFocus();
	}
}

QString UsernameBox::getName() const {
	return _username->text().replace('@', QString()).trimmed();
}

void UsernameBox::updateLinkText() {
	const auto uname = getName();
	_link->setText(_font->elided(
		_session->createInternalLinkFull(uname),
		st::boxWideWidth - _padding.left() - _padding.right()));
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

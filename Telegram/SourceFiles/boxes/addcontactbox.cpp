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
#include "addcontactbox.h"
#include "mainwidget.h"
#include "window.h"

AddContactBox::AddContactBox(QString fname, QString lname, QString phone) :
	_peer(0),
	_addButton(this, lang(lng_add_contact), st::btnSelectDone),
	_retryButton(this, lang(lng_try_other_contact), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
    _firstInput(this, st::inpAddContact, lang(lng_signup_firstname), fname),
    _lastInput(this, st::inpAddContact, lang(lng_signup_lastname), lname),
    _phoneInput(this, st::inpAddContact, lang(lng_contact_phone), phone.isEmpty() ? phone : App::formatPhone(phone)),
	_contactId(0), _addRequest(0), a_opacity(0, 1), _hiding(false) {

	if (!phone.isEmpty()) {
		_phoneInput.setDisabled(true);
	}

	initBox();
}

AddContactBox::AddContactBox(PeerData *peer) :
	_peer(peer),
	_addButton(this, lang(lng_settings_save), st::btnSelectDone),
	_retryButton(this, lang(lng_try_other_contact), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
    _firstInput(this, st::inpAddContact, lang(peer->chat ? lng_dlg_new_group_name : lng_signup_firstname), peer->chat ? peer->name : peer->asUser()->firstName),
    _lastInput(this, st::inpAddContact, lang(lng_signup_lastname), peer->chat ? QString() : peer->asUser()->lastName),
    _phoneInput(this, st::inpAddContact, lang(lng_contact_phone)),
	_contactId(0), _addRequest(0), a_opacity(0, 1), _hiding(false) {

	initBox();
}

void AddContactBox::initBox() {
	_width = st::addContactWidth;
	if (_peer) {
		if (_peer->chat) {
			_boxTitle = lang(lng_edit_group_title);
			_height = st::addContactTitleHeight + st::addContactPadding.top() + 1 * _firstInput.height() + st::addContactPadding.bottom() + _addButton.height();
		} else {
			_boxTitle = lang(_peer == App::self() ? lng_edit_self_title : lng_edit_contact_title);
			_height = st::addContactTitleHeight + st::addContactPadding.top() + 2 * _firstInput.height() + 1 * st::addContactDelta + st::addContactPadding.bottom() + _addButton.height();
		}
	} else {
		bool readyToAdd = !_phoneInput.text().isEmpty() && (!_firstInput.text().isEmpty() || !_lastInput.text().isEmpty());
		_boxTitle = lang(readyToAdd ? lng_confirm_contact_data : lng_enter_contact_data);
		_height = st::addContactTitleHeight + st::addContactPadding.top() + 3 * _firstInput.height() + 2 * st::addContactDelta + st::addContactPadding.bottom() + _addButton.height();
	}
	_firstInput.setGeometry(st::addContactPadding.left(), st::addContactTitleHeight + st::addContactPadding.top(), _width - st::addContactPadding.left() - st::addContactPadding.right(), _firstInput.height());
	_lastInput.setGeometry(st::addContactPadding.left(), _firstInput.y() + _firstInput.height() + st::addContactDelta, _firstInput.width(), _firstInput.height());
	_phoneInput.setGeometry(st::addContactPadding.left(), _lastInput.y() + _lastInput.height() + st::addContactDelta, _lastInput.width(), _lastInput.height());

	int32 buttonTop = (_peer ? (_peer->chat ? _firstInput : _lastInput) : _phoneInput).y() + _phoneInput.height() + st::addContactPadding.bottom();
	_cancelButton.move(0, buttonTop);
	_addButton.move(_width - _addButton.width(), buttonTop);
	_retryButton.move(_width - _retryButton.width(), buttonTop);
	_retryButton.hide();

	connect(&_addButton, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_retryButton, SIGNAL(clicked()), this, SLOT(onRetry()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void AddContactBox::hideAll() {
	_firstInput.hide();
	_lastInput.hide();
	_phoneInput.hide();
	_addButton.hide();
	_retryButton.hide();
	_cancelButton.hide();
}

void AddContactBox::showAll() {
	_firstInput.show();
	if (_peer && _peer->chat) {
		_lastInput.hide();
	} else {
		_lastInput.show();
	}
	if (_peer) {
		_phoneInput.hide();
	} else {
		_phoneInput.show();
	}
	_addButton.show();
	_cancelButton.show();
}

void AddContactBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_firstInput.hasFocus()) {
			if (_peer && _peer->chat) {
				if (_firstInput.text().trimmed().isEmpty()) {
					_firstInput.setFocus();
					_firstInput.notaBene();
				} else {
					onSend();
				}
			} else {
				_lastInput.setFocus();
			}
		} else if (_lastInput.hasFocus()) {
			if (_peer) {
				if (_firstInput.text().trimmed().isEmpty()) {
					_firstInput.setFocus();
					_firstInput.notaBene();
				} else if (_lastInput.text().trimmed().isEmpty()) {
					_lastInput.setFocus();
					_lastInput.notaBene();
				} else {
					onSend();
				}
			} else if (_phoneInput.isEnabled()) {
				_phoneInput.setFocus();
			} else {
				onSend();
			}
		} else if (_phoneInput.hasFocus()) {
			if (_firstInput.text().trimmed().isEmpty()) {
				_firstInput.setFocus();
				_firstInput.notaBene();
			} else if (_lastInput.text().trimmed().isEmpty()) {
				_lastInput.setFocus();
				_lastInput.notaBene();
			} else {
				onSend();
			}
		}
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void AddContactBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void AddContactBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

			// paint shadows
			if (_retryButton.isHidden()) {
				p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);
			}
			p.fillRect(0, size().height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.fillRect(st::btnSelectCancel.width, size().height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

			// draw box title / text
			p.setPen(st::black->p);
			p.setFont(st::addContactTitleFont->f);
			if (_retryButton.isHidden()) {
				p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, _boxTitle);
			} else {
				int32 h = size().height() - st::boxPadding.top() * 2 - _retryButton.height() - st::boxPadding.bottom();
				p.drawText(QRect(st::boxPadding.left(), st::boxPadding.top(), _width - st::boxPadding.left() - st::boxPadding.right(), h), lang(lng_contact_not_joined).replace(qsl("{name}"), _sentName), style::al_topleft);
			}
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void AddContactBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			if ((_firstInput.text().isEmpty() && _lastInput.text().isEmpty()) || _phoneInput.isHidden() || !_phoneInput.isEnabled()) {
				_firstInput.setFocus();
			} else {
				_phoneInput.setFocus();
			}
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
}

void AddContactBox::onSend() {
	if (_addRequest) return;

	QString firstName = _firstInput.text().trimmed(), lastName = _lastInput.text().trimmed(), phone = _phoneInput.text().trimmed();
	if (firstName.isEmpty() && lastName.isEmpty()) {
		_firstInput.setFocus();
		_firstInput.notaBene();
		return;
	} else if (!_peer && !App::isValidPhone(phone)) {
		_phoneInput.setFocus();
		_phoneInput.notaBene();
		return;
	}
	if (firstName.isEmpty()) {
		firstName = lastName;
		lastName = QString();
	}
	_sentName = firstName;
	if (_peer == App::self()) {
		_addRequest = MTP::send(MTPaccount_UpdateProfile(MTP_string(firstName), MTP_string(lastName)), rpcDone(&AddContactBox::onSaveSelfDone), rpcFail(&AddContactBox::onSaveSelfFail));
	} else if (_peer) {
		if (_peer->chat) {
			_addRequest = MTP::send(MTPmessages_EditChatTitle(MTP_int(App::chatFromPeer(_peer->id)), MTP_string(firstName)), rpcDone(&AddContactBox::onSaveChatDone), rpcFail(&AddContactBox::onSaveFail));
		} else {
			_contactId = MTP::nonce<uint64>();
			QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(_peer->asUser()->phone), MTP_string(firstName), MTP_string(lastName)));
			_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v), MTP_bool(false)), rpcDone(&AddContactBox::onSaveUserDone), rpcFail(&AddContactBox::onSaveFail));
		}
	} else {
		_contactId = MTP::nonce<uint64>();
		QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(phone), MTP_string(firstName), MTP_string(lastName)));
		_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v), MTP_bool(false)), rpcDone(&AddContactBox::onImportDone));
	}
}

void AddContactBox::onSaveSelfDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(QVector<MTPUser>(1, user)));
	emit closed();
}

bool AddContactBox::onSaveSelfFail(const RPCError &error) {
	QString err(error.type());
	QString firstName = textOneLine(_firstInput.text()), lastName = textOneLine(_lastInput.text());
	if (err == "NAME_NOT_MODIFIED") {
		App::self()->setName(firstName, lastName, firstName + ' ' + lastName);
		emit closed();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		_firstInput.setFocus();
		_firstInput.notaBene();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		_lastInput.setFocus();
		_lastInput.notaBene();
		return true;
	}
	_firstInput.setFocus();
	return true;
}

bool AddContactBox::onSaveFail(const RPCError &error) {
	QString err(error.type());
	QString firstName = _firstInput.text().trimmed(), lastName = _lastInput.text().trimmed();
	if (err == "CHAT_TITLE_NOT_MODIFIED") {
		_peer->updateName(firstName, QString());
		emit closed();
		return true;
	} else if (err == "NO_CHAT_TITLE") {
		_firstInput.setFocus();
		_firstInput.notaBene();
		return true;
	}
	_firstInput.setFocus();
	return true;
}

void AddContactBox::onImportDone(const MTPcontacts_ImportedContacts &res) {
	if (_hiding || !App::main()) return;

	const MTPDcontacts_importedContacts &d(res.c_contacts_importedContacts());
	App::feedUsers(d.vusers);

	const QVector<MTPImportedContact> &v(d.vimported.c_vector().v);
	int32 uid = 0;
	if (!v.isEmpty()) {
		const MTPDimportedContact &c(v.front().c_importedContact());
		if (c.vclient_id.v != _contactId) return;

		uid = c.vuser_id.v;
		if (uid && !App::userLoaded(uid)) {
			uid = 0;
		}
	}
	if (uid) {
		App::main()->addNewContact(uid);
		App::main()->showPeer(App::peerFromUser(uid));
		App::wnd()->hideLayer();
	} else {
		_addButton.hide();
		_firstInput.hide();
		_lastInput.hide();
		_phoneInput.hide();
		_retryButton.show();
		int32 theight = st::addContactTitleFont->m.boundingRect(0, 0, _width - st::boxPadding.left() - st::boxPadding.right(), 1, Qt::TextWordWrap, lang(lng_contact_not_joined).replace(qsl("{name}"), _sentName)).height();
		int32 h = st::boxPadding.top() * 2 + theight + _retryButton.height() + st::boxPadding.bottom();
		resize(_width, h);
		_retryButton.move(_retryButton.x(), h - _retryButton.height());
		_cancelButton.move(_cancelButton.x(), h - _retryButton.height());
		update();
	}
}

void AddContactBox::onSaveChatDone(const MTPmessages_StatedMessage &result) {
	App::main()->sentFullDataReceived(0, result);
	emit closed();
}

void AddContactBox::onSaveUserDone(const MTPcontacts_ImportedContacts &res) {
	const MTPDcontacts_importedContacts &d(res.c_contacts_importedContacts());
	App::feedUsers(d.vusers);
	emit closed();
}

void AddContactBox::onCancel() {
	emit closed();
}

void AddContactBox::onRetry() {
	_addRequest = 0;
	_contactId = 0;
	_addButton.show();
	_cancelButton.move(_cancelButton.x(), _addButton.y());
	showAll();
	_firstInput.setText(QString());
	_firstInput.updatePlaceholder();
	_lastInput.setText(QString());
	_lastInput.updatePlaceholder();
	_phoneInput.setText(QString());
	_phoneInput.updatePlaceholder();
	_phoneInput.setDisabled(false);
	_retryButton.hide();
	_firstInput.setFocus();
	resize(_width, _height);
	update();
}

void AddContactBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

AddContactBox::~AddContactBox() {
}
